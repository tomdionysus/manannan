set(manannan_test_plugins ${PROJECT_BINARY_DIR}/test-plugins)

# One directory per scan the loader's tests make: the good plugins, two
# libraries claiming one name, and each malformed library on its own.
function(manannan_test_plugin target directory)
  add_library(${target} MODULE plugins/test_plugin.cpp)
  target_link_libraries(${target} PRIVATE manannan_api)
  target_compile_definitions(${target} PRIVATE ${ARGN})
  set_target_properties(${target} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${manannan_test_plugins}/${directory})
  # Without GNU unique symbols, dlclose unloads the library, so each test sees the file on disk.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE -fno-gnu-unique)
  endif()
  add_dependencies(manannan_tests ${target})
endfunction()

add_executable(manannan_tests
  application_test.cpp
  check_ip_test.cpp
  config_test.cpp
  duration_test.cpp
  logger_test.cpp
  plugin_loader_test.cpp
  route53_test.cpp
  service_test.cpp
  sigv4_test.cpp)
target_include_directories(manannan_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manannan_tests PRIVATE manannan_core manannan_aws_sigv4 manannan_warnings GTest::gtest_main)
target_compile_definitions(manannan_tests PRIVATE
  MANANNAN_TEST_WORK_DIR="${PROJECT_BINARY_DIR}/test-work"
  MANANNAN_TEST_PLUGIN_DIR="${manannan_test_plugins}"
  MANANNAN_TEST_CHECK_IP_DIR="$<TARGET_FILE_DIR:manannan_source_amazon_check_ip>"
  MANANNAN_TEST_ROUTE53_DIR="$<TARGET_FILE_DIR:manannan_registry_aws_route53>"
  MANANNAN_TEST_DAEMON="$<TARGET_FILE:manannan>")
add_dependencies(manannan_tests manannan manannan_source_amazon_check_ip manannan_registry_aws_route53)

manannan_test_plugin(manannan_test_source good TEST_PLUGIN_SOURCE TEST_PLUGIN_NAME="test_source")
manannan_test_plugin(manannan_test_registry good TEST_PLUGIN_NAME="test_registry")
manannan_test_plugin(manannan_test_duplicate_a duplicate TEST_PLUGIN_SOURCE TEST_PLUGIN_NAME="test_source")
manannan_test_plugin(manannan_test_duplicate_b duplicate TEST_PLUGIN_SOURCE TEST_PLUGIN_NAME="test_source")
manannan_test_plugin(manannan_test_no_entrypoint no_entrypoint TEST_PLUGIN_NAME="broken" VARIANT_NO_ENTRYPOINT)
manannan_test_plugin(manannan_test_null_descriptor null_descriptor TEST_PLUGIN_NAME="broken" VARIANT_NULL_DESCRIPTOR)
manannan_test_plugin(manannan_test_wrong_abi wrong_abi TEST_PLUGIN_NAME="broken" VARIANT_WRONG_ABI)
manannan_test_plugin(manannan_test_no_create no_create TEST_PLUGIN_NAME="broken" VARIANT_NO_CREATE)
