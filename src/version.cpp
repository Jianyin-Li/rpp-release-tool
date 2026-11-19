#include "version.hpp"

#include <cctype>
#include <cstdlib>
#include <string_view>

namespace rpp {
namespace {

// dpkg's ordering for non-digit characters: "~" sorts before everything
// (including the end of the string), letters sort by ASCII, other symbols
// after letters.
int char_order(char c) {
  if (c == '\0') return 0;
  if (std::isdigit(static_cast<unsigned char>(c))) return 0;
  if (std::isalpha(static_cast<unsigned char>(c))) return static_cast<unsigned char>(c);
  if (c == '~') return -1;
  return static_cast<unsigned char>(c) + 256;
}

int verrevcmp(std::string_view a, std::string_view b) {
  std::size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    int first_diff = 0;
    while ((i < a.size() && !std::isdigit(static_cast<unsigned char>(a[i]))) ||
           (j < b.size() && !std::isdigit(static_cast<unsigned char>(b[j])))) {
      char ac = i < a.size() ? a[i] : '\0';
      char bc = j < b.size() ? b[j] : '\0';
      int ao = char_order(ac);
      int bo = char_order(bc);
      if (ao != bo) return ao - bo;
      if (i < a.size()) ++i;
      if (j < b.size()) ++j;
    }
    while (i < a.size() && a[i] == '0') ++i;
    while (j < b.size() && b[j] == '0') ++j;
    while (i < a.size() && j < b.size() && std::isdigit(static_cast<unsigned char>(a[i])) &&
           std::isdigit(static_cast<unsigned char>(b[j]))) {
      if (first_diff == 0) first_diff = a[i] - b[j];
      ++i;
      ++j;
    }
    if (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) return 1;
    if (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) return -1;
    if (first_diff != 0) return first_diff;
  }
  return 0;
}

// Split "epoch:upstream-revision".  The revision is the part after the last
// '-'; the epoch is only recognised when it is all digits.
struct ParsedVersion {
  unsigned long epoch = 0;
  std::string_view upstream;
  std::string_view revision;
};

ParsedVersion parse(std::string_view v) {
  ParsedVersion p;
  auto colon = v.find(':');
  if (colon != std::string_view::npos) {
    bool digits = colon > 0;
    for (std::size_t i = 0; i < colon; ++i) {
      if (!std::isdigit(static_cast<unsigned char>(v[i]))) digits = false;
    }
    if (digits) {
      p.epoch = std::strtoul(std::string(v.substr(0, colon)).c_str(), nullptr, 10);
      v.remove_prefix(colon + 1);
    }
  }
  auto dash = v.rfind('-');
  if (dash != std::string_view::npos) {
    p.upstream = v.substr(0, dash);
    p.revision = v.substr(dash + 1);
  } else {
    p.upstream = v;
  }
  return p;
}

}  // namespace

int deb_version_compare(const std::string& a, const std::string& b) {
  ParsedVersion pa = parse(a);
  ParsedVersion pb = parse(b);
  if (pa.epoch != pb.epoch) return pa.epoch < pb.epoch ? -1 : 1;
  int c = verrevcmp(pa.upstream, pb.upstream);
  if (c != 0) return c < 0 ? -1 : 1;
  c = verrevcmp(pa.revision, pb.revision);
  if (c != 0) return c < 0 ? -1 : 1;
  return 0;
}

}  // namespace rpp
