// sha256.h -- SHA-256 of a file, to check a downloaded mod against its index entry.
//
// Self-contained (FIPS 180-4) so it does not depend on which TLS backend libcurl
// was built with: Schannel on Windows, Secure Transport on macOS, OpenSSL on Linux.

#pragma once

#include <string>

namespace swos {

// Lower-case hex digest of the file's contents, or "" when it cannot be read.
std::string sha256_file(const std::string &path);

// Lower-case hex digest of `data`.
std::string sha256_hex(const std::string &data);

} // namespace swos
