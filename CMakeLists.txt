cmake_minimum_required(VERSION 3.28)
project(asio-chatt-del-2 LANGUAGES CXX)

# Demoprogram till artikeln
#   "Bygg en chattserver med Boost.Asio, del 2: Asynkront med callbacks"
# Bygg allt härifrån, eller ett program i taget från dess egen mapp.

include(cmake/common.cmake)

add_subdirectory(timer)
add_subdirectory(echoserver)
add_subdirectory(daytime-client)
add_subdirectory(chatserver-one-thread)
add_subdirectory(chatserver-race)
add_subdirectory(chatserver-strands)
