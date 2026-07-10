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

add_executable(RunNode src/RunNode.cpp)
target_include_directories(RunNode PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(RunNode PUBLIC c_std_99 cxx_std_17)  # Require C99 and C++17

add_executable(TravelPose src/TravelPose.cpp)
ament_target_dependencies(TravelPose rclcpp moveit_ros_planning_interface)
install(TARGETS TravelPose DESTINATION lib/${PROJECT_NAME})

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

# Python self-filter node (removes lidar returns that hit the robot's own body)
install(PROGRAMS scripts/scan_self_filter.py
  DESTINATION lib/${PROJECT_NAME})

ament_package()
