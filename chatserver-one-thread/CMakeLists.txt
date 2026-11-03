cmake_minimum_required(VERSION 3.28)
project(chatserver-one-thread LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(chatserver-one-thread src/chatserver.cxx)
target_link_libraries(chatserver-one-thread PRIVATE asio::common)
