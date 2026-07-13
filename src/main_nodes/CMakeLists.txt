cmake_minimum_required(VERSION 3.8)
project(main_nodes)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
# uncomment the following section in order to fill in
# further dependencies manually.
# find_package(<dependency> REQUIRED)
find_package(rclcpp REQUIRED)
find_package(moveit_ros_planning_interface REQUIRED)
# detectionNode deps (Nav2 action client + TF)
find_package(rclcpp_action REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
# VisionNode deps (depth camera -> OpenCV -> GetTargets service)
find_package(cv_bridge REQUIRED)
find_package(OpenCV REQUIRED)
find_package(image_geometry REQUIRED)
find_package(bin_interfaces REQUIRED)
# ArmPickup deps (MoveIt pick/place + LinkAttacher grasp services)
find_package(moveit_msgs REQUIRED)
find_package(linkattacher_msgs REQUIRED)

add_executable(RunNode src/RunNode.cpp)
target_include_directories(RunNode PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(RunNode PUBLIC c_std_99 cxx_std_17)  # Require C99 and C++17

add_executable(TravelPose src/TravelPose.cpp)
ament_target_dependencies(TravelPose rclcpp moveit_ros_planning_interface std_srvs)
install(TARGETS TravelPose DESTINATION lib/${PROJECT_NAME})

add_executable(detectionNode src/detectionNode.cpp)
ament_target_dependencies(detectionNode
  rclcpp rclcpp_action sensor_msgs geometry_msgs nav2_msgs std_msgs
  tf2 tf2_ros tf2_geometry_msgs bin_interfaces)
install(TARGETS detectionNode DESTINATION lib/${PROJECT_NAME})

add_executable(VisionNode src/VisionNode.cpp)
target_compile_features(VisionNode PUBLIC c_std_99 cxx_std_17)  # Require C99 and C++17
ament_target_dependencies(VisionNode
  rclcpp
  cv_bridge
  sensor_msgs
  OpenCV
  image_geometry
  geometry_msgs
  tf2_ros
  tf2_geometry_msgs
  bin_interfaces)
install(TARGETS VisionNode DESTINATION lib/${PROJECT_NAME})

add_executable(ArmPickup src/ArmPickup.cpp)
ament_target_dependencies(ArmPickup
  rclcpp
  moveit_ros_planning_interface
  geometry_msgs
  tf2
  moveit_msgs
  linkattacher_msgs
  std_srvs
  bin_interfaces)
install(TARGETS ArmPickup DESTINATION lib/${PROJECT_NAME})

add_executable(ArmManager src/ArmManager.cpp)
ament_target_dependencies(ArmManager
  rclcpp
  rclcpp_action
  std_srvs
  std_msgs
  geometry_msgs
  nav2_msgs
  tf2
  bin_interfaces)
install(TARGETS ArmManager DESTINATION lib/${PROJECT_NAME})

install(TARGETS RunNode
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

install(DIRECTORY launch urdf rviz worlds config
  DESTINATION share/${PROJECT_NAME})

# Python scan filter nodes (self-hits, and known-static-object removal)
install(PROGRAMS
  scripts/scan_self_filter.py
  scripts/scan_objects_filter.py
  DESTINATION lib/${PROJECT_NAME})

ament_package()
