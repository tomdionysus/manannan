#
# Manannan - plugin-based dynamic DNS updater
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
#
find_package(CURL REQUIRED)
# The body reader is a library of its own, so the fuzz target reaches it.
add_library(manannan_check_ip STATIC check_ip_body.cpp)
target_include_directories(manannan_check_ip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manannan_check_ip PRIVATE manannan_warnings)
add_library(manannan_source_amazon_check_ip MODULE amazon_check_ip.cpp)
target_link_libraries(manannan_source_amazon_check_ip PRIVATE manannan_api manannan_warnings manannan_check_ip CURL::libcurl)
set_target_properties(manannan_source_amazon_check_ip PROPERTIES PREFIX "" OUTPUT_NAME "manannan_source_amazon_check_ip")
install(TARGETS manannan_source_amazon_check_ip LIBRARY DESTINATION ${MANANNAN_PLUGIN_INSTALL_DIR})
