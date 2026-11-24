#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
# One libFuzzer target per reader of outside bytes; tools/ci.sh fuzz builds the
# whole tree with -fsanitize=fuzzer-no-link and the sanitizers, and runs each.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "MANANNAN_BUILD_FUZZING needs clang's libFuzzer")
endif()
foreach(target route53_response checkip_body dns_message dns_engine)
  add_executable(${target}_fuzz ${target}_fuzz.cpp)
  target_compile_options(${target}_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(${target}_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_libraries(${target}_fuzz PRIVATE manannan_warnings)
endforeach()
target_link_libraries(route53_response_fuzz PRIVATE manannan_aws)
target_link_libraries(checkip_body_fuzz PRIVATE manannan_check_ip)
target_link_libraries(dns_message_fuzz PRIVATE manannan_dns)
target_link_libraries(dns_engine_fuzz PRIVATE manannan_dns)
