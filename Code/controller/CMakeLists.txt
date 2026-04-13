cmake_minimum_required(VERSION 3.8)
project(controller_node)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)

add_executable(controller_node src/controller_node.cpp)
ament_target_dependencies(controller_node rclcpp sensor_msgs geometry_msgs)

install(TARGETS
  controller_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()