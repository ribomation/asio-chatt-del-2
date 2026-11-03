cmake_minimum_required(VERSION 3.28)
project(echoserver LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(echoserver src/echoserver.cxx)
target_link_libraries(echoserver PRIVATE asio::common)
