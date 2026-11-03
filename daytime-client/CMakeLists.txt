cmake_minimum_required(VERSION 3.28)
project(daytime-client LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(daytime-client src/daytime-client.cxx)
target_link_libraries(daytime-client PRIVATE asio::common)
