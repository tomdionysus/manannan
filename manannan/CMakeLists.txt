#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
# The plugin interface: what a plugin compiles against, and what manannan-dev
# installs.
add_library(manannan_api INTERFACE)
target_include_directories(manannan_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(manannan_api INTERFACE yaml-cpp::yaml-cpp)

# Everything but main(), so the tests drive the same code the daemon runs.
add_library(manannan_core STATIC
  daemon/application.cpp
  daemon/config.cpp
  daemon/duration.cpp
  daemon/plugin_loader.cpp
  daemon/service.cpp
  loggers/logger_scoped.cpp
  loggers/logger_stdio.cpp
  loggers/util.cpp)
target_include_directories(manannan_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(manannan_core PUBLIC manannan_api ${CMAKE_DL_LIBS} PRIVATE manannan_warnings)

add_executable(manannan daemon/main.cpp)
target_link_libraries(manannan PRIVATE manannan_core manannan_warnings)
