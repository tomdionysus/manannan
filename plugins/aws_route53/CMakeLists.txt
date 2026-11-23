#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
# Signing, secrets and the response reader are a library of their own, so the
# tests can check signing against fixed times and the fuzz targets reach the
# reader.
add_library(manannan_aws STATIC aws_sigv4.cpp route53_response.cpp secret.cpp)
target_include_directories(manannan_aws PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manannan_aws PUBLIC OpenSSL::Crypto PRIVATE manannan_warnings)
add_library(manannan_registry_aws_route53 MODULE aws_route53.cpp)
target_link_libraries(manannan_registry_aws_route53 PRIVATE manannan_api manannan_warnings manannan_aws CURL::libcurl)
set_target_properties(manannan_registry_aws_route53 PROPERTIES PREFIX "" OUTPUT_NAME "manannan_registry_aws_route53")
install(TARGETS manannan_registry_aws_route53 LIBRARY DESTINATION ${MANANNAN_PLUGIN_INSTALL_DIR})
