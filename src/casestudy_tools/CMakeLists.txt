cmake_minimum_required(VERSION 3.5)
project(casestudy_tools)

# Default to C99
if(NOT CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 99)
endif()

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(priority_executor REQUIRED)
find_package(simple_timer REQUIRED)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # uncomment the line when a copyright and license is not present in all source files
  #set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # uncomment the line when this package is not in a git repo
  #set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

add_library(primes_workload src/primes_workload.cpp)
target_include_directories(primes_workload PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(primes_workload
  simple_timer
  rclcpp
  priority_executor

)

add_library(test_nodes src/test_nodes.cpp)
target_include_directories(test_nodes PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(test_nodes primes_workload)
ament_target_dependencies(test_nodes
  rclcpp
  simple_timer
)

add_library(experiment_host src/experiment.cpp)
target_include_directories(experiment_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(experiment_host
  primes_workload
  test_nodes
)
ament_target_dependencies(experiment_host
  rclcpp
  simple_timer
  priority_executor
)


install(
  DIRECTORY include/
  DESTINATION include
)

install(
  TARGETS primes_workload test_nodes experiment_host
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

ament_export_include_directories(include)
# ament_export_libraries(primes_workload test_nodes experiment_host)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp priority_executor simple_timer)
ament_package()
