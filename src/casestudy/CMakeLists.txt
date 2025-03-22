cmake_minimum_required(VERSION 3.5)
project(casestudy)

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
# uncomment the following section in order to fill in
# further dependencies manually.
# find_package(<dependency> REQUIRED)
find_package(priority_executor REQUIRED)
find_package(simple_timer REQUIRED)
find_package(rclcpp REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(casestudy_tools REQUIRED)

# add_library(test_nodes src/test_nodes.cpp)
# target_include_directories(test_nodes PUBLIC
#   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#   $<INSTALL_INTERFACE:include>
# )
# ament_target_dependencies(test_nodes
#   rclcpp
#   simple_timer
#   casestudy_tools
# )


list(APPEND experiments
  casestudy_2023customfile_singlethread
  casestudy_2024ours_latency
  casestudy_2024ours_executor2executor
  casestudy_example
)

function(new_experiment experiment_name)
  add_executable(${experiment_name} src/${experiment_name}.cpp)
  target_include_directories(${experiment_name} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  ament_target_dependencies(${experiment_name}
    rclcpp
    priority_executor
    simple_timer
    casestudy_tools
  )
endfunction()

foreach(experiment ${experiments})
  new_experiment(${experiment})
endforeach()

target_link_libraries(casestudy_2023customfile_singlethread
  jsoncpp
)


install(TARGETS
  ${experiments}
  DESTINATION lib/${PROJECT_NAME}
)


install(
  DIRECTORY include/
  DESTINATION include
)


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

ament_export_include_directories(include)
ament_package()
