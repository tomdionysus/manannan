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
  daemon/clock.cpp
  daemon/config.cpp
  daemon/duration.cpp
  daemon/migrate.cpp
  daemon/plugin_loader.cpp
  daemon/service.cpp
  loggers/logger_scoped.cpp
  loggers/logger_stdio.cpp
  loggers/util.cpp)
target_include_directories(manannan_core PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_definitions(manannan_core PRIVATE MANANNAN_DEFAULT_CONFIG="${CMAKE_INSTALL_FULL_SYSCONFDIR}/manannan/config.yaml"
                                                 MANANNAN_DEFAULT_SECRETS="${CMAKE_INSTALL_FULL_SYSCONFDIR}/manannan/secrets.yaml")
target_link_libraries(manannan_core PUBLIC manannan_api ${CMAKE_DL_LIBS} PRIVATE manannan_warnings)

# The DNS wire format: names, the message parser and the writer.
add_library(manannan_dns STATIC
  dns/message.cpp
  dns/name.cpp
  dns/writer.cpp)
target_include_directories(manannan_dns PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(manannan_dns PRIVATE manannan_warnings)

add_executable(manannan daemon/main.cpp)
target_link_libraries(manannan PRIVATE manannan_core manannan_warnings)
