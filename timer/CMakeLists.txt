cmake_minimum_required(VERSION 3.28)
project(timer LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(timer src/timer.cxx)
target_link_libraries(timer PRIVATE asio::common)
