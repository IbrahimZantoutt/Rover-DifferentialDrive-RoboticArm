// Detection node: watch /scan_objects (the "novel objects only" scan), pick the
// closest detection, and send Nav2 to a pose that stops the robot in front of it
// with the front-mounted camera pointing at it.
//
// Flow:
//   1. /scan_objects -> closest finite return = nearest object (smallest range).
//   2. Build a goal along the current bearing to it, at (range - standoff) from
//      the robot, yaw = that bearing (so the robot's +x / camera faces the object).
//   3. Transform the goal from the lidar frame into 'map' via TF.
//   4. Send it to Nav2's navigate_to_pose action.
//
// standoff = distance from the ROBOT BASE (robot_base) to the object. Nav2 drives
// the base, and the object is an obstacle in the nav costmap (0.55 m inflation),
// with the footprint front at 0.45 m -- so a base-to-object distance much below
// ~0.6 m gets rejected as in-collision. The camera is ~0.4 m forward on the base,
// so standoff 0.6 leaves the camera ~0.2 m from the object.

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
    standoff_ = this->declare_parameter("standoff", 0.3);              // base->object [m]
    target_frame_ = this->declare_parameter("target_frame", std::string("map"));

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

    // Pulsed the instant we commit a Nav2 goal, so the orchestrator can tell
    // "driving to a cube" (found) apart from "no cubes left" (never fires).
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
        // stays latched (navigating_ = true) so it doesn't immediately re-drive;
        // the orchestrator re-arms via start_search (which clears the latch).
        active_ = false;  // one-shot: stop until re-armed
        publishReached(true);
        break;
      }
      case rclcpp_action::ResultCode::ABORTED: {
        RCLCPP_WARN(get_logger(),
          "Nav2 aborted. Staying COMMITTED to this one goal (one-shot): we do NOT "
          "recompute a fresh goal from the robot's drifted position, which is what "
          "made it approach from a random angle. Restart the node to try again.");
        // navigating_ stays true -> no re-goal, no random re-approach
        active_ = false;  // one-shot: stop until re-armed
        publishReached(false);
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

  double standoff_;
  std::string target_frame_;
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
