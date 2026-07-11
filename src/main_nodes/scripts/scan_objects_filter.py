#!/usr/bin/env python3
"""Publish a "novel objects only" scan: the lidar scan with the KNOWN static
environment (walls + table) removed, so a detection pipeline sees only things
that aren't part of the map. Nav2 keeps using the full /scan; this is a parallel,
more-aggressively-filtered stream on /scan_objects.

  lidar -> /scan_raw -> [self-filter] -> /scan -> [this node] -> /scan_objects
                                          (Nav2/SLAM)            (detection)

Why this differs from the self-filter
-------------------------------------
The self-filter's box is in the LIDAR frame -- correct for the camera stand,
which is bolted to the robot and is always ~(0.40, 0) in lidar_link. The walls
and table are fixed in the WORLD, not to the robot, so their position in the
lidar frame changes as the robot drives; a lidar-frame box would slide off them.

So instead we transform every scan point into a world-fixed frame ('map') via TF,
then test it against boxes defined in that world frame. We look up the lidar's
pose in 'map' once per scan and apply the 2D rotation+translation to each point
(the lidar is level, so a planar transform is exact).

Filtering (all in the 'map' frame)
----------------------------------
  - point at/near the room perimeter (|x| or |y| >= room_half)  -> wall  -> drop
  - point inside the table box                                  -> table -> drop
  - everything else (interior, not on a known object)           -> kept, i.e. a
    real novel/dynamic object placed in the room (e.g. the green cube)

NOTE on frames: the SLAM 'map' frame is anchored at the robot's start pose. Since
the robot spawns at the world origin, map ~= the Gazebo world frame, so the walls
(+/-4 m) and table (x=1) keep their world coordinates here. If you start the robot
somewhere else, offset these bounds by the start pose (or express them in 'map'
after seeing where the map origin lands).

Requires use_sim_time:=true so the TF lookup matches the sim clock.
"""
import math

import rclpy
from rclpy.node import Node
from rclpy.time import Time
from sensor_msgs.msg import LaserScan
from tf2_ros import Buffer, TransformListener, TransformException


class ScanObjectsFilter(Node):
    def __init__(self):
        super().__init__('scan_objects_filter')
        # World frame the boxes are defined in.
        self.target_frame = self.declare_parameter('target_frame', 'map').value
        # Room half-size: any point with |x|>= or |y|>= this (in map) is treated
        # as wall/outside. Walls are at +/-4 m, so 3.8 keeps the interior.
        self.room_half = self.declare_parameter('room_half', 3.8).value
        # Table box in the map frame (table centred at x=1, ~0.8 x 0.8, + margin).
        self.table_min_x = self.declare_parameter('table_min_x', 0.55).value
        self.table_max_x = self.declare_parameter('table_max_x', 1.45).value
        self.table_min_y = self.declare_parameter('table_min_y', -0.45).value
        self.table_max_y = self.declare_parameter('table_max_y', 0.45).value

        self.tf_buffer = Buffer()
        self.tf_listener = TransformListener(self.tf_buffer, self)

        self.pub = self.create_publisher(LaserScan, 'scan_objects', 10)
        self.sub = self.create_subscription(LaserScan, 'scan', self.cb, 10)
        self.get_logger().info(
            f"scan_objects_filter up: removing walls (|x|,|y|>={self.room_half}) "
            f"+ table, in frame '{self.target_frame}'; /scan -> /scan_objects")

    def cb(self, msg: LaserScan):
        # Pose of the lidar in the world frame. Use the LATEST available transform
        # (Time()), NOT msg.header.stamp: slam_toolbox's map->odom lags the newest
        # scan, so an exact-stamp lookup extrapolates into the future and throws
        # (and a blocking timeout can't help -- on a single-threaded executor the
        # TF listener is blocked too). The tiny pose staleness is negligible here.
        try:
            tf = self.tf_buffer.lookup_transform(
                self.target_frame, msg.header.frame_id, Time())
        except TransformException as e:
            self.get_logger().warn(
                f"no TF {self.target_frame} <- {msg.header.frame_id}: {e}",
                throttle_duration_sec=2.0)
            return  # skip this scan; a detection stream can tolerate gaps

        t = tf.transform.translation
        q = tf.transform.rotation
        # yaw of the lidar in the world frame (planar: the lidar is level)
        yaw = math.atan2(2.0 * (q.w * q.z + q.x * q.y),
                         1.0 - 2.0 * (q.y * q.y + q.z * q.z))
        cos_y, sin_y = math.cos(yaw), math.sin(yaw)

        ranges = list(msg.ranges)
        for i, r in enumerate(ranges):
            if not math.isfinite(r):
                continue
            a = msg.angle_min + i * msg.angle_increment
            px = r * math.cos(a)
            py = r * math.sin(a)
            # rotate + translate the point into the world (map) frame
            X = t.x + cos_y * px - sin_y * py
            Y = t.y + sin_y * px + cos_y * py

            is_wall = abs(X) >= self.room_half or abs(Y) >= self.room_half
            is_table = (self.table_min_x <= X <= self.table_max_x and
                        self.table_min_y <= Y <= self.table_max_y)
            if is_wall or is_table:
                ranges[i] = float('nan')  # invalid -> dropped from detection
        msg.ranges = ranges
        self.pub.publish(msg)


def main():
    rclpy.init()
    node = ScanObjectsFilter()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
