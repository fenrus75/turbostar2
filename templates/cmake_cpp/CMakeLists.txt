cmake_minimum_required(VERSION 3.16)
project(@@PROJECT_NAME@@ VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(@@EXECUTABLE_NAME@@ src/main.cpp)
