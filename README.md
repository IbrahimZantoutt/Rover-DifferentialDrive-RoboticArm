<div align="center">

# MobiArm — Mobile Differential Drive Mounted 6-DOF Arm

**An autonomous mobile manipulator in ROS 2 & Gazebo: it explores an unmapped room, finds green cubes on its own, drives to them, picks them up, and carries them to a drop-off table — start to finish, with nobody touching the keyboard.**

You launch two files and walk away. slam_toolbox maps the room live, a filtered lidar stream picks out the objects that *aren't* part of the world, a depth camera localizes the cube in 3D, MoveIt plans the grasp, and a C++ state machine drives the whole detect → approach → pick → carry → place loop until the room is empty.

![ROS 2](https://img.shields.io/badge/ROS_2-Humble-22314E?logo=ros&logoColor=white)
![Gazebo](https://img.shields.io/badge/Gazebo-Classic-FF6C2C?logo=gazebo&logoColor=white)
![MoveIt 2](https://img.shields.io/badge/MoveIt_2-Manipulation-0A7BBB)
![Nav2](https://img.shields.io/badge/Nav2-Navigation-2E7D32)
![slam_toolbox](https://img.shields.io/badge/slam__toolbox-Mapping-1565C0)
![OpenCV](https://img.shields.io/badge/OpenCV-Perception-5C3EE8?logo=opencv&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)

</div>

---

## Demo

<div align="center">

### The full mission — search, approach, pick, carry, place, repeat

<!-- Drag-and-drop your horizontal full-process GIF into GitHub's editor here and replace the src below. -->

<img width="90%" alt="MobiArm running the full autonomous pick-and-place mission in Gazebo" src="https://github.com/user-attachments/assets/8c89f1fc-3266-469f-a5a3-b6f9d4576a41" />

</div>

<table>
<tr>
<td width="50%" align="center">

### The robot model (URDF)

<!-- Drag-and-drop a screenshot of the full robot in RViz (RobotModel display) here. -->
<img width="100%" alt="MobiArm URDF: 4-wheel differential base, 6-DOF arm, lidar and depth camera" src="https://github.com/user-attachments/assets/cf1dbb5f-381e-41ab-882d-ef488186bfaa" />

</td>
<td width="50%" align="center">

### Live SLAM map — bird's-eye view
<!-- Drag-and-drop the RViz bird's-eye map GIF here. -->
<img width="100%" alt="slam_toolbox building the map in RViz while the robot works" src="https://github.com/user-attachments/assets/6f992512-db09-4305-9865-783b3122afcb" />

</td>
</tr>
</table>

---

## Highlights

- **Nobody clicks a goal.** The `ArmManager` state machine owns the whole mission. It decides when to look, when to drive, when to grasp, and when the room is empty and the job is done.
- **It knows what's *new*.** A two-stage lidar filter subtracts the robot's own body and then the known walls and table, leaving a `/scan_objects` stream that contains only things that don't belong to the world. The nearest return in it *is* the next cube.
- **Sees in 3D.** A depth camera + OpenCV HSV segmentation finds the green cube, deprojects its centroid through the pinhole model, and hands MoveIt a grasp point in the arm's own frame.
- **No pre-built map.** slam_toolbox maps the room online while the robot works. No AMCL, no `map_server` — SLAM alone provides `/map` and `map→odom`.
- **The arm folds before every drive.** A raised arm shows up in the object scan as the closest "novel object" and the robot would chase itself. Folding first is a correctness requirement, not a nicety.
- **Grasping that actually holds.** The IFRA LinkAttacher plugin welds cube to gripper in Gazebo, sidestepping the friction-based grasp instability that plagues simulated pick-and-place.

---

## How It Works

```
                       ArmManager  (the orchestrator / state machine)
                             │
        ┌────────────────────┼────────────────────┬──────────────────┐
        │                    │                    │                  │
   travel_pose          start_search           pickup             place
   (fold arm)          (arm detection)       (grasp cube)      (set it down)
        │                    │                    │                  │
        ▼                    ▼                    ▼                  ▼
   TravelPose          detectionNode           ArmPickup ◄──── get_targets ──── VisionNode
   MoveGroup("arm")    nearest return in       MoveGroup("arm")                 depth cam + OpenCV
                       /scan_objects           + LinkAttacher weld              HSV → centroid →
                       → Nav2 standoff goal                                     deproject → arm frame
                             │
                             ├── /object_found    (pulsed: committed to a cube)
                             └── /object_reached  (true = arrived, false = nav failed)


  Lidar  ──► /scan_raw ──► [scan_self_filter] ──► /scan ──────► slam_toolbox ──► /map + map→odom
                            drops the robot's      │                                    │
                            own camera stand       │                                    ▼
                                                   ├──────────────────────────────►  Nav2
                                                   │                            (RPP · NavFn ·
                                                   ▼                             costmaps · BT)
                                          [scan_objects_filter]                         │
                                          drops walls + table                      /cmd_vel
                                                   │                                    ▼
                                                   ▼                          Gazebo (diff_drive)
                                            /scan_objects  ──► detectionNode
```

**1. Fold —** `ArmManager` calls `travel_pose` first. Every base movement happens with the arm tucked, so the arm can never be mistaken for a target and never clips the world while driving.

**2. Search —** `start_search` arms `detectionNode`. It watches `/scan_objects` — the stream with the robot, walls and table already subtracted — takes the closest finite return as the nearest cube, and sends Nav2 a standoff pose along that bearing with the camera facing the object.

**3. Approach —** Two signals come back, and the split matters: `/object_found` pulses the instant detection commits to a cube (so "no cubes left" is distinguishable from "still driving"), then `/object_reached` reports the outcome. If Nav2 *aborts* but the base is within `arrival_tolerance` of the goal, that counts as arrived — Nav2 routinely aborts the last stretch when the goal sits inside the costmap inflation.

**4. Pick —** `ArmPickup` snapshots the target from `VisionNode` via the `get_targets` service, welds the base to the ground (so the arm's reaction torque can't shove the robot), plans a top-down grasp, and swaps the ground weld for a cube→gripper weld.

**5. Carry & place —** Fold again, drive to a **staging waypoint** and only then to the drop-off, so the final leg always starts from the same place. Then `place` runs a named MoveIt pose and releases. Back to step 1 — until a search turns up nothing, which ends the mission.

---

## Tech Stack

| Layer | Tools / Libraries |
|---|---|
| **Middleware** | ROS 2 **Humble** (`rclcpp`, `rclcpp_action`, launch) |
| **Manipulation** | **MoveIt 2** (`moveit_ros_planning_interface`, OMPL, `MoveGroupInterface`) |
| **Navigation** | **Nav2** (Regulated Pure Pursuit, NavFn, `costmap_2d`, `bt_navigator`, behaviors) |
| **Mapping** | **slam_toolbox** (online async SLAM — no AMCL / map_server) |
| **Perception** | **OpenCV** (HSV segmentation), `cv_bridge`, `image_geometry` (pinhole deprojection) |
| **Simulation** | **Gazebo Classic** (`gazebo_ros`, `gazebo_ros2_control`, diff-drive, ray + depth camera) |
| **Joint control** | **ros2_control** (`joint_trajectory_controller` for arm + gripper) |
| **Grasping** | **IFRA LinkAttacher** (vendored) — Gazebo link attach/detach services |
| **Transforms** | `tf2`, `tf2_ros`, `tf2_geometry_msgs` |
| **Robot model** | URDF / Xacro — 4-wheel differential base + 6-DOF arm + 2-finger gripper |
| **Custom interfaces** | `bin_interfaces` — `GetTargets.srv`, `StartSearch.srv` |
| **Language / build** | C++17 (all mission nodes), Python (scan filters, launch), `colcon`, `ament_cmake` |

Vendored: [`IFRA-Cranfield/IFRA_LinkAttacher`](https://github.com/IFRA-Cranfield/IFRA_LinkAttacher) (Apache-2.0).

---

## The Robot

| | |
|---|---|
| **Base** | 4-wheel skid-steer differential drive, 0.85 × 0.55 × 0.13 m, 13 kg, COM lowered to resist tipping |
| **Arm** | 6-DOF (`j1_waist` → `j6_wrist`), MoveIt group `arm`, mounted on top of the lidar |
| **Gripper** | 2-finger parallel, MoveIt group `gripper`, end effector `gripper_ee` |
| **Lidar** | 360-sample planar scan, 0.15–12 m, scan plane ~2 cm above the base top |
| **Camera** | Depth camera on a forward stand at x = 0.4 m, 60° FOV, 640×480, 0.05–10 m |
| **World** | 8 × 8 m walled room; drop-off table at x = 2.5 with a bin on top; two green cubes on stands at (−1.4, 1.8) and (−2.5, −1.2) |

---

## Repository Layout

```
src/
├── main_nodes/              # robot description + every mission node
│   ├── src/
│   │   ├── ArmManager.cpp       # ⭐ the orchestrator: state machine, owns the mission
│   │   ├── detectionNode.cpp    # /scan_objects → nearest cube → Nav2 standoff goal
│   │   ├── VisionNode.cpp       # depth cam + OpenCV → cube point in arm_base_link
│   │   ├── ArmPickup.cpp        # pickup + place services (MoveIt + LinkAttacher)
│   │   └── TravelPose.cpp       # travel_pose service (fold the arm)
│   ├── scripts/
│   │   ├── scan_self_filter.py     # /scan_raw → /scan   (drop the robot's own stand)
│   │   └── scan_objects_filter.py  # /scan → /scan_objects (drop walls + table)
│   ├── urdf/                # base · arm · robot (top-level xacro)
│   ├── worlds/world.sdf     # room, table, bin, cube stands, green cubes
│   ├── config/              # nav2_params.yaml, slam_toolbox.yaml
│   └── rviz/                # model.rviz, nav2_view.rviz
├── moveit_config/           # MoveIt 2 config + the launch files that tie it together
│   ├── config/              # mobiarm.srdf (groups, named poses, collision matrix),
│   │                        # kinematics, joint_limits, ros2_controllers, urdf xacro
│   └── launch/
│       ├── nav2_slam.launch.py  # ⭐ sim + MoveIt + SLAM + Nav2 + scan filters
│       ├── mission.launch.py    # ⭐ perception + arm services + orchestrator
│       ├── gazebo.launch.py     #   Gazebo + robot + controllers + move_group
│       └── demo.launch.py       #   MoveIt demo (mock hardware, no Gazebo)
├── bin_interfaces/          # GetTargets.srv · StartSearch.srv
└── IFRA_LinkAttacher/       # vendored Gazebo link attacher (grasp simulation)
```

---

## Getting Started

### Prerequisites
- Ubuntu 22.04 + **ROS 2 Humble**
- Gazebo Classic, MoveIt 2, Nav2, slam_toolbox, `ros2_control`

```bash
sudo apt install ros-humble-gazebo-ros-pkgs ros-humble-gazebo-ros2-control \
  ros-humble-ros2-control ros-humble-ros2-controllers \
  ros-humble-moveit ros-humble-nav2-bringup ros-humble-slam-toolbox \
  ros-humble-cv-bridge ros-humble-image-geometry
```

### Build

```bash
git clone https://github.com/IbrahimZantoutt/Rover-DifferentialDrive-RoboticArm.git
cd Rover-DifferentialDrive-RoboticArm
colcon build
source install/setup.bash
```

### Run

The mission runs in **two terminals**. The first owns the simulation and the navigation stack; the second owns perception and the mission itself. Wait for the first to settle before starting the second.

```bash
# Terminal 1 — Gazebo + MoveIt + SLAM + Nav2 + the scan filters
ros2 launch moveit_config nav2_slam.launch.py
```

```bash
# Terminal 2 — vision, detection, arm services, and the orchestrator
source install/setup.bash
ros2 launch moveit_config mission.launch.py
```

That's it — the robot folds its arm, starts hunting for cubes, and works until the room is clear. `ArmManager` shuts itself down when a search comes up empty.

### Watching it work

Both RViz windows are **off by default**: they are a heavy constant GUI draw that starves the Nav2 control loop and leaves the robot crawling. Turn them on (plus the HSV/mask vision windows) when you want to see the internals:

```bash
ros2 launch moveit_config nav2_slam.launch.py debug_mode:=true
ros2 launch moveit_config mission.launch.py debug_mode:=true
```

### Tuning the drop-off

The drop-off pose is a launch argument, so you can retarget it without touching code:

```bash
ros2 launch moveit_config mission.launch.py dropoff_x:=2.0 dropoff_y:=0.0 dropoff_yaw:=0.0
```

---

## Challenges & Lessons Learned

The genuinely hard parts, kept here so they don't have to be re-derived.

### Making the robot not chase itself
- **The lidar was blind.** `range_min` (0.15) exactly equalled the lidar housing's own collision radius, so *every* beam clipped on its own housing and returned `inf`. Fix: the lidar link carries no collision at all — it's a rigid fixed connector that never needs one.
- **The camera stand is a real obstacle to a real lidar.** Rather than cheat by deleting its collision, `scan_self_filter` nulls beams landing in a box around the stand — the same thing you'd have to do on physical hardware. The box stays *inside* the robot footprint, so it can only ever remove the robot's own hardware, never a genuine obstacle.
- **Self-filter and world-filter can't share a frame.** The stand is bolted to the robot, so its box is fixed in the *lidar* frame. Walls and the table are fixed in the *world*, so a lidar-frame box would slide off them as the robot drives — `scan_objects_filter` transforms every point into `map` first and tests it there.
- **A raised arm is the closest novel object.** It reads as a cube and false-triggers the mission, which is why folding is step one of every loop iteration.

### Gazebo + MoveIt integration
- **The MoveIt Setup Assistant silently breaks Gazebo.** Saving regenerates `mobiarm.urdf.xacro` and `mobiarm.ros2_control.xacro` from scratch, deleting the hand-added `use_gazebo` arg, the `libgazebo_ros2_control.so` plugin block, and the `GazeboSystem` hardware branch (swapping in `mock_components/GenericSystem`). The result: no `controller_manager`, unactuated joints, **and the arm collapses under gravity on launch.** After any regen, restore both files and keep only the SRDF:
  ```bash
  git checkout HEAD -- src/moveit_config/config/mobiarm.urdf.xacro \
                       src/moveit_config/config/mobiarm.ros2_control.xacro
  # verify: must print GazeboSystem, not mock_components
  xacro src/moveit_config/config/mobiarm.urdf.xacro use_gazebo:=true | grep gazebo_ros2_control
  ```
- **An XML comment collapsed the arm.** `gazebo_ros2_control` forwards `robot_description` to the `controller_manager` as a `--param` override, and on Humble `rcl` cannot parse that value if it contains the xacro `<?xml?>` declaration or a `<!-- -->` banner. The controller manager fails *silently* and the arm collapses. `gazebo.launch.py` strips both before publishing.
- **Everyone must share `/clock`.** Mixing wall clock (MoveIt) and sim clock (controller_manager) makes them disagree on trajectory timestamps, and execution dies with `controller failed during execution`.
- **MoveGroup inside a service callback deadlocks.** `plan()`/`execute()` block waiting on move_group's action responses, which a single-threaded executor can never deliver — it's blocked in the callback. Every arm node uses a `MultiThreadedExecutor` + a dedicated callback group.

### Grasping
- **LinkAttacher allows one active weld at a time.** The base→ground weld (which stops the arm's reaction torque shoving the robot mid-grasp) has to be dropped *before* the cube→gripper weld is made.

### Navigation
- **Skid-steer needs to be allowed to scrub.** With `mu2 = 1` the wheels were pinned laterally and the base achieved ~11% of commanded rotation. Dropping to `mu2 = 0.7` (with `fdir1` pinning axis 1 to the rolling direction) lets it actually yaw.
- **Nav2 aborts the last stretch on purpose.** The cube goal sits inside the costmap inflation, so approaches abort even though the base is right there. `detectionNode` accepts an abort as success when the base ended within `arrival_tolerance` — and on a *genuine* failure it stays committed rather than recomputing from a drifted pose.
- **Regulated Pure Pursuit ignores your goal yaw.** RPP is a path *follower*: it stops inside `xy_goal_tolerance` keeping whatever heading it arrived with, so the drop-off yaw drifted with wherever the cube happened to be. Tightening `yaw_goal_tolerance` doesn't help (RPP just hovers or aborts). Fix: a **staging waypoint** before the final leg, so the approach direction — and therefore the final heading — is always identical.
- **Goal tolerance wants to be different per leg.** `ArmManager` swaps `xy_goal_tolerance` at runtime through the controller's parameter server: tight (0.2) for the cube approach so the cube lands in arm reach, loose (0.4) for the drop-off so the robot stops on arrival instead of fidgeting.
- **A timeout that timed the wrong thing.** A single 20 s search timeout covered the *whole drive*, so a ~20.7 s approach tripped it 0.7 s before arrival and quit before the pickup ever ran. Splitting it into `/object_found` (is there a cube?) and `/object_reached` (did we get there?) fixed it.

---

## Key Tuning Knobs

| File | Knob | Note |
|---|---|---|
| `main_nodes/config/nav2_params.yaml` | `desired_linear_vel`, `inflation_radius`, `xy_goal_tolerance` | navigation feel |
| `main_nodes/config/slam_toolbox.yaml` | `resolution`, `max_laser_range`, `minimum_travel_distance` | mapping |
| `main_nodes/scripts/scan_objects_filter.py` | `room_half`, `table_min/max_x/y` | world keep-out boxes |
| `main_nodes/scripts/scan_self_filter.py` | `min/max_x`, `min/max_y` | self keep-out box (lidar frame) |
| `main_nodes/src/detectionNode.cpp` | `standoff`, `arrival_tolerance` | how close the base parks to a cube |
| `main_nodes/src/ArmManager.cpp` | `dropoff_*`, `dropoff_via_*`, `*_timeout_s`, `max_failures` | mission behaviour |
| `main_nodes/src/VisionNode.cpp` | HSV `inRange` bounds, min blob area | what counts as a cube |
| `moveit_config/config/mobiarm.srdf` | `placepose2` and other named poses | where the cube gets set down |

---

## Known Limitations

- **Grasp welds by call order, not by cube.** `ArmPickup` attaches `green_cube_<N>` with an incrementing counter, but cubes are picked by *proximity* — so with several cubes it can weld the wrong Gazebo model. Needs a position → model-name lookup.
- **The vision target never expires.** `have_target_` latches true and the cache is never invalidated, so `get_targets` can hand back a stale point from a previous cube. Should be aged out or rejected past a threshold.
- **Keep-out boxes are hardcoded to the world.** `scan_objects_filter` assumes the robot spawns at the origin (so `map` ≈ the Gazebo world frame). Spawn elsewhere and the wall/table bounds need offsetting by the start pose.
- **The drop-off is a fixed pose**, not a perceived one — retarget `dropoff_x/y/yaw` if you move the table.
- **RViz is off by default** because it starves the Nav2 control loop; `debug_mode:=true` costs you navigation speed.

---

<div align="center">

*Built by [Ibrahim Zantout](https://github.com/IbrahimZantoutt) — autonomous mobile manipulation, SLAM & motion planning in ROS 2.*

</div>
