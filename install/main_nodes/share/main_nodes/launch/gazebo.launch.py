import os
import re

import xacro
from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, RegisterEventHandler
from launch.event_handlers import OnProcessExit
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    pkg_share = get_package_share_directory('main_nodes')
    gazebo_ros_share = get_package_share_directory('gazebo_ros')

    urdf_path = os.path.join(pkg_share, 'urdf', 'robot.urdf.xacro')

    # Expand xacro to a string here so we can clean it up before publishing.
    # gazebo_ros2_control forwards robot_description to the controller_manager,
    # and on Humble rcl can't parse the value if it still contains the xacro
    # `<?xml?>` declaration or `<!-- -->` comments -> the controller_manager
    # silently fails to start and the arm collapses. Strip both.
    robot_description_xml = xacro.process_file(urdf_path).toxml()
    robot_description_xml = re.sub(r'<\?xml[^>]*\?>', '', robot_description_xml)
    robot_description_xml = re.sub(r'<!--.*?-->', '', robot_description_xml, flags=re.DOTALL)

    # Everything must share Gazebo's /clock.
    use_sim_time = {'use_sim_time': True}

    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(gazebo_ros_share, 'launch', 'gazebo.launch.py')
        )
    )

    robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        output='screen',
        parameters=[{'robot_description': robot_description_xml}, use_sim_time],
    )

    spawn_entity = Node(
        package='gazebo_ros',
        executable='spawn_entity.py',
        output='screen',
        arguments=[
            '-topic', 'robot_description',
            '-entity', 'mobiarm',
            '-z', '0.1',
        ],
    )

    # Controllers, spawned in order once the robot exists in Gazebo.
    joint_state_broadcaster = Node(
        package='controller_manager',
        executable='spawner',
        arguments=['joint_state_broadcaster', '-c', '/controller_manager'],
    )
    arm_controller = Node(
        package='controller_manager',
        executable='spawner',
        arguments=['arm_controller', '-c', '/controller_manager'],
    )
    gripper_controller = Node(
        package='controller_manager',
        executable='spawner',
        arguments=['gripper_controller', '-c', '/controller_manager'],
    )

    return LaunchDescription([
        gazebo,
        robot_state_publisher,
        spawn_entity,
        # Chain the spawners so each starts only after the previous step is done.
        RegisterEventHandler(
            OnProcessExit(
                target_action=spawn_entity,
                on_exit=[joint_state_broadcaster],
            )
        ),
        RegisterEventHandler(
            OnProcessExit(
                target_action=joint_state_broadcaster,
                on_exit=[arm_controller],
            )
        ),
        RegisterEventHandler(
            OnProcessExit(
                target_action=arm_controller,
                on_exit=[gripper_controller],
            )
        ),
    ])
