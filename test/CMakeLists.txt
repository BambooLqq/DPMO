cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

project(test)

set (CMAKE_C_FLAGS "-Wall -Wno-deprecated-declarations -Wsign-compare")

set (CMAKE_CXX_FLAGS "-g ${CMAKE_C_FLAGS} -Wall -std=c++14 -O0 -DTHREAD  -libverbs -lpmemobj")

set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/../include)
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")
include_directories(${INCLUDE_BASE})

link_directories(${PROJECT_SOURCE_DIR}/../build/lib)
# 连接库rdmapmem
link_libraries(rdmapmem)

add_executable(testread 
    ${PROJECT_SOURCE_DIR}/testRead.cpp)

add_executable(testwrite 
    ${PROJECT_SOURCE_DIR}/testWrite.cpp)

add_executable(testgetpoolobj
    ${PROJECT_SOURCE_DIR}/testWrite.cpp)

target_link_libraries(testread rdmapmem pmemobj)
target_link_libraries(testwrite rdmapmem pmemobj)
target_link_libraries(testgetpoolobj rdmapmem pmemobj)



