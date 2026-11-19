#pragma once

#include <string>

namespace rpp {

// Debian package version comparison (the same ordering as dpkg):
//   < 0 if a is older, 0 if equal, > 0 if a is newer.
// Handles epoch "1:", "~" pre-releases and the last "-" revision split.
int deb_version_compare(const std::string& a, const std::string& b);

}  // namespace rpp
