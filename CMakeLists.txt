cmake_minimum_required(VERSION 3.10)

project(TinyIM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TinyIM
    src/main.cpp
    src/TcpServer.cpp
    src/Logger.cpp)

target_include_directories(TinyIM PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)