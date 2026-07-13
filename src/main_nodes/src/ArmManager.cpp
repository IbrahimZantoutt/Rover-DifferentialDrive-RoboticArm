#include "rclcpp/rclcpp.hpp"


class ArmManager : public rclcpp::Node
{
    public:
    ArmManager() : Node("arm_manager"){

        RCLCPP_INFO(this->get_logger(), "Arm Manager Node has been started.");
        travel_pose_client_ = this->create_client<std_srvs::srv::Trigger>("travel_pose");
        vision_node_client_ = this->create_client<bin_interfaces::srv::GetTargets>("get_targets");
        arm_pick_client_ = this->create_client<std_srvs::srv::Trigger>("pickup");
        start_search_client_ = this->create_client<bin_interfaces::srv::StartSearch>("start_search");

    }

    void startCycle(){
        auto requestTravelPose = std::make_shared<std_srvs::srv::Trigger::Request>();
        travel_pose_client_->send_async_request(requestTravelPose, [this](rclcpp::Client<AddTwoInts>::SharedFuture ftr){
            RCLCPP_INFO(this->get_logger(), "travel pose done");

            auto requestStartSearch = std::make_shared<bin_interfaces::srv::StartSearch::Request>();
            reqyestStartSearch->start = true;
            start_search_client_->send_async_request(requestStartSearch, [this](rclcpp::Client<AddTwoInts>::SharedFuture ftr2){
                RCLCPP_INFO(this->get_logger(), "searching");
            });
        });
    }

    private:
        rclcpp::Client<std_srvs::srv>::SharedPtr travel_pose_client_;
        rclcpp::Client<bin_interfaces::srv::GetTargets>::SharedPtr vision_node_client_;
        rclcpp::Client<std_srvs::srv>::SharedPtr travel_pose_client_;
        rclcpp::Client<bin_interfaces::srv::StartSearch>::SharedPtr start_search_client_;

};

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<ArmManager>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}