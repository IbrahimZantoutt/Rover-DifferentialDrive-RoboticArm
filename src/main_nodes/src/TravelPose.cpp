#include "rclcpp/rclcpp.hpp"
#include <moveit/move_group_interface/move_group_interface.h>
#include <std_srvs/srv/trigger.hpp>
#include <thread>
#include <vector>

using moveit::planning_interface::MoveGroupInterface;

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("travel_pose");
    auto logger = node->get_logger();

    // The service callback drives MoveGroup, and plan()/execute() block while they
    // wait on move_group's action responses. Give the service its own callback
    // group and spin with a MultiThreadedExecutor so those responses are serviced
    // on another thread -- a single-threaded executor would deadlock in-callback.
    auto srv_group =
        node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

    rclcpp::executors::MultiThreadedExecutor executor;
    executor.add_node(node);
    std::thread spin_thread([&executor]() { executor.spin(); });

    MoveGroupInterface arm(node, "arm");
    arm.setMaxVelocityScalingFactor(0.3);
    arm.setMaxAccelerationScalingFactor(0.3);
    arm.setPlanningTime(5.0);

    auto fold_to_travel = [&]() -> bool {
        arm.setJointValueTarget(std::vector<double>{
            -1.477,    // j1_waist
            -1.0,      // j2_shoulder  (fold down)
             2.0,      // j3_elbow
            -1.477,    // j4_wrist
             0.0,      // j5_wrist
             0.0       // j6_wrist
        });
        MoveGroupInterface::Plan plan;
        bool ok = (arm.plan(plan) == moveit::core::MoveItErrorCode::SUCCESS);
        RCLCPP_INFO(logger, "Plan to [travel]: %s", ok ? "SUCCESS" : "FAILED");
        if (ok) arm.execute(plan);
        return ok;
    };

    auto srv = node->create_service<std_srvs::srv::Trigger>(
        "travel_pose",
        [&](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
            std::shared_ptr<std_srvs::srv::Trigger::Response> res) {
            RCLCPP_INFO(logger, "travel_pose service called");
            const bool ok = fold_to_travel();
            res->success = ok;
            res->message = ok ? "folded to travel pose" : "planning failed";
        },
        rmw_qos_profile_services_default, srv_group);

    spin_thread.join();   // block until shutdown (Ctrl-C stops the executor)
    rclcpp::shutdown();
    return 0;
}
