cmake_minimum_required(VERSION 3.22)

project(zyvst VERSION 1.0.0 LANGUAGES C CXX)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture" FORCE)
    endif()

    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags for low latency
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3)
endif()

add_subdirectory(third_party/JUCE)

if(APPLE)
    option(ZYV_BUILD_FX "Build the effect AU" ON)
    # Instrument and effect AU targets both fully support VST3 instruments and effects.
    option(ZYV_BUILD_INST "Build the instrument AU" ON)
    option(ZYV_BUILD_MIDI "Build the MIDI FX AU" ON)

    add_subdirectory(plugin)
endif()
