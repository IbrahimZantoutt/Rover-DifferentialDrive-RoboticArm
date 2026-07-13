// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from bin_interfaces:srv/StartSearch.idl
// generated code does not contain a copyright notice

#ifndef BIN_INTERFACES__SRV__DETAIL__START_SEARCH__STRUCT_H_
#define BIN_INTERFACES__SRV__DETAIL__START_SEARCH__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in srv/StartSearch in the package bin_interfaces.
typedef struct bin_interfaces__srv__StartSearch_Request
{
  bool start;
} bin_interfaces__srv__StartSearch_Request;

// Struct for a sequence of bin_interfaces__srv__StartSearch_Request.
typedef struct bin_interfaces__srv__StartSearch_Request__Sequence
{
  bin_interfaces__srv__StartSearch_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} bin_interfaces__srv__StartSearch_Request__Sequence;


// Constants defined in the message

/// Struct defined in srv/StartSearch in the package bin_interfaces.
typedef struct bin_interfaces__srv__StartSearch_Response
{
  bool success;
} bin_interfaces__srv__StartSearch_Response;

// Struct for a sequence of bin_interfaces__srv__StartSearch_Response.
typedef struct bin_interfaces__srv__StartSearch_Response__Sequence
{
  bin_interfaces__srv__StartSearch_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} bin_interfaces__srv__StartSearch_Response__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // BIN_INTERFACES__SRV__DETAIL__START_SEARCH__STRUCT_H_
