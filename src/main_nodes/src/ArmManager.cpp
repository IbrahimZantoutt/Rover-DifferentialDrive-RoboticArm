// ArmManager: the mission orchestrator. Owns the state machine and drives the
// dumb worker nodes over their services/actions:
//
//   loop:
//     travel_pose            (fold the arm before any base movement)
//     start_search           (arm detection_node; it drives to the nearest cube)
//     wait /object_reached   (true = arrived, false = nav failed, timeout = no cubes left)
//     pickup                 (grasp the cube in front of the camera)
//     travel_pose            (tuck the carried cube before driving)
//     navigate_to_pose       (drive to the fixed dropoff in front of the table)
//     place                  (set the cube down, release)
//
// A search that yields nothing (timeout) ends the mission. The whole sequence runs
// on a worker thread with blocking calls; a MultiThreadedExecutor services the
// service/action responses on other threads, so the blocking calls don't deadlock.

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/parameter_client.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/empty.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "bin_interfaces/srv/start_search.hpp"
#include "tf2/LinearMath/Quaternion.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

using namespace std::chrono_literals;
using Trigger = std_srvs::srv::Trigger;
using StartSearch = bin_interfaces::srv::StartSearch;
using NavigateToPose = nav2_msgs::action::NavigateToPose;
using NavGoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

