cmake_minimum_required(VERSION 3.3)
project(lcx_libev)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")


set(LISTENER_SOURCE_FILES listener.cpp)
add_executable(lcx_listener ${LISTENER_SOURCE_FILES})

set(SLAVE_SOURCE_FILES slave.cpp)
add_executable(lcx_slave ${SLAVE_SOURCE_FILES})

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    include_directories("/usr/local/include")
    find_library(EV_STATIC_LIBRARY libev.a "/usr/local/lib")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    include_directories("/usrlocal/include")
    find_library(EV_STATIC_LIBRARY libev.a "/usr/local/lib")
endif()

target_link_libraries(lcx_listener ${EV_STATIC_LIBRARY})
target_link_libraries(lcx_slave ${EV_STATIC_LIBRARY})
