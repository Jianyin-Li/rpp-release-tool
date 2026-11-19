#pragma once

#include <string>
#include <vector>

#include "model.hpp"

namespace rpp {

Artifacts scan_dir(const fs::path& dir);
DscInfo parse_dsc(const fs::path& path);
DebInfo read_deb(const fs::path& path);

// A local "package set": one source package at one version, together with all
// of its binary packages found in the same directory.
struct PackageSet {
  std::string source;
  std::string version;
  std::vector<std::size_t> dscs;  // indices into Artifacts::dscs
  std::vector<std::size_t> debs;  // indices into Artifacts::debs
  bool has_source() const { return !dscs.empty(); }
  bool has_binary() const { return !debs.empty(); }
};

// Group a scan into publishable sets.  Directories containing a .changes file
// cannot be split safely (the .changes references every file in the upload),
// so such a scan yields a single set containing everything.
std::vector<PackageSet> group_artifacts(const Artifacts& art);

// Keep only the selected sets.  Source files referenced by a selected .dsc
// (tarballs) stay available to the caller because staging copies them.
Artifacts select_artifacts(const Artifacts& art, const std::vector<PackageSet>& sets,
                           const std::vector<bool>& selected);

std::string dsc_source_name(const DscInfo& d);
std::vector<std::string> split_words(const std::string& s);
std::string strip_source_version(const std::string& s);
std::string deb_source_name(const DebInfo& d);

}  // namespace rpp