class ArmManager : public rclcpp::Node
{
public:
  ArmManager() : Node("arm_manager")
  {
    dropoff_x_    = this->declare_parameter("dropoff_x", 1.76);
    dropoff_y_    = this->declare_parameter("dropoff_y", 0.0);
    dropoff_yaw_  = this->declare_parameter("dropoff_yaw", 0.0);
    target_frame_ = this->declare_parameter("target_frame", std::string("map"));
    found_timeout_   = std::chrono::seconds(this->declare_parameter("found_timeout_s", 30));
    reached_timeout_ = std::chrono::seconds(this->declare_parameter("reached_timeout_s", 180));
    action_timeout_ = std::chrono::seconds(this->declare_parameter("action_timeout_s", 60));
    nav_timeout_    = std::chrono::seconds(this->declare_parameter("nav_timeout_s", 120));
    max_failures_   = this->declare_parameter("max_failures", 3);
    // Staging waypoint driven to BEFORE the dropoff so the final leg always starts
    // from the same spot -> same approach direction -> consistent dropoff yaw.
    stage_x_ = this->declare_parameter("dropoff_via_x", 0.0);
    stage_y_ = this->declare_parameter("dropoff_via_y", 0.0);
    // Per-journey xy goal tolerance: tight for the cube approach (push into the
    // costmap inflation so the cube is within arm reach), loose for the dropoff so
    // the robot stops on arrival instead of fidgeting to nail the exact spot.
    approach_xy_tol_ = this->declare_parameter("approach_xy_tolerance", 0.2);
    dropoff_xy_tol_  = this->declare_parameter("dropoff_xy_tolerance", 0.4);

    travel_client_ = this->create_client<Trigger>("travel_pose");
    search_client_ = this->create_client<StartSearch>("start_search");
    pickup_client_ = this->create_client<Trigger>("pickup");
    place_client_  = this->create_client<Trigger>("place");
    nav_client_    = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
    // Lets us swap the Nav2 controller's xy_goal_tolerance per leg at runtime.
    controller_params_ = std::make_shared<rclcpp::AsyncParametersClient>(this, "controller_server");

    // Match detection_node's transient-local (latched) QoS on both topics.
    reached_sub_ = this->create_subscription<std_msgs::msg::Bool>(
      "/object_reached", rclcpp::QoS(1).transient_local(),
      std::bind(&ArmManager::onReached, this, std::placeholders::_1));
    found_sub_ = this->create_subscription<std_msgs::msg::Empty>(
      "/object_found", rclcpp::QoS(1).transient_local(),
      std::bind(&ArmManager::onFound, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(), "arm_manager up: dropoff (%.2f, %.2f) yaw %.2f in '%s'",
                dropoff_x_, dropoff_y_, dropoff_yaw_, target_frame_.c_str());
  }

  void start() { worker_ = std::thread(&ArmManager::runMission, this); }
  void join()  { if (worker_.joinable()) worker_.join(); }

private:
  // ---- detection's two signals: /object_found (started driving) then --------
  // ---- /object_reached (drive finished, with success/failure) ----------------
  void onFound(const std_msgs::msg::Empty::SharedPtr)
  {
    std::lock_guard<std::mutex> lk(m_);
    found_got_ = true;
    cv_.notify_all();
  }

  void onReached(const std_msgs::msg::Bool::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lk(m_);
    reached_got_ = true;
    reached_ok_  = msg->data;
    cv_.notify_all();
  }

  // Reset both signals before arming detection for a fresh search cycle.
  void armSearch()
  {
    std::lock_guard<std::mutex> lk(m_);
    found_got_   = false;
    reached_got_ = false;
  }

  // true = detection committed to a cube (driving); false = timed out => no cubes.
  bool waitForFound(std::chrono::seconds timeout)
  {
    std::unique_lock<std::mutex> lk(m_);
    return cv_.wait_for(lk, timeout, [this] { return found_got_; });
  }

  // nullopt = the drive never reported back in time, else true/false from Nav2.
  std::optional<bool> waitForReached(std::chrono::seconds timeout)
  {
    std::unique_lock<std::mutex> lk(m_);
    if (cv_.wait_for(lk, timeout, [this] { return reached_got_; }))
      return reached_ok_;
    return std::nullopt;
  }

  // ---- blocking service calls (run on the worker thread) -----------------------
  bool callTrigger(const rclcpp::Client<Trigger>::SharedPtr & client, const char * name)
  {
    if (!client->wait_for_service(5s)) {
      RCLCPP_ERROR(get_logger(), "%s service not available", name);
      return false;
    }
    auto fut = client->async_send_request(std::make_shared<Trigger::Request>());
    if (fut.wait_for(action_timeout_) != std::future_status::ready) {
      RCLCPP_ERROR(get_logger(), "%s timed out", name);
      return false;
    }
    auto res = fut.get();
    RCLCPP_INFO(get_logger(), "%s -> %s (%s)",
                name, res->success ? "OK" : "FAIL", res->message.c_str());
    return res->success;
  }

  bool callStartSearch()
  {
    if (!search_client_->wait_for_service(5s)) {
      RCLCPP_ERROR(get_logger(), "start_search service not available");
      return false;
    }
    auto req = std::make_shared<StartSearch::Request>();
    req->start = true;
    auto fut = search_client_->async_send_request(req);
    if (fut.wait_for(5s) != std::future_status::ready) {
      RCLCPP_ERROR(get_logger(), "start_search timed out");
      return false;
    }
    return fut.get()->success;
  }

  // ---- swap the controller's xy goal tolerance at runtime ----------------------
  // general_goal_checker.xy_goal_tolerance lives on controller_server; changing it
  // per leg keeps the cube approach tight while the dropoff stops on arrival.
  void setXyGoalTolerance(double v)
  {
    if (!controller_params_->wait_for_service(3s)) {
      RCLCPP_WARN(get_logger(), "controller_server params unavailable; xy tol unchanged");
      return;
    }
    auto fut = controller_params_->set_parameters(
      {rclcpp::Parameter("general_goal_checker.xy_goal_tolerance", v)});
    if (fut.wait_for(3s) != std::future_status::ready) {
      RCLCPP_WARN(get_logger(), "set xy_goal_tolerance timed out");
      return;
    }
    const auto results = fut.get();
    const bool ok = !results.empty() && results[0].successful;
    RCLCPP_INFO(get_logger(), "xy_goal_tolerance -> %.2f (%s)", v, ok ? "OK" : "FAIL");
  }

  // ---- send one navigate_to_pose goal and block until it finishes --------------
  bool navToPose(double x, double y, double yaw, const char * what)
  {
    if (!nav_client_->wait_for_action_server(5s)) {
      RCLCPP_ERROR(get_logger(), "navigate_to_pose action server not available");
      return false;
    }

    NavigateToPose::Goal goal;
    goal.pose.header.frame_id = target_frame_;
    goal.pose.header.stamp = now();
    goal.pose.pose.position.x = x;
    goal.pose.pose.position.y = y;
    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, yaw);
    goal.pose.pose.orientation.x = q.x();
    goal.pose.pose.orientation.y = q.y();
    goal.pose.pose.orientation.z = q.z();
    goal.pose.pose.orientation.w = q.w();

    auto gh_fut = nav_client_->async_send_goal(goal);
    if (gh_fut.wait_for(10s) != std::future_status::ready) {
      RCLCPP_ERROR(get_logger(), "%s goal send timed out", what);
      return false;
    }
    NavGoalHandle::SharedPtr gh = gh_fut.get();
    if (!gh) {
      RCLCPP_ERROR(get_logger(), "%s goal rejected by Nav2", what);
      return false;
    }
    auto res_fut = nav_client_->async_get_result(gh);
    if (res_fut.wait_for(nav_timeout_) != std::future_status::ready) {
      RCLCPP_ERROR(get_logger(), "%s nav timed out", what);
      return false;
    }
    return res_fut.get().code == rclcpp_action::ResultCode::SUCCEEDED;
  }

