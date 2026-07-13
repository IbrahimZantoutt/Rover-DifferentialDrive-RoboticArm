// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from bin_interfaces:srv/StartSearch.idl
// generated code does not contain a copyright notice

#ifndef BIN_INTERFACES__SRV__DETAIL__START_SEARCH__BUILDER_HPP_
#define BIN_INTERFACES__SRV__DETAIL__START_SEARCH__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "bin_interfaces/srv/detail/start_search__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace bin_interfaces
{

namespace srv
{

namespace builder
{

class Init_StartSearch_Request_start
{
public:
  Init_StartSearch_Request_start()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::bin_interfaces::srv::StartSearch_Request start(::bin_interfaces::srv::StartSearch_Request::_start_type arg)
  {
    msg_.start = std::move(arg);
    return std::move(msg_);
  }

private:
  ::bin_interfaces::srv::StartSearch_Request msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::bin_interfaces::srv::StartSearch_Request>()
{
  return bin_interfaces::srv::builder::Init_StartSearch_Request_start();
}

}  // namespace bin_interfaces


namespace bin_interfaces
{

namespace srv
{

namespace builder
{

class Init_StartSearch_Response_success
{
public:
  Init_StartSearch_Response_success()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  ::bin_interfaces::srv::StartSearch_Response success(::bin_interfaces::srv::StartSearch_Response::_success_type arg)
  {
    msg_.success = std::move(arg);
    return std::move(msg_);
  }

private:
  ::bin_interfaces::srv::StartSearch_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::bin_interfaces::srv::StartSearch_Response>()
{
  return bin_interfaces::srv::builder::Init_StartSearch_Response_success();
}

}  // namespace bin_interfaces

#endif  // BIN_INTERFACES__SRV__DETAIL__START_SEARCH__BUILDER_HPP_
