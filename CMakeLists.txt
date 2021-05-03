cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

project(dpmo)

set (CMAKE_C_FLAGS "-Wall -Wno-deprecated-declarations -Wsign-compare")

set (CMAKE_CXX_FLAGS "-g ${CMAKE_C_FLAGS} -Wall -std=c++14 -O0 -DTHREAD  -libverbs ")

set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/include)

set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")

include_directories(${INCLUDE_BASE})

SET(DPMO_SRC
        ${PROJECT_SOURCE_DIR}/src/Debug.cpp
        ${PROJECT_SOURCE_DIR}/src/Configuration.cpp
        ${PROJECT_SOURCE_DIR}/src/RdmaSocket.cpp
)

add_library(DPMO SHARED ${DPMO_SRC})
target_link_libraries(DPMO ibverbs pthread)

add_executable(test ${PROJECT_SOURCE_DIR}/testConfiguration.cpp)
add_executable(server 
        ${PROJECT_SOURCE_DIR}/RunServer.cpp
        ${PROJECT_SOURCE_DIR}/src/Server.cpp
)
add_executable(client 
        ${PROJECT_SOURCE_DIR}/RunClient.cpp
        ${PROJECT_SOURCE_DIR}/src/Client.cpp
)

add_executable(testRdmaSocket 
        ${PROJECT_SOURCE_DIR}/testRdmaSocket.cpp
)
target_link_libraries(testRdmaSocket DPMO)
target_link_libraries(test DPMO)
target_link_libraries(server DPMO)
target_link_libraries(client DPMO -libpmemobj)



SET(RDMAPMEM_SRC
        ${DPMO_SRC}
        ${PROJECT_SOURCE_DIR}/src/Client.cpp
        ${PROJECT_SOURCE_DIR}/lib/librdmapmem.cpp
)
add_library(rdmapmem SHARED ${RDMAPMEM_SRC})
target_link_libraries(rdmapmem ibverbs pthread pmemobj)