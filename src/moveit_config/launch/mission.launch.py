# mission.launch.py -- the full detect -> approach -> pick -> dropoff -> place loop.
#
# Run nav2_slam.launch.py FIRST (Gazebo + move_group + Nav2 + SLAM + the scan
# filters); it is unchanged and owns /scan_objects. Then run THIS in a second
# terminal. It brings up perception, the arm service nodes, and the orchestrator:
#
#   VisionNode      green-cube localizer: get_targets service + /targets
#   detectionNode   lidar -> nearest cube -> Nav2, gated by start_search
#   TravelPose      travel_pose service (fold the arm)
#   ArmPickup       pickup + place services
#   ArmManager      the orchestrator (state machine) -- started last, on a delay
#
# The scan filters (scan_self_filter / scan_objects_filter) are NOT started here;
# they come from nav2_slam.launch.py.

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from moveit_configs_utils import MoveItConfigsBuilder


def generate_launch_description():
    # Same robot model move_group uses in gazebo.launch.py, so the two agree.
    # MoveGroupInterface clients (TravelPose, ArmPickup) each build their own copy
    # of the model, so they need the URDF/SRDF/kinematics on their OWN parameter
    # server -- without robot_description_semantic they abort on an empty SRDF.
    moveit_config = (
        MoveItConfigsBuilder("mobiarm", package_name="moveit_config")
        .robot_description(mappings={"use_gazebo": "true"})
        .to_moveit_configs()
    )

    sim_time = {"use_sim_time": True}
    moveit_params = [
        moveit_config.robot_description,
        moveit_config.robot_description_semantic,
        moveit_config.robot_description_kinematics,
        sim_time,
    ]

    # --- dropoff pose (map frame) -- MUST be tuned to sit the base in front of the
    # table so ArmPickup's place() pose can reach it. Override on the CLI, e.g.
    #   ros2 launch moveit_config mission.launch.py dropoff_x:=2.0 dropoff_yaw:=0.0
    dropoff_x_arg = DeclareLaunchArgument("dropoff_x", default_value="1.8")
    dropoff_y_arg = DeclareLaunchArgument("dropoff_y", default_value="0.0")
    dropoff_yaw_arg = DeclareLaunchArgument("dropoff_yaw", default_value="0.0")

    vision = Node(
        package="main_nodes", executable="VisionNode",
        output="screen", parameters=[sim_time],
    )

    detection = Node(
        package="main_nodes", executable="detectionNode",
        output="screen", parameters=[sim_time],
    )

    travel_pose = Node(
        package="main_nodes", executable="TravelPose",
        output="screen", parameters=moveit_params,
    )

    arm_pickup = Node(
        package="main_nodes", executable="ArmPickup",
        output="screen", parameters=moveit_params,
    )

    arm_manager = Node(
        package="main_nodes", executable="ArmManager",
        output="screen",
        parameters=[sim_time, {
            "dropoff_x": ParameterValue(LaunchConfiguration("dropoff_x"), value_type=float),
            "dropoff_y": ParameterValue(LaunchConfiguration("dropoff_y"), value_type=float),
            "dropoff_yaw": ParameterValue(LaunchConfiguration("dropoff_yaw"), value_type=float),
        }],
    )

    # Start the orchestrator last, on a delay: it aborts if travel_pose isn't up
    # within a few seconds, and the arm service nodes need time to connect to
    # move_group first. Bump this if move_group is slow to come up.
    arm_manager_delayed = TimerAction(period=10.0, actions=[arm_manager])

    return LaunchDescription([
        dropoff_x_arg, dropoff_y_arg, dropoff_yaw_arg,
        vision, detection, travel_pose, arm_pickup, arm_manager_delayed,
    ])
