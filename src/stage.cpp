#include "stage.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <set>
#include <sstream>
#include <vector>

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#include "config.hpp"
#include "i18n.hpp"

namespace rpp {
namespace {

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

bool clone_file(const fs::path& src, const fs::path& dst, std::string* err) {
#if defined(__linux__) && defined(FICLONE)
  int in = ::open(src.c_str(), O_RDONLY | O_CLOEXEC);
  if (in >= 0) {
    int out = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (out >= 0) {
      int rc = ::ioctl(out, FICLONE, in);
      ::close(out);
      ::close(in);
      if (rc == 0) return true;
      std::error_code ec;
      fs::remove(dst, ec);
    } else {
      ::close(in);
    }
  }
#endif
  std::error_code ec;
  fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (err) *err = trf("stage.copy_failed", src.string(), dst.string(), ec.message());
    return false;
  }
  return true;
}

void add_file(std::vector<fs::path>& files, std::set<std::string>& seen, const fs::path& p) {
  if (p.empty()) return;
  std::string key = p.lexically_normal().string();
  if (!seen.insert(key).second) return;
  files.push_back(p);
}

// Remove snapshots left behind by a crashed/killed run.  Only entries older
// than a week are touched so a concurrent instance is never disturbed.
void prune_stage_root(const fs::path& base) {
  std::error_code ec;
  auto now = fs::file_time_type::clock::now();
  for (auto& e : fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec)) {
    if (!e.is_directory(ec)) continue;
    if (e.path().filename().string().rfind("run-", 0) != 0) continue;
    std::error_code te;
    auto ts = fs::last_write_time(e.path(), te);
    if (te) continue;
    if (now - ts > std::chrono::hours(24 * 7)) fs::remove_all(e.path(), ec);
  }
}

}  // namespace

fs::path default_stage_root() {
  if (auto e = env("RPP_RELEASE_STAGE"); !e.empty()) return expand_user(e);
  std::string xdg = env("XDG_CACHE_HOME");
  fs::path base = xdg.empty() ? expand_user("~/.cache") : fs::path(xdg);
  return base / "rpp-release-tool" / "stage";
}

StageResult stage_artifacts(const Artifacts& art, const fs::path& root) {
  StageResult r;
  if (art.dscs.empty() && art.debs.empty() && art.changes.empty()) {
    r.error = tr("stage.nothing");
    return r;
  }

  std::vector<fs::path> files;
  std::set<std::string> seen;

  // A .changes upload is all-or-nothing: stage every regular file in the
  // directory, since `reprepro include` resolves relative paths itself.
  if (!art.changes.empty()) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(art.dir, fs::directory_options::skip_permission_denied, ec)) {
      if (e.is_regular_file(ec)) add_file(files, seen, e.path());
    }
  } else {
    for (const auto& d : art.dscs) {
      add_file(files, seen, d.path);
      // Tarballs referenced by the .dsc must sit next to the staged .dsc.
      for (const auto& f : d.files) {
        fs::path p = d.path.parent_path() / f.name;
        std::error_code ec;
        if (fs::exists(p, ec)) add_file(files, seen, p);
      }
    }
    for (const auto& d : art.debs) add_file(files, seen, d.path);
    for (const auto& p : art.buildinfos) add_file(files, seen, p);
    for (const auto& p : art.detached_sigs) add_file(files, seen, p);
  }

  if (files.empty()) {
    r.error = tr("stage.nothing");
    return r;
  }
  for (const auto& p : files) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
      r.error = trf("stage.missing", p.string());
      return r;
    }
  }

  fs::path base = root.empty() ? default_stage_root() : root;
  std::error_code ec;
  fs::create_directories(base, ec);
  if (ec) {
    r.error = trf("stage.mkdir_failed", base.string(), ec.message());
    return r;
  }
  prune_stage_root(base);
  auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path dir = base / ("run-" + std::to_string(::getpid()) + "-" + std::to_string(stamp));
  fs::create_directories(dir, ec);
  if (ec) {
    r.error = trf("stage.mkdir_failed", dir.string(), ec.message());
    return r;
  }

  for (const auto& src : files) {
    fs::path dst = dir / src.filename();
    std::string err;
    std::uint64_t before = fs::file_size(src, ec);
    if (!clone_file(src, dst, &err)) {
      fs::remove_all(dir);
      r.error = err.empty() ? trf("stage.copy_failed", src.string(), dst.string(), "?") : err;
      return r;
    }
    if (ec || fs::file_size(dst, ec) != before) {
      fs::remove_all(dir);
      r.error = trf("stage.size_mismatch", src.string());
      return r;
    }
    ++r.copied;
  }

  Artifacts out = art;
  out.dir = dir;
  for (auto& d : out.dscs) d.path = dir / d.path.filename();
  for (auto& d : out.debs) d.path = dir / d.path.filename();
  for (auto& p : out.buildinfos) p = dir / p.filename();
  for (auto& p : out.detached_sigs) p = dir / p.filename();
  for (auto& p : out.changes) p = dir / p.filename();

  r.ok = true;
  r.dir = dir;
  r.art = std::move(out);
  return r;
}

void cleanup_stage(const fs::path& dir) {
  if (dir.empty()) return;
  std::error_code ec;
  fs::remove_all(dir, ec);
}

}  // namespace rpp
