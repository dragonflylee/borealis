cmake_minimum_required(VERSION 3.6)

project(BOREALIS_DEMO)

# armeabi-v7a requires cpufeatures library
# include(AndroidNdkModules)
# android_ndk_import_module_cpufeatures()

include(FetchContent)
FetchContent_Declare(SDL
    URL https://github.com/libsdl-org/SDL/archive/release-2.30.6.tar.gz
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/SDL2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL)

SET(PLATFORM_ANDROID ON CACHE BOOL "")
SET(LIBROMFS_PREBUILT_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/borealis/libromfs-generator" CACHE STRING "")

if (NOT EXISTS "${LIBROMFS_PREBUILT_GENERATOR}")
    message(FATAL_ERROR "libromfs-generator has not been built, please refer to borealis/build_libromfs_generator.sh for more information")
endif()

add_subdirectory(borealis)
