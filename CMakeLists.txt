cmake_minimum_required(VERSION 3.2.0)

project(ping)

if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

add_definitions(-DUNICODE -D_UNICODE)

# Target Windows Server 2003
add_definitions(
    -DWINVER=0x502
    -D_WIN32_IE=0x600
    -D_WIN32_WINNT=0x502
    -D_WIN32_WINDOWS=0x502
)

add_compile_options(/nologo)
add_compile_options(/W4)
add_compile_options(/Gm-)
add_compile_options(/GS)
add_compile_options(/MP)
add_compile_options(/MTd)

add_executable(ping ping.c)
target_link_libraries(ping ws2_32 iphlpapi)
