from launch import LaunchDescription
from launch_ros.actions import Node
from moveit_configs_utils import MoveItConfigsBuilder


def generate_launch_description():
    # Same robot model move_group uses in gazebo.launch.py, so the two agree.
    moveit_config = (
        MoveItConfigsBuilder("mobiarm", package_name="moveit_config")
        .robot_description(mappings={"use_gazebo": "true"})
        .to_moveit_configs()
    )

    # A MoveGroupInterface node builds its own copy of the robot model, so it
    # needs the URDF, SRDF and kinematics on its OWN parameter server, plus
    # sim time so it agrees with Gazebo's clock. Without robot_description_semantic
    # you get "Could not parse the SRDF ... EMPTY_DOCUMENT" and it aborts.
    #
    # This does NOT start move_group / Gazebo / RViz -- run gazebo.launch.py
    # first, then run this in a second terminal.
    travel_pose = Node(
        package="main_nodes",
        executable="TravelPose",
        output="screen",
        parameters=[
            moveit_config.robot_description,
            moveit_config.robot_description_semantic,
            moveit_config.robot_description_kinematics,
            {"use_sim_time": True},
        ],
    )

    return LaunchDescription([travel_pose])
