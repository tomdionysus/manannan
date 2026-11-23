#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
cmake_minimum_required(VERSION 3.24)
project(Manannan VERSION 0.3.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(MANANNAN_BUILD_TESTING "Build manannan_tests (GoogleTest)" OFF)
option(MANANNAN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MANANNAN_BUILD_FUZZING "Build each fuzz target as its own binary (clang and libFuzzer)" OFF)

find_package(yaml-cpp 0.6 CONFIG REQUIRED)
# yaml-cpp before 0.8 exports its target without the yaml-cpp:: namespace.
if(NOT TARGET yaml-cpp::yaml-cpp)
  if(TARGET yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
  else()
    add_library(yaml-cpp::yaml-cpp INTERFACE IMPORTED)
    target_include_directories(yaml-cpp::yaml-cpp INTERFACE ${YAML_CPP_INCLUDE_DIR})
    target_link_libraries(yaml-cpp::yaml-cpp INTERFACE ${YAML_CPP_LIBRARIES})
  endif()
endif()

add_library(manannan_warnings INTERFACE)
target_compile_options(manannan_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(manannan_warnings INTERFACE -Wthread-safety)
endif()
if(MANANNAN_WARNINGS_AS_ERRORS)
  target_compile_options(manannan_warnings INTERFACE -Werror)
endif()

configure_file(manannan/include/manannan/version.h.in ${PROJECT_BINARY_DIR}/generated/manannan/version.h @ONLY)

# Executables at the top of the build tree, so the core's binary directory
# cannot share the daemon's name.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
add_subdirectory(manannan core)

# Plugins install here; the generated configuration and unit file refer to the
# same install directories so they agree on every prefix and libdir layout.
set(MANANNAN_PLUGIN_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/manannan/plugins)
set(MANANNAN_PLUGIN_DIR ${CMAKE_INSTALL_FULL_LIBDIR}/manannan/plugins)
configure_file(config/config.yaml.in config.yaml @ONLY)
configure_file(man/manannan.8.in manannan.8 @ONLY)

add_subdirectory(plugins)
if(MANANNAN_BUILD_FUZZING)
  add_subdirectory(manannan/fuzz fuzz)
endif()

install(TARGETS manannan RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manannan.8 DESTINATION ${CMAKE_INSTALL_MANDIR}/man8)
install(DIRECTORY manannan/include/manannan DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "*.in" EXCLUDE)
install(FILES ${PROJECT_BINARY_DIR}/generated/manannan/version.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/manannan)
# Never overwrite an existing configuration: it holds the site's records. The
# template is installed only on a fresh install. The secrets file is never
# installed: the package's postinst creates it, and a source install by hand.
install(CODE "
  set(manannan_config \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/manannan/config.yaml\")
  if(EXISTS \"\${manannan_config}\")
    message(STATUS \"Keeping existing configuration: \${manannan_config}\")
  else()
    # file(INSTALL) applies DESTDIR itself.
    file(INSTALL \"${CMAKE_CURRENT_BINARY_DIR}/config.yaml\"
         DESTINATION \"${CMAKE_INSTALL_FULL_SYSCONFDIR}/manannan\")
  endif()")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  configure_file(packaging/manannan.service.in manannan.service @ONLY)
  # systemd reads units and sysusers.d from <prefix>/lib, never lib64 or a multiarch libdir.
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manannan.service DESTINATION lib/systemd/system)
  install(FILES packaging/manannan.sysusers DESTINATION lib/sysusers.d RENAME manannan.conf)
endif()

if(MANANNAN_BUILD_TESTING)
  find_package(GTest 1.12 CONFIG REQUIRED)
  add_subdirectory(manannan/test test)
endif()
