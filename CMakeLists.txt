cmake_minimum_required(VERSION 3.30)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PROJECT_NAME "ECS-Experiment")
set(LIBRARY_NAME "ECSExperiment")
project(${PROJECT_NAME} VERSION 0.1.0)

add_library(${LIBRARY_NAME} INTERFACE)

target_include_directories(
  ${LIBRARY_NAME} INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

set_target_properties(${LIBRARY_NAME} PROPERTIES CXX_STANDARD 20
                                                 CXX_STANDARD_REQUIRED ON)
target_compile_features(${LIBRARY_NAME} INTERFACE cxx_std_20)
# Compiler-specific options
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(${TARGET_NAME} PRIVATE /W4 /std:c++20 /Zi)
  set_target_properties(${TARGET_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
else()
  target_compile_options(${TARGET_NAME} PRIVATE -g -std=c++20 -Wall -Wextra)
endif()
