cmake_minimum_required(VERSION 3.8)
project(core)

# Warnungen aktivieren
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

# Abhängigkeiten
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)

# Executable erstellen
add_executable(core_node src/core_node.cpp)

# Abhängigkeiten mitgeben
ament_target_dependencies(core_node
  rclcpp
  sensor_msgs
  geometry_msgs
)

# Installationspfad für ros2 run
install(TARGETS
  core_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
