cmake_minimum_required(VERSION 3.20)
project(Manannan VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

add_library(manannan_api INTERFACE)
target_include_directories(manannan_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(manannan_api INTERFACE yaml-cpp::yaml-cpp)

add_library(manannan_logging STATIC
  src/loggers/logger_scoped.cpp
  src/loggers/logger_stdio.cpp
  src/util.cpp)
target_link_libraries(manannan_logging PUBLIC manannan_api)

add_executable(manannan
  src/main.cpp
  src/config.cpp
  src/duration.cpp
  src/plugin_loader.cpp
  src/service.cpp)
target_link_libraries(manannan PRIVATE manannan_logging ${CMAKE_DL_LIBS})

# Plugins install here; the generated configuration and unit file refer to the
# same install directories so they agree on every prefix and libdir layout.
set(MANANNAN_PLUGIN_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/manannan/plugins)
set(MANANNAN_PLUGIN_DIR ${CMAKE_INSTALL_FULL_LIBDIR}/manannan/plugins)
configure_file(config/manannan.yaml.in manannan.yaml @ONLY)

add_subdirectory(plugins)

install(TARGETS manannan RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(DIRECTORY include/manannan DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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

if(BUILD_TESTING)
  add_executable(manannan_tests
    tests/test_main.cpp
    src/config.cpp
    src/duration.cpp
    src/plugin_loader.cpp
    src/service.cpp)
  target_link_libraries(manannan_tests PRIVATE manannan_logging ${CMAKE_DL_LIBS})
  target_compile_definitions(manannan_tests PRIVATE
    MANANNAN_TEST_ROUTE53_DIR="$<TARGET_FILE_DIR:manannan_registry_aws_route53>")
  add_dependencies(manannan_tests manannan_registry_aws_route53)
  add_test(NAME manannan_tests COMMAND manannan_tests)
endif()

