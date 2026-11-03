cmake_minimum_required(VERSION 3.28)
project(chatserver-strands LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(chatserver-strands src/chatserver.cxx)
target_link_libraries(chatserver-strands PRIVATE asio::common)
