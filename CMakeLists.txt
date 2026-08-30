cmake_minimum_required(VERSION 3.16)
project(firelink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

file(GLOB_RECURSE FIRELINK_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(firelink ${FIRELINK_SOURCES})
target_include_directories(firelink PRIVATE include)
target_link_libraries(firelink PRIVATE gpiod pthread m)
