cmake_minimum_required(VERSION 3.22.1)

project("Iron")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}{debug_defines}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}{debug_defines}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}{release_defines}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}{release_defines}")

include_directories(
{includes})

add_library(
  iron
  SHARED
{files})

{libraries1}
target_link_libraries(
  iron
{libraries2})
