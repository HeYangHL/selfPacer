cmake_minimum_required(VERSION 3.0)
project(pacer VERSION 0001 DESCRIPTION "self pacer")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

file(GLOB SRC 
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(pacer SHARED ${SRC})
target_link_libraries(pacer PUBLIC Threads::Threads)