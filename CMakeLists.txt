cmake_minimum_required(VERSION 3.3)
project(lcx_libev)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")


set(LISTENER_SOURCE_FILES listener.cpp)
add_executable(lcx_listener ${LISTENER_SOURCE_FILES})

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    include_directories("/usr/local/Cellar/libev/4.20/include")
    find_library(EV_STATIC_LIBRARY libev.a "/usr/local/Cellar/libev/4.20/lib")
endif()

target_link_libraries(lcx_listener ${EV_STATIC_LIBRARY})