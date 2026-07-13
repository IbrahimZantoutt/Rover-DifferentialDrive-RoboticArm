// generated from rosidl_generator_cpp/resource/idl__traits.hpp.em
// with input from bin_interfaces:srv/StartSearch.idl
// generated code does not contain a copyright notice

#ifndef BIN_INTERFACES__SRV__DETAIL__START_SEARCH__TRAITS_HPP_
#define BIN_INTERFACES__SRV__DETAIL__START_SEARCH__TRAITS_HPP_

#include <stdint.h>

#include <sstream>
#include <string>
#include <type_traits>

#include "bin_interfaces/srv/detail/start_search__struct.hpp"
#include "rosidl_runtime_cpp/traits.hpp"

namespace bin_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const StartSearch_Request & msg,
  std::ostream & out)
{
  out << "{";
  // member: start
  {
    out << "start: ";
    rosidl_generator_traits::value_to_yaml(msg.start, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const StartSearch_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: start
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "start: ";
    rosidl_generator_traits::value_to_yaml(msg.start, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const StartSearch_Request & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace bin_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use bin_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const bin_interfaces::srv::StartSearch_Request & msg,
  std::ostream & out, size_t indentation = 0)
{
  bin_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use bin_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const bin_interfaces::srv::StartSearch_Request & msg)
{
  return bin_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<bin_interfaces::srv::StartSearch_Request>()
{
  return "bin_interfaces::srv::StartSearch_Request";
}

template<>
inline const char * name<bin_interfaces::srv::StartSearch_Request>()
{
  return "bin_interfaces/srv/StartSearch_Request";
}

template<>
struct has_fixed_size<bin_interfaces::srv::StartSearch_Request>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<bin_interfaces::srv::StartSearch_Request>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<bin_interfaces::srv::StartSearch_Request>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace bin_interfaces
{

namespace srv
{

inline void to_flow_style_yaml(
  const StartSearch_Response & msg,
  std::ostream & out)
{
  out << "{";
  // member: success
  {
    out << "success: ";
    rosidl_generator_traits::value_to_yaml(msg.success, out);
  }
  out << "}";
}  // NOLINT(readability/fn_size)

inline void to_block_style_yaml(
  const StartSearch_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  // member: success
  {
    if (indentation > 0) {
      out << std::string(indentation, ' ');
    }
    out << "success: ";
    rosidl_generator_traits::value_to_yaml(msg.success, out);
    out << "\n";
  }
}  // NOLINT(readability/fn_size)

inline std::string to_yaml(const StartSearch_Response & msg, bool use_flow_style = false)
{
  std::ostringstream out;
  if (use_flow_style) {
    to_flow_style_yaml(msg, out);
  } else {
    to_block_style_yaml(msg, out);
  }
  return out.str();
}

}  // namespace srv

}  // namespace bin_interfaces

namespace rosidl_generator_traits
{

[[deprecated("use bin_interfaces::srv::to_block_style_yaml() instead")]]
inline void to_yaml(
  const bin_interfaces::srv::StartSearch_Response & msg,
  std::ostream & out, size_t indentation = 0)
{
  bin_interfaces::srv::to_block_style_yaml(msg, out, indentation);
}

[[deprecated("use bin_interfaces::srv::to_yaml() instead")]]
inline std::string to_yaml(const bin_interfaces::srv::StartSearch_Response & msg)
{
  return bin_interfaces::srv::to_yaml(msg);
}

template<>
inline const char * data_type<bin_interfaces::srv::StartSearch_Response>()
{
  return "bin_interfaces::srv::StartSearch_Response";
}

template<>
inline const char * name<bin_interfaces::srv::StartSearch_Response>()
{
  return "bin_interfaces/srv/StartSearch_Response";
}

template<>
struct has_fixed_size<bin_interfaces::srv::StartSearch_Response>
  : std::integral_constant<bool, true> {};

template<>
struct has_bounded_size<bin_interfaces::srv::StartSearch_Response>
  : std::integral_constant<bool, true> {};

template<>
struct is_message<bin_interfaces::srv::StartSearch_Response>
  : std::true_type {};

}  // namespace rosidl_generator_traits

namespace rosidl_generator_traits
{

template<>
inline const char * data_type<bin_interfaces::srv::StartSearch>()
{
  return "bin_interfaces::srv::StartSearch";
}

template<>
inline const char * name<bin_interfaces::srv::StartSearch>()
{
  return "bin_interfaces/srv/StartSearch";
}

template<>
struct has_fixed_size<bin_interfaces::srv::StartSearch>
  : std::integral_constant<
    bool,
    has_fixed_size<bin_interfaces::srv::StartSearch_Request>::value &&
    has_fixed_size<bin_interfaces::srv::StartSearch_Response>::value
  >
{
};

template<>
struct has_bounded_size<bin_interfaces::srv::StartSearch>
  : std::integral_constant<
    bool,
    has_bounded_size<bin_interfaces::srv::StartSearch_Request>::value &&
    has_bounded_size<bin_interfaces::srv::StartSearch_Response>::value
  >
{
};

template<>
struct is_service<bin_interfaces::srv::StartSearch>
  : std::true_type
{
};

template<>
struct is_service_request<bin_interfaces::srv::StartSearch_Request>
  : std::true_type
{
};

template<>
struct is_service_response<bin_interfaces::srv::StartSearch_Response>
  : std::true_type
{
};

}  // namespace rosidl_generator_traits

#endif  // BIN_INTERFACES__SRV__DETAIL__START_SEARCH__TRAITS_HPP_
