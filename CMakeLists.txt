cmake_minimum_required(VERSION 3.20)
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
option(MANANNAN_COVERAGE "Instrument the daemon, plugins and tests for gcov" OFF)

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
if(MANANNAN_WARNINGS_AS_ERRORS)
  target_compile_options(manannan_warnings INTERFACE -Werror)
endif()
if(MANANNAN_COVERAGE)
  target_compile_options(manannan_warnings INTERFACE --coverage -O0 -g)
  target_link_options(manannan_warnings INTERFACE --coverage)
endif()

configure_file(include/manannan/version.h.in ${PROJECT_BINARY_DIR}/generated/manannan/version.h @ONLY)

add_library(manannan_api INTERFACE)
target_include_directories(manannan_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(manannan_api INTERFACE yaml-cpp::yaml-cpp)

# Everything but main(), so the tests drive the same code the daemon runs.
add_library(manannan_core STATIC
  src/application.cpp
  src/config.cpp
  src/duration.cpp
  src/loggers/logger_scoped.cpp
  src/loggers/logger_stdio.cpp
  src/plugin_loader.cpp
  src/service.cpp
  src/util.cpp)
target_link_libraries(manannan_core PUBLIC manannan_api ${CMAKE_DL_LIBS} PRIVATE manannan_warnings)

add_executable(manannan src/main.cpp)
target_link_libraries(manannan PRIVATE manannan_core manannan_warnings)

# Plugins install here; the generated configuration and unit file refer to the
# same install directories so they agree on every prefix and libdir layout.
set(MANANNAN_PLUGIN_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/manannan/plugins)
set(MANANNAN_PLUGIN_DIR ${CMAKE_INSTALL_FULL_LIBDIR}/manannan/plugins)
configure_file(config/manannan.yaml.in manannan.yaml @ONLY)
configure_file(man/manannan.8.in manannan.8 @ONLY)

add_subdirectory(plugins)

install(TARGETS manannan RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manannan.8 DESTINATION ${CMAKE_INSTALL_MANDIR}/man8)
install(DIRECTORY include/manannan DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "*.in" EXCLUDE)
install(FILES ${PROJECT_BINARY_DIR}/generated/manannan/version.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/manannan)
# Never overwrite an existing configuration: it holds the site's records and
# credentials. The template is installed only on a fresh install.
install(CODE "
  set(manannan_config \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SYSCONFDIR}/manannan/manannan.yaml\")
  if(EXISTS \"\${manannan_config}\")
    message(STATUS \"Keeping existing configuration: \${manannan_config}\")
  else()
    # file(INSTALL) applies DESTDIR itself.
    file(INSTALL \"${CMAKE_CURRENT_BINARY_DIR}/manannan.yaml\"
         DESTINATION \"${CMAKE_INSTALL_FULL_SYSCONFDIR}/manannan\")
  endif()")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  configure_file(packaging/manannan.service.in manannan.service @ONLY)
  # systemd reads units from <prefix>/lib/systemd/system, never lib64 or a multiarch libdir.
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manannan.service DESTINATION lib/systemd/system)
endif()

if(MANANNAN_BUILD_TESTING)
  find_package(GTest 1.12 CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
