# Licensing

Manannan is GPL-3.0-only (`LICENSE`). Every source file carries the project's
header block.

It links these libraries, none of which is copied into the tree:

| Library | Used for | Licence |
| --- | --- | --- |
| yaml-cpp | reading the configuration | MIT |
| libcurl | HTTPS to the check IP service and Route 53 | curl (MIT-style) |
| OpenSSL 3 (libcrypto) | SHA-256 and HMAC for SigV4, cleansing secrets | Apache-2.0 |
| GoogleTest | the tests only, not shipped | BSD-3-Clause |

The Debian packages depend on the distribution's builds of the first three.
`debian/copyright` states the package's own licence.