  // ---- drive to the fixed dropoff, always approaching from the staging point ----
  // RPP keeps whatever heading it approached with, so the dropoff yaw used to vary
  // with wherever the cube was picked. Staging at (stage_x_, stage_y_) first makes
  // the final leg always start from the same place -> same approach -> same yaw.
  bool navToDropoff()
  {
    const double bearing = std::atan2(dropoff_y_ - stage_y_, dropoff_x_ - stage_x_);
    if (!navToPose(stage_x_, stage_y_, bearing, "staging")) return false;
    // Loosen the tolerance for the final leg so the robot stops on arrival (no
    // fidgeting to hit the exact spot), then restore the tight approach tolerance.
    setXyGoalTolerance(dropoff_xy_tol_);
    const bool ok = navToPose(dropoff_x_, dropoff_y_, dropoff_yaw_, "dropoff");
    setXyGoalTolerance(approach_xy_tol_);
    return ok;
  }

  // ---- the mission ------------------------------------------------------------
  void runMission()
  {
    RCLCPP_INFO(get_logger(), "mission starting");
    int failures = 0;

    while (rclcpp::ok()) {
      // Fold the arm before any base movement (also un-extends it after a place).
      if (!callTrigger(travel_client_, "travel_pose")) {
        RCLCPP_ERROR(get_logger(), "fold failed -- aborting mission");
        break;
      }

      // Search, then wait briefly for a commit. No commit => nothing left => done.
      armSearch();
      if (!callStartSearch()) {
        RCLCPP_ERROR(get_logger(), "start_search failed -- aborting mission");
        break;
      }
      RCLCPP_INFO(get_logger(), "searching for the next cube...");

      if (!waitForFound(found_timeout_)) {
        RCLCPP_INFO(get_logger(), "no cube found within %llds -- mission complete",
                    static_cast<long long>(found_timeout_.count()));
        break;
      }

      // A cube was found and detection is driving to it; wait (long) for arrival.
      RCLCPP_INFO(get_logger(), "cube found -- driving to it");
      auto reached = waitForReached(reached_timeout_);
      if (!reached.has_value()) {
        RCLCPP_ERROR(get_logger(), "no arrival within %llds -- aborting mission",
                    static_cast<long long>(reached_timeout_.count()));
        break;
      }
      if (!reached.value()) {
        if (++failures > max_failures_) {
          RCLCPP_ERROR(get_logger(), "too many approach failures -- aborting mission");
          break;
        }
        RCLCPP_WARN(get_logger(), "approach failed (%d/%d) -- retrying search",
                    failures, max_failures_);
        continue;
      }
      failures = 0;

      if (!callTrigger(pickup_client_, "pickup")) {
        RCLCPP_WARN(get_logger(), "pickup failed -- back to search");
        continue;
      }

      // Tuck the carried cube, drive to the dropoff, set it down.
      if (!callTrigger(travel_client_, "travel_pose")) {
        RCLCPP_ERROR(get_logger(), "carry-fold failed -- aborting mission");
        break;
      }
      if (!navToDropoff()) {
        RCLCPP_ERROR(get_logger(), "dropoff nav failed (still holding cube) -- aborting mission");
        break;
      }
      if (!callTrigger(place_client_, "place")) {
        RCLCPP_WARN(get_logger(), "place failed");
      }
    }

    RCLCPP_INFO(get_logger(), "mission finished");
    rclcpp::shutdown();
  }

  // params
  double dropoff_x_, dropoff_y_, dropoff_yaw_;
  double stage_x_{0.0}, stage_y_{0.0};
  std::string target_frame_;
  std::chrono::seconds found_timeout_{10}, reached_timeout_{180},
                       action_timeout_{60}, nav_timeout_{120};
  int max_failures_{3};
  double approach_xy_tol_{0.2}, dropoff_xy_tol_{0.4};

  // clients / subscription
  rclcpp::Client<Trigger>::SharedPtr travel_client_, pickup_client_, place_client_;
  rclcpp::Client<StartSearch>::SharedPtr search_client_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;
  rclcpp::AsyncParametersClient::SharedPtr controller_params_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr reached_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr found_sub_;

  // detection-signal synchronization (/object_found + /object_reached)
  std::mutex m_;
  std::condition_variable cv_;
  bool found_got_   = false;
  bool reached_got_ = false;
  bool reached_ok_  = false;

  std::thread worker_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ArmManager>();

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  node->start();     // launch the mission worker thread
  executor.spin();   // service responses handled here until the mission shuts down
  node->join();

  rclcpp::shutdown();
  return 0;
}
