cmake_minimum_required(VERSION 3.21)
project(ZaidRootTools VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if(NOT ANDROID)
    message(FATAL_ERROR "This Geode mod is Android-only. Use geode build -p android64")
endif()
if(DEFINED ANDROID_ABI AND NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Zaid Root Tools v0.1 only supports arm64-v8a")
endif()

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/RawEvdev.cpp
    src/RootInputReader.cpp
)

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Define GEODE_SDK with the path to Geode SDK 5.11.2")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
