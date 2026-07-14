import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, TimerAction
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    """Bring up the full MobiArm sim + SLAM (slam_toolbox) + Nav2.

    Pipeline:
      gazebo.launch.py -> robot (base + arm + lidar) in world.sdf, diff_drive
                          (/odom, /cmd_vel, odom->robot_base TF), lidar (/scan),
                          arm controllers + MoveIt.
      slam_toolbox     -> builds the map, publishes map->odom.
      nav2             -> controller / planner / behaviors / bt_navigator, driving
                          /cmd_vel from the costmaps built off /scan.
    """
    main_nodes_share = get_package_share_directory("main_nodes")
    moveit_config_share = get_package_share_directory("moveit_config")
    nav2_bringup_share = get_package_share_directory("nav2_bringup")

    slam_params = os.path.join(main_nodes_share, "config", "slam_toolbox.yaml")
    nav2_params = os.path.join(main_nodes_share, "config", "nav2_params.yaml")
    # Project copy of nav2_default_view.rviz with an extra (disabled-by-default)
    # LaserScan display on /scan_objects -- tick "Scan Objects" in RViz to show it.
    nav2_rviz_config = os.path.join(
        main_nodes_share, "rviz", "nav2_view.rviz"
    )

    use_sim_time = {"use_sim_time": True}

    # Both RViz windows are OFF unless debug_mode:=true -- they are a large GUI/CPU
    # draw that starves the Nav2 control loop (the robot ends up crawling).
    debug_mode = LaunchConfiguration("debug_mode")
    debug_mode_arg = DeclareLaunchArgument("debug_mode", default_value="false")

    # --- Full Gazebo sim (robot + world + arm controllers + MoveIt + RViz) ------
    # This is your existing launch; it spawns robot.urdf.xacro (base + lidar +
    # arm), so /scan, /odom and /cmd_vel are all available for Nav2/SLAM.
    gazebo_sim = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(moveit_config_share, "launch", "gazebo.launch.py")
        ),
        launch_arguments={"debug_mode": debug_mode}.items(),
    )

    # --- Self-filter: strip lidar returns that hit the robot's own camera stand -
    # The raw Gazebo scan is on /scan_raw; this republishes a cleaned /scan (the
    # topic SLAM/Nav2 consume). Same idea as laser_filters' box filter, but as an
    # in-repo node so there's no extra apt dependency. Ports directly to hardware.
    scan_filter = Node(
        package="main_nodes",
        executable="scan_self_filter.py",
        name="scan_self_filter",
        output="screen",
        parameters=[use_sim_time],
    )

    scan_objects = Node(
        package="main_nodes",
        executable="scan_objects_filter.py",
        name="scan_objects_filter",
        output="screen",
        parameters=[use_sim_time],
    )

    # --- SLAM: slam_toolbox online async (publishes /map + map->odom) -----------
    slam = Node(
        package="slam_toolbox",
        executable="async_slam_toolbox_node",
        name="slam_toolbox",
        output="screen",
        parameters=[slam_params, use_sim_time],
    )

    # --- Nav2 navigation servers ------------------------------------------------
    # navigation_launch.py starts controller/planner/smoother/behavior/bt_navigator
    # /waypoint_follower/velocity_smoother + their lifecycle manager. It does NOT
    # start amcl/map_server (SLAM provides the map + map->odom instead).
    nav2 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(nav2_bringup_share, "launch", "navigation_launch.py")
        ),
        launch_arguments={
            "use_sim_time": "true",
            "params_file": nav2_params,
            "autostart": "true",
        }.items(),
    )

    # --- Dedicated Nav2 RViz (Map + Global/Local Costmap + path + Nav2 Goal) ----
    # The included gazebo.launch.py already opens a MoveIt RViz; this is a SECOND
    # window preconfigured for navigation, so you can see what the lidar detects
    # and what the costmaps mark. Uses -f map as its fixed frame.
    nav2_rviz = Node(
        package="rviz2",
        executable="rviz2",
        name="nav2_rviz",
        output="screen",
        condition=IfCondition(debug_mode),
        arguments=["-d", nav2_rviz_config],
        parameters=[use_sim_time],
    )

    # Let Gazebo spawn the robot and start /clock + the diff_drive (odom->robot_base)
    # and lidar (/scan) before SLAM/Nav2 come up, so their TF/scan lookups succeed.
    slam_and_nav2 = TimerAction(period=8.0, actions=[slam, nav2, nav2_rviz])

    # The filter can start immediately -- it just waits for /scan_raw and must be
    # up before SLAM/Nav2 subscribe to /scan.
    return LaunchDescription(
        [debug_mode_arg, gazebo_sim, scan_filter, slam_and_nav2, scan_objects]
    )
