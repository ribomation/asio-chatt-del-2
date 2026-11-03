# Gemensamma inställningar för alla demoprogram.
#
# Inkluderas av rotens CMakeLists.txt och av varje demoprograms CMakeLists.txt,
# så att ett demoprogram kan byggas för sig (cmake i dess mapp) eller alla på en
# gång (cmake i projektroten). include_guard() gör att filen bara körs en gång.
include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wpedantic)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Externa bibliotek via de handskrivna find-modulerna i ~/Libs.
list(APPEND CMAKE_MODULE_PATH "$ENV{HOME}/Libs/CMake-modules")

# Boost.Asio är header-only, men behöver pthreads.
find_package(Boost 1.92 REQUIRED)
find_package(Threads REQUIRED)
message(STATUS "Boost ${Boost_VERSION} från ${Boost_ROOT}")

# Ett interface-target som samlar allt ett Asio-program behöver.
add_library(asio_common INTERFACE)
add_library(asio::common ALIAS asio_common)
target_link_libraries(asio_common INTERFACE Boost::headers Threads::Threads)

# Utan denna innehåller system_error::what() sökväg, radnummer och funktionsnamn
# från Asios interna headers. Med den blir felmeddelandet bara "connect: Connection refused".
target_compile_definitions(asio_common INTERFACE BOOST_ASIO_DISABLE_ERROR_LOCATION)
