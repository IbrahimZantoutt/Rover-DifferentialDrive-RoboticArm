// generated from rosidl_generator_cpp/resource/idl__builder.hpp.em
// with input from bin_interface:srv/GetTargets.idl
// generated code does not contain a copyright notice

#ifndef BIN_INTERFACE__SRV__DETAIL__GET_TARGETS__BUILDER_HPP_
#define BIN_INTERFACE__SRV__DETAIL__GET_TARGETS__BUILDER_HPP_

#include <algorithm>
#include <utility>

#include "bin_interface/srv/detail/get_targets__struct.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"


namespace bin_interface
{

namespace srv
{


}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::bin_interface::srv::GetTargets_Request>()
{
  return ::bin_interface::srv::GetTargets_Request(rosidl_runtime_cpp::MessageInitialization::ZERO);
}

}  // namespace bin_interface


namespace bin_interface
{

namespace srv
{

namespace builder
{

class Init_GetTargets_Response_target
{
public:
  explicit Init_GetTargets_Response_target(::bin_interface::srv::GetTargets_Response & msg)
  : msg_(msg)
  {}
  ::bin_interface::srv::GetTargets_Response target(::bin_interface::srv::GetTargets_Response::_target_type arg)
  {
    msg_.target = std::move(arg);
    return std::move(msg_);
  }

private:
  ::bin_interface::srv::GetTargets_Response msg_;
};

class Init_GetTargets_Response_found
{
public:
  Init_GetTargets_Response_found()
  : msg_(::rosidl_runtime_cpp::MessageInitialization::SKIP)
  {}
  Init_GetTargets_Response_target found(::bin_interface::srv::GetTargets_Response::_found_type arg)
  {
    msg_.found = std::move(arg);
    return Init_GetTargets_Response_target(msg_);
  }

private:
  ::bin_interface::srv::GetTargets_Response msg_;
};

}  // namespace builder

}  // namespace srv

template<typename MessageType>
auto build();

template<>
inline
auto build<::bin_interface::srv::GetTargets_Response>()
{
  return bin_interface::srv::builder::Init_GetTargets_Response_found();
}

}  // namespace bin_interface

#endif  // BIN_INTERFACE__SRV__DETAIL__GET_TARGETS__BUILDER_HPP_
