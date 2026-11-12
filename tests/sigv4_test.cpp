// AWS Signature Version 4: known answers computed independently from the
// specification, with and without a session token, and RFC 3986 encoding.

#include <gtest/gtest.h>

#include <chrono>

#include "aws_sigv4.h"

namespace {
using manannan::aws::Credentials;
using manannan::aws::sign_v4;
using manannan::aws::url_encode;

std::chrono::system_clock::time_point at(long long seconds) {
  return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

const Credentials example{"AKIDEXAMPLE", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", ""};

TEST(SigV4, SignsARequestWithoutASessionToken) {
  const auto signed_request = sign_v4("GET", "route53.amazonaws.com", "/2013-04-01/hostedzone/Z1/rrset",
                                      "maxitems=1&name=a.example.com.&type=A", "", example, "us-east-1", "route53",
                                      at(1440938160));
  EXPECT_EQ(signed_request.amz_date, "20150830T123600Z");
  EXPECT_EQ(signed_request.content_sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(signed_request.authorization,
            "AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/route53/aws4_request, "
            "SignedHeaders=host;x-amz-content-sha256;x-amz-date, "
            "Signature=650caeefeebc3f876179b4b90e36e7e7dad6d1d11f517cddab504e87201843a6");
}

TEST(SigV4, SignsTheSessionTokenWhenThereIsOne) {
  auto credentials = example;
  credentials.session_token = "TOKEN123";
  const auto signed_request = sign_v4("POST", "127.0.0.1:8080", "/2013-04-01/hostedzone/Z1/rrset/", "", "<x/>",
                                      credentials, "eu-west-1", "route53", at(1794445323));
  EXPECT_EQ(signed_request.amz_date, "20261112T010203Z");
  EXPECT_EQ(signed_request.content_sha256, "2a31f44da4bd7decbbd3ddfd1a37ae04d02ec665e2c2688816ccc65631586ed1");
  EXPECT_EQ(signed_request.authorization,
            "AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20261112/eu-west-1/route53/aws4_request, "
            "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-security-token, "
            "Signature=1079305a583cb32a977741101b8cec9d4c1157149407782f570112739b3e1d29");
}

TEST(SigV4, UrlEncodesAllButUnreservedCharacters) {
  EXPECT_EQ(url_encode("AZaz09-_.~"), "AZaz09-_.~");
  EXPECT_EQ(url_encode("a b/*=&"), "a%20b%2F%2A%3D%26");
  EXPECT_EQ(url_encode("\xC3\xA9"), "%C3%A9");
}
}  // namespace
