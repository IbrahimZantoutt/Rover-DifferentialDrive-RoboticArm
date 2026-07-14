// Detection node: watch /scan_objects, take the closest return as the nearest
// object, and send Nav2 to a standoff pose in front of it (camera facing it).
// Gated by the start_search service.
//
// standoff = base->object distance. Below ~0.6 m Nav2 rejects the goal as
// in-collision (0.55 m costmap inflation); the camera is ~0.4 m forward.

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/empty.hpp"
#include "bin_interfaces/srv/start_search.hpp"

using NavigateToPose = nav2_msgs::action::NavigateToPose;
using GoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

class DetectionNode : public rclcpp::Node
{
public:
  DetectionNode() : Node("detection_node")
  {
    standoff_ = this->declare_parameter("standoff", 0.4);              // base->object [m]
    target_frame_ = this->declare_parameter("target_frame", std::string("map"));
    // If Nav2 ABORTS the approach but the base ended within this of the goal, count
    // it as arrived (see onResult) instead of bouncing back to detection.
    arrival_tolerance_ = this->declare_parameter("arrival_tolerance", 0.6);       // [m]
    robot_base_frame_ = this->declare_parameter("robot_base_frame", std::string("robot_base"));

    tf_buffer_ = std::make_shared<tf2_ros::Buffer>(this->get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

    nav_client_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");

    scan_sub_ = this->create_subscription<sensor_msgs::msg::LaserScan>(
      "scan_objects", 10,
      std::bind(&DetectionNode::onScan, this, std::placeholders::_1));

    lever_ = this->create_service<bin_interfaces::srv::StartSearch>(
      "start_search",
      [this](const std::shared_ptr<bin_interfaces::srv::StartSearch::Request> req,
             std::shared_ptr<bin_interfaces::srv::StartSearch::Response> res) {
              if(req->start == true){
                active_ = true;
                navigating_ = false;  // clear the previous one-shot latch so we can re-search
                RCLCPP_INFO(get_logger(), "detection_node: search activated");
                res->success = true;
              }
      });

    reached_pub_ = this->create_publisher<std_msgs::msg::Bool>(
      "/object_reached", rclcpp::QoS(1).transient_local());

    // Pulsed when we commit a Nav2 goal, so the orchestrator can tell "driving"
    // (found) from "no cubes left" (never fires).
    found_pub_ = this->create_publisher<std_msgs::msg::Empty>(
      "/object_found", rclcpp::QoS(1).transient_local());

    RCLCPP_INFO(get_logger(),
      "detection_node up: watching /scan_objects, standoff %.2f m, frame '%s'",
      standoff_, target_frame_.c_str());
  }

private:
  void onScan(const sensor_msgs::msg::LaserScan::SharedPtr msg)
  {
    if (!active_) return;
    if (navigating_) return;  // one goal at a time

    // 1) closest detection = smallest finite range
    int best_i = -1;
    float best_r = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < msg->ranges.size(); ++i) {
      const float r = msg->ranges[i];
      if (std::isfinite(r) && r < best_r) { best_r = r; best_i = static_cast<int>(i); }
    }
    if (best_i < 0) return;  // nothing detected this scan

    const double theta = msg->angle_min + best_i * msg->angle_increment;  // bearing [rad]
    const double reach = best_r - standoff_;                              // how far to drive up the bearing
    if (reach <= 0.0) {
      RCLCPP_WARN(get_logger(),
        "closest object is %.2f m away, inside the %.2f m standoff -- skipping",
        best_r, standoff_);
      return;
    }

    // 2) goal in the LIDAR frame: along the bearing, facing the object
    geometry_msgs::msg::PoseStamped goal_lidar;
    goal_lidar.header.frame_id = msg->header.frame_id;
    goal_lidar.header.stamp = msg->header.stamp;
    goal_lidar.pose.position.x = reach * std::cos(theta);
    goal_lidar.pose.position.y = reach * std::sin(theta);
    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, theta);  // +x points at the object -> camera faces it
    goal_lidar.pose.orientation = tf2::toMsg(q);

    // 3) transform the goal into the world (map) frame
    geometry_msgs::msg::PoseStamped goal_map;
    try {
      const auto tf = tf_buffer_->lookupTransform(
        target_frame_, msg->header.frame_id, tf2::TimePointZero);  // latest
      tf2::doTransform(goal_lidar, goal_map, tf);
    } catch (const tf2::TransformException & e) {
      RCLCPP_WARN(get_logger(), "no TF %s <- %s: %s",
        target_frame_.c_str(), msg->header.frame_id.c_str(), e.what());
      return;
    }
    goal_map.header.frame_id = target_frame_;
    goal_map.header.stamp = now();

