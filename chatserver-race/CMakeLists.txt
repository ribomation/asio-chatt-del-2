cmake_minimum_required(VERSION 3.28)
project(chatserver-race LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(chatserver-race src/chatserver.cxx)
target_link_libraries(chatserver-race PRIVATE asio::common)
