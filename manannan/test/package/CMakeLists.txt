#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
# A plugin author's build against the installed manannan-dev headers, found
# through CMAKE_PREFIX_PATH: tools/ci.sh package builds and runs it.
cmake_minimum_required(VERSION 3.24)
project(consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(yaml-cpp CONFIG REQUIRED)
find_path(MANANNAN_INCLUDE_DIR manannan/plugin.h REQUIRED)

add_executable(consumer consumer.cpp)
target_include_directories(consumer PRIVATE ${MANANNAN_INCLUDE_DIR})
target_link_libraries(consumer PRIVATE yaml-cpp::yaml-cpp)