    // 4) send the goal to Nav2
    if (!nav_client_->action_server_is_ready() &&
        !nav_client_->wait_for_action_server(std::chrono::seconds(2))) {
      RCLCPP_WARN(get_logger(), "navigate_to_pose server not available yet");
      return;
    }

    NavigateToPose::Goal goal;
    goal.pose = goal_map;

    rclcpp_action::Client<NavigateToPose>::SendGoalOptions opts;
    opts.goal_response_callback =
      [this](const GoalHandle::SharedPtr & h) {
        if (!h) { RCLCPP_WARN(get_logger(), "Nav2 rejected the goal"); navigating_ = false; }
      };
    opts.result_callback =
      std::bind(&DetectionNode::onResult, this, std::placeholders::_1);

    navigating_ = true;
    committed_goal_ = goal_map;   // remember where we told the base to stop
    have_goal_ = true;
    RCLCPP_INFO(get_logger(),
      "closest object %.2f m @ %.0f deg -> goal (%.2f, %.2f) in %s; navigating",
      best_r, theta * 180.0 / M_PI,
      goal_map.pose.position.x, goal_map.pose.position.y, target_frame_.c_str());
    nav_client_->async_send_goal(goal, opts);
    found_pub_->publish(std_msgs::msg::Empty());  // committed to a cube -> driving
  }

  void onResult(const GoalHandle::WrappedResult & result)
  {
    switch (result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED: {
        RCLCPP_INFO(get_logger(), "reached the object -- camera is facing it");
        // stays latched (navigating_ = true) so it doesn't re-drive; orchestrator
        // re-arms via start_search.
        active_ = false;  // one-shot: stop until re-armed
        publishReached(true);
        break;
      }
      case rclcpp_action::ResultCode::ABORTED: {
        // Nav2 often ABORTS the last stretch even though the base is at the cube
        // (progress-checker trip, or goal inside the costmap inflation). If we
        // ended within arrival_tolerance, treat it as reached so the pickup runs.
        double dist = -1.0;
        const bool have_dist = distanceToGoal(dist);
        if (have_dist && dist <= arrival_tolerance_) {
          RCLCPP_WARN(get_logger(),
            "Nav2 aborted but base is %.2f m from goal (<= %.2f) -- accepting as "
            "reached; running pickup", dist, arrival_tolerance_);
          active_ = false;         // one-shot: stop until re-armed
          publishReached(true);    // commit: pickup fires, no re-detect
        } else {
          RCLCPP_WARN(get_logger(),
            "Nav2 aborted and base is %.2f m from goal (> %.2f) -- approach failed. "
            "Staying COMMITTED; we do NOT recompute from a drifted pose.",
            dist, arrival_tolerance_);
          active_ = false;  // one-shot: stop until re-armed
          publishReached(false);
        }
        break;
      }
      case rclcpp_action::ResultCode::CANCELED: {
        RCLCPP_WARN(get_logger(), "Nav2 canceled");
        navigating_ = false;
        active_ = false;  // one-shot: stop until re-armed
        publishReached(false);
        break;
      }
      default: {
        RCLCPP_WARN(get_logger(), "Nav2 returned an unknown result");
        navigating_ = false;
        active_ = false;  // one-shot: stop until re-armed
        publishReached(false);
        break;
      }
    }
  }

  void publishReached(bool ok)
  {
    std_msgs::msg::Bool m;
    m.data = ok;
    reached_pub_->publish(m);
  }

  // Planar distance [m] from the robot base to the last committed goal (map frame).
  // Returns false if no goal committed yet or TF map<-robot_base is unavailable.
  bool distanceToGoal(double & dist_out)
  {
    if (!have_goal_) return false;
    try {
      const auto tf = tf_buffer_->lookupTransform(
        target_frame_, robot_base_frame_, tf2::TimePointZero);  // latest
      const double dx = tf.transform.translation.x - committed_goal_.pose.position.x;
      const double dy = tf.transform.translation.y - committed_goal_.pose.position.y;
      dist_out = std::hypot(dx, dy);
      return true;
    } catch (const tf2::TransformException & e) {
      RCLCPP_WARN(get_logger(), "distanceToGoal: no TF %s <- %s: %s",
        target_frame_.c_str(), robot_base_frame_.c_str(), e.what());
      return false;
    }
  }

  double standoff_;
  std::string target_frame_;
  double arrival_tolerance_;
  std::string robot_base_frame_;
  geometry_msgs::msg::PoseStamped committed_goal_;
  bool have_goal_ = false;
  bool navigating_ = false;
  bool active_ = false;

  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

  rclcpp::Service<bin_interfaces::srv::StartSearch>::SharedPtr lever_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr reached_pub_;
  rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr found_pub_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DetectionNode>());
  rclcpp::shutdown();
  return 0;
}
