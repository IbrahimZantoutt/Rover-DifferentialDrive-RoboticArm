#!/usr/bin/env python3
"""Remove laser returns that land on the robot's own body (the front camera
stand), so SLAM/Nav2 don't map the robot's own hardware as an obstacle.

This is the real-hardware equivalent of "making the lidar ignore part of the
robot": on a physical robot you cannot delete the stand's collision, so you
filter the scan in software. Subscribes to /scan_raw, nulls out any beam whose
(x, y) in the lidar frame falls inside a configured box around the stand, and
republishes the cleaned scan on /scan.

It is functionally the same as laser_filters' LaserScanBoxFilter, packaged as a
small in-repo node so there is no extra system dependency. To switch to the
standard package instead: `sudo apt install ros-humble-laser-filters` and run
its scan_to_scan_filter_chain with a box filter using the same bounds.

Box bounds are in the LIDAR (scan) frame. The camera stand sits at ~(0.40, 0.0)
in lidar_link (stand at robot_base x=0.40; lidar_link shares the base's x,y),
radius ~0.025 m. The default box is kept inside the robot footprint (front at
x=0.45) so it can only ever remove the robot's own hardware, never a real
obstacle -- a real object can't be inside the footprint.
"""
import math

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import LaserScan


class ScanSelfFilter(Node):
    def __init__(self):
        super().__init__('scan_self_filter')
        # Box in the lidar frame (metres); beams inside are dropped.
        self.min_x = self.declare_parameter('min_x', 0.35).value
        self.max_x = self.declare_parameter('max_x', 0.45).value
        self.min_y = self.declare_parameter('min_y', -0.08).value
        self.max_y = self.declare_parameter('max_y', 0.08).value

        self.pub = self.create_publisher(LaserScan, 'scan', 10)
        self.sub = self.create_subscription(LaserScan, 'scan_raw', self.cb, 10)
        self.get_logger().info(
            'scan_self_filter up: dropping beams inside '
            f'x[{self.min_x}, {self.max_x}] y[{self.min_y}, {self.max_y}] '
            '(lidar frame); /scan_raw -> /scan')

    def cb(self, msg: LaserScan):
        ranges = list(msg.ranges)
        removed = 0
        for i, r in enumerate(ranges):
            if not math.isfinite(r):
                continue
            a = msg.angle_min + i * msg.angle_increment
            x = r * math.cos(a)
            y = r * math.sin(a)
            if self.min_x <= x <= self.max_x and self.min_y <= y <= self.max_y:
                ranges[i] = float('nan')  # invalid -> ignored by SLAM/costmaps
                removed += 1
        msg.ranges = ranges
        self.pub.publish(msg)

    # (no throttled logging to keep the node tiny; check /scan vs /scan_raw)


def main():
    rclpy.init()
    node = ScanSelfFilter()
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
