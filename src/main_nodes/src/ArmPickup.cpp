#include "rclcpp/rclcpp.hpp"
#include <moveit/move_group_interface/move_group_interface.h>
#include <geometry_msgs/msg/pose.hpp>
#include <tf2/LinearMath/Quaternion.h>
#include <linkattacher_msgs/srv/attach_link.hpp>
#include <linkattacher_msgs/srv/detach_link.hpp>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/msg/collision_object.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <bin_interfaces/srv/get_targets.hpp>

#include <chrono>
#include <map>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using moveit::planning_interface::MoveGroupInterface;

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>(
        "arm_pickup_node",
        rclcpp::NodeOptions().automatically_declare_parameters_from_overrides(true));
    auto logger = node->get_logger();

    auto client = node->create_client<bin_interfaces::srv::GetTargets>("get_targets");
    auto request = std::make_shared<bin_interfaces::srv::GetTargets::Request>();

    int action_count_ = 0;

    // The pickup/place callbacks drive MoveGroup and block on service-client
    // responses; run them in their own callback group under a MultiThreadedExecutor
    // so those responses are serviced on other threads (no single-thread deadlock).
    auto srv_group =
        node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

    rclcpp::executors::MultiThreadedExecutor executor;
    executor.add_node(node);
    std::thread spin_thread([&executor]() { executor.spin(); });

    MoveGroupInterface arm(node, "arm");
    arm.setPlanningTime(5.0);
    arm.setMaxVelocityScalingFactor(0.5);
    arm.setMaxAccelerationScalingFactor(0.5);
    arm.setGoalOrientationTolerance(0.3);   // radians, before setPoseTarget

    MoveGroupInterface gripper(node, "gripper");
    gripper.setMaxVelocityScalingFactor(0.5);
    gripper.setMaxAccelerationScalingFactor(0.5);
    gripper.setPlanningTime(5.0);

    auto search = [&](geometry_msgs::msg::Point & out) -> bool {
        if (!client->wait_for_service(5s)) {
            RCLCPP_ERROR(logger, "get_targets service not available (is VisionNode running?)");
            return false;
        }
        auto future = client->async_send_request(request);
        if (future.wait_for(5s) != std::future_status::ready) {
            RCLCPP_ERROR(logger, "get_targets call timed out");
            return false;
        }
        auto response = future.get();
        if (response->found) {
            out = response->target.point;
            RCLCPP_INFO(logger, "Target at: x=%f y=%f z=%f", out.x, out.y, out.z);
            return true;
        }
        RCLCPP_WARN(logger, "No target found.");
        return false;
    };

    auto plan_and_execute = [&](const std::string &what) {
        MoveGroupInterface::Plan plan;
        bool ok = (arm.plan(plan) == moveit::core::MoveItErrorCode::SUCCESS);
        if (ok) {
            arm.execute(plan);
            RCLCPP_INFO(logger, "Executed plan for %s", what.c_str());
        }
        return ok;
    };

    // Service clients for the Gazebo LinkAttacher plugin.
    auto attach_client = node->create_client<linkattacher_msgs::srv::AttachLink>("/ATTACHLINK");
    auto detach_client = node->create_client<linkattacher_msgs::srv::DetachLink>("/DETACHLINK");

    auto attach = [&](const std::string & m1, const std::string & l1,
                      const std::string & m2, const std::string & l2){
        if (!attach_client->wait_for_service(5s)) {
            RCLCPP_ERROR(logger, "/ATTACHLINK service not available");
            return false;
        }
        auto req = std::make_shared<linkattacher_msgs::srv::AttachLink::Request>();
        req->model1_name = m1;
        req->link1_name  = l1;
        req->model2_name = m2;
        req->link2_name  = l2;
        auto future = attach_client->async_send_request(req);
        if (future.wait_for(5s) != std::future_status::ready) {
            RCLCPP_ERROR(logger, "/ATTACHLINK call timed out");
            return false;
        }
        auto res = future.get();
        RCLCPP_INFO(logger, "Attach: %s (%s)", res->success ? "OK" : "FAIL", res->message.c_str());
        return res->success;
    };

    auto detach = [&](const std::string & m1, const std::string & l1,
                      const std::string & m2, const std::string & l2){
        if (!detach_client->wait_for_service(5s)) {
            RCLCPP_ERROR(logger, "/DETACHLINK service not available");
            return false;
        }
        auto req = std::make_shared<linkattacher_msgs::srv::DetachLink::Request>();
        req->model1_name = m1;
        req->link1_name  = l1;
        req->model2_name = m2;
        req->link2_name  = l2;
        auto future = detach_client->async_send_request(req);
        if (future.wait_for(5s) != std::future_status::ready) {
            RCLCPP_ERROR(logger, "/DETACHLINK call timed out");
            return false;
        }
        auto res = future.get();
        RCLCPP_INFO(logger, "Detach: %s (%s)", res->success ? "OK" : "FAIL", res->message.c_str());
        return res->success;
    };

    auto lockBase   = [&](){ return attach("mobiarm", "robot_base", "ground_plane", "link"); };
    auto unlockBase = [&](){ return detach("mobiarm", "robot_base", "ground_plane", "link"); };


    auto setGripper = [&](double g1, double g2){
        gripper.setJointValueTarget({std::map<std::string, double>{{"gripperfinger_left_joint", g1}, {"gripperfinger_right_joint", g2}}});
        MoveGroupInterface::Plan plan;
        if (gripper.plan(plan) == moveit::core::MoveItErrorCode::SUCCESS) {
            gripper.execute(plan);
        }
    };

    auto goToPos = [&](double x, double y, double z, double roll, double pitch, double yaw){
        geometry_msgs::msg::Pose target_pose;
        target_pose.position.x = x;
        target_pose.position.y = y;
        target_pose.position.z = z;
        tf2::Quaternion q;
        q.setRPY(roll, pitch, yaw);
        target_pose.orientation.x = q.x();
        target_pose.orientation.y = q.y();
        target_pose.orientation.z = q.z();
        target_pose.orientation.w = q.w();
        arm.setPoseTarget(target_pose);
        return plan_and_execute("pose target");
    };

    auto pick = [&](double x, double y, double z) -> bool {
        //goToPos(0.304, 0.339, 0.247,2.863, 1.389, -3.141);  better to add when added collision aware movement
        lockBase();
        if (goToPos(x, y, z + 0.26, 1.5, 0, 0)) {
            action_count_++;
            // NOTE: attaches green_cube_<N> in call order, not the cube actually
            // grasped -- needs a position->model-name lookup for arbitrary layouts.
            std::string current_cube = "green_cube_" + std::to_string(action_count_);
            // The LinkAttacher plugin allows only ONE active attachment at a time
            // (single global IsAttached guard), so release the base->ground weld
            // BEFORE welding the wrist to the cube -- otherwise the cube attach is
            // rejected with "Both links have already been attached, aborting...".
            unlockBase();
            attach("mobiarm", "wrist_yaw_link", current_cube, "link");
            setGripper(0.01, 0.01);
            return true;
        }
        unlockBase();
        return false;
    };

    auto place = [&]() -> bool {
        //goToPos(0.374, -0.215, 0.327,-0.898, -1.442, 2.633);
        arm.setNamedTarget("placepose2");
        if (plan_and_execute("place pose target")) {
            std::string current_cube = "green_cube_" + std::to_string(action_count_);
            detach("mobiarm", "wrist_yaw_link", current_cube, "link");
            setGripper(0.0, 0.0);
            return true;
        }
        return false;
    };

    // Pick a single cube: pull the latest target from vision, then grasp it.
    auto pick_one = [&]() -> bool {
        geometry_msgs::msg::Point target;
        if (!search(target)) return false;
        return pick(target.x, target.y, target.z);
    };

    //  auto addCollisionObjects = [&](){
    //     moveit::planning_interface::PlanningSceneInterface planning_scene_interface;
    //     std::vector<moveit_msgs::msg::CollisionObject> collision_objects;
    //     collision_objects.resize(4);

    //     collision_objects[0].id = "camera";
    //     collision_objects[0].header.frame_id = "world";
    //     collision_objects[0].primitives.resize(1);
    //     collision_objects[0].primitives[0].type = shape_msgs::msg::SolidPrimitive::BOX;
    //     collision_objects[0].primitives[0].dimensions = {0.05, 0.1, 0.2};
    //     collision_objects[0].primitive_poses.resize(1);
    //     collision_objects[0].primitive_poses[0].position.x = 0.4;
    //     collision_objects[0].primitive_poses[0].position.y = 0;
    //     collision_objects[0].primitive_poses[0].position.z = 0.1;
    //     collision_objects[0].operation = moveit_msgs::msg::CollisionObject::ADD;

    //     planning_scene_interface.applyCollisionObjects(collision_objects);
    // };

    auto pickup_srv = node->create_service<std_srvs::srv::Trigger>(
        "pickup",
        [&](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
            std::shared_ptr<std_srvs::srv::Trigger::Response> res){
            RCLCPP_INFO(logger, "pickup service called");
            const bool ok = pick_one();
            res->success = ok;
            res->message = ok ? "picked cube" : "no target / pick failed";
        },
        rmw_qos_profile_services_default, srv_group);

    auto place_srv = node->create_service<std_srvs::srv::Trigger>(
        "place",
        [&](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
            std::shared_ptr<std_srvs::srv::Trigger::Response> res){
            RCLCPP_INFO(logger, "place service called");
            const bool ok = place();
            res->success = ok;
            res->message = ok ? "placed cube" : "place failed";
        },
        rmw_qos_profile_services_default, srv_group);

    spin_thread.join();   // block until shutdown (Ctrl-C stops the executor)
    rclcpp::shutdown();
    return 0;
}
