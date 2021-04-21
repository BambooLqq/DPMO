cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

project(dpmo)

set (CMAKE_C_FLAGS "-Wall -Wno-deprecated-declarations -Wsign-compare")

set (CMAKE_CXX_FLAGS "-g ${CMAKE_C_FLAGS} -std=c++14 -Wall -O0 -DTHREAD  -libverbs ")

set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/include)

set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")

include_directories(${INCLUDE_BASE})

SET(DPMO_SRC
        ${PROJECT_SOURCE_DIR}/src/Debug.cpp
        ${PROJECT_SOURCE_DIR}/src/Configuration.cpp
)

add_library(DPMO SHARED ${DPMO_SRC})

add_executable(test ${PROJECT_SOURCE_DIR}/testConfiguration.cpp)

target_link_libraries(test DPMO)


