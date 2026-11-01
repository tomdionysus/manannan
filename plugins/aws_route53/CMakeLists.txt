find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
add_library(manannan_registry_aws_route53 MODULE aws_route53.cpp aws_sigv4.cpp)
target_link_libraries(manannan_registry_aws_route53 PRIVATE manannan_api CURL::libcurl OpenSSL::Crypto)
set_target_properties(manannan_registry_aws_route53 PROPERTIES PREFIX "" OUTPUT_NAME "manannan_registry_aws_route53")
install(TARGETS manannan_registry_aws_route53 LIBRARY DESTINATION ${MANANNAN_PLUGIN_INSTALL_DIR})

