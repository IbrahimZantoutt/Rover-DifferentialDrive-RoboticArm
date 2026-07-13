#include "rclcpp/rclcpp.hpp"
#include <moveit/move_group_interface/move_group_interface.h>
#include <thread>

using moveit::planning_interface::MoveGroupInterface;
int main(int argc,char **argv){

    rclcpp::init(argc,argv);
    auto node = std::make_shared<rclcpp::Node>("travel_pose");
    auto logger = node->get_logger();

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    std::thread spin_thread([&executor]() { executor.spin(); });

    node->create_service<std_srvs::srv::Trigger>("travel_pose", [&](const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
      std::shared_ptr<std_srvs::srv::Trigger::Response> response){
          RCLCPP_INFO(logger, "Travel pose service called");
          response->success = true;
          response->message = "Travel pose service called";
          travel_process();
          return true;
      });

    auto travel_process = [&](){
      MoveGroupInterface arm(node, "arm");
      arm.setMaxVelocityScalingFactor(0.3);
      arm.setMaxAccelerationScalingFactor(0.3);
      arm.setPlanningTime(5.0);

      auto plan_and_execute = [&](const std::string & what) {
        MoveGroupInterface::Plan plan;
        bool ok = (arm.plan(plan) == moveit::core::MoveItErrorCode::SUCCESS);
        RCLCPP_INFO(logger, "Plan to [%s]: %s", what.c_str(), ok ? "SUCCESS" : "FAILED");
        if (ok) {
          arm.execute(plan);
        }
        return ok;
      };

      arm.setJointValueTarget(std::vector<double>{
          -1.477,    // j1_waist
          -1.0,   // j2_shoulder  (fold down)
          2.0,    // j3_elbow
          -1.477,    // j4_wrist
          0.0,    // j5_wrist
          0.0     // j6_wrist
      });
      plan_and_execute("travel");
    }

    rclcpp::shutdown(); spin_thread.join();
    return 0;
}