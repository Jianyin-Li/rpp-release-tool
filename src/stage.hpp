#pragma once

#include "model.hpp"

namespace rpp {

// A private snapshot of the selected artifacts.  Downstream checks, preview
// and reprepro all use these paths, so the build directory may be modified or
// deleted while the tool is running without affecting the publish.
struct StageResult {
  bool ok = false;
  std::string error;
  fs::path dir;
  Artifacts art;
  std::size_t copied = 0;  // files cloned (reflink) or byte-copied
};

fs::path default_stage_root();

// Copy the artifacts into a fresh directory below default_stage_root() (or
// `root` when non-empty).  Reflinks are used when the filesystem supports
// them; otherwise files are copied.  Hard links are deliberately not used:
// they fail across filesystems and do not protect against in-place rewrites.
StageResult stage_artifacts(const Artifacts& art, const fs::path& root = {});

void cleanup_stage(const fs::path& dir);

}  // namespace rpp
