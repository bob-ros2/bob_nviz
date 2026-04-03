cmake_minimum_required(VERSION 3.8)
project(bob_nviz)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(nviz
  src/nviz_node.cpp
)

target_include_directories(nviz PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(nviz
  nlohmann_json::nlohmann_json
)

ament_target_dependencies(nviz
  rclcpp
  std_msgs
)

install(TARGETS
  nviz
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # Exclude build and install directories from linting
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  set(ament_cmake_uncrustify_FOUND TRUE)
  list(APPEND AMENT_LINT_AUTO_EXCLUDE
    build
    install
    bin
  )
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
