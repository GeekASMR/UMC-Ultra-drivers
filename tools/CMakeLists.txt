cmake_minimum_required(VERSION 3.15)
project(asio_switch_test LANGUAGES CXX)

add_executable(asio_switch_test asio_switch_test.cpp)
target_link_libraries(asio_switch_test PRIVATE ole32)
set_target_properties(asio_switch_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
