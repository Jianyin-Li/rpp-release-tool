#include "checks.hpp"

#include <algorithm>
#include <set>

#include "exec.hpp"
#include "i18n.hpp"
#include "scan.hpp"

namespace rpp {
namespace {

Severity policy_sev(const std::string& v, Severity fallback) {
  if (v == "error") return Severity::Block;
  if (v == "warn") return Severity::Warn;
  if (v == "ignore") return Severity::Ok;
  return fallback;
}

bool is_binary_arch(const std::string& a) { return !a.empty() && a != "source"; }

std::vector<std::string> dsc_binaries(const DscInfo& d) {
  std::string raw = d.field("Binary");
  std::vector<std::string> out;
  std::string cur;
  for (char c : raw) {
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool is_source_artifact_for(const fs::path& p, const DscInfo& d) {
  auto name = p.filename().string();
  if (name.find(".orig.tar") != std::string::npos ||
      name.find(".debian.tar") != std::string::npos) {
    return true;
  }
  // Native source packages use <source>_<version>.tar.*; a binary release
  // bundle such as foo_1.0_linux_amd64.tar.gz does not match this.
  auto src = dsc_source_name(d);
  auto ver = d.field("Version");
  if (!src.empty() && !ver.empty()) {
    auto native = src + "_" + ver + ".tar.";
    if (name.rfind(native, 0) == 0) return true;
  }
  return false;
}

}  // namespace

int count_issues(const std::vector<Issue>& issues, Severity sev) {
  return static_cast<int>(std::count_if(issues.begin(), issues.end(),
                                        [&](const Issue& i) { return i.sev == sev; }));
}

bool has_blocking(const std::vector<Issue>& issues) {
  return count_issues(issues, Severity::Block) > 0;
}

std::vector<Issue> run_checks(const CheckInput& in) {
  std::vector<Issue> r;
  const Artifacts& a = *in.art;
  const Policy& pol = *in.policy;

  std::set<std::string> targets;
  for (const auto& x : in.target_archs) {
    if (is_binary_arch(x)) targets.insert(x);
  }

  // ---- scene -------------------------------------------------------------
  switch (a.scene) {
    case Scene::Both:
      r.push_back({Severity::Ok, "scene.both", tr("scan.scene.both"), {}, a.dir});
      break;
    case Scene::BinaryOnly:
      r.push_back({Severity::Warn, "scene.binary", tr("scan.scene.binary"), {}, a.dir});
      break;
    case Scene::SourceOnly:
      r.push_back({Severity::Warn, "scene.source", tr("scan.scene.source"), {}, a.dir});
      break;
    case Scene::Empty:
      r.push_back({Severity::Block, "scene.empty", tr("scan.scene.empty"), {}, a.dir});
      break;
  }
  if (a.dscs.size() > 1) {
    r.push_back({Severity::Block, "scene.multi_dsc",
                 trf("scan.multi_dsc", a.dscs.size()), {}, a.dir});
  }

  // ---- source package ----------------------------------------------------
  std::set<std::string> listed_files;
  for (const auto& d : a.dscs) {
    if (!d.has("Priority")) {
      r.push_back({Severity::Warn, "dsc.no_priority", tr("check.dsc.no_priority"), {}, d.path});
    }
    if (!d.has("Version") || d.field("Version").empty()) {
      r.push_back({Severity::Block, "dsc.no_version", tr("check.dsc.no_version"), {}, d.path});
    } else {
      r.push_back({Severity::Ok, "dsc.version", trf("scan.source_version", d.field("Version")), {}, d.path});
    }
    if (dsc_source_name(d).empty()) {
      r.push_back({Severity::Block, "dsc.no_source", tr("check.dsc.no_source"), {}, d.path});
    }
    if (!d.has_files_block) {
      r.push_back({Severity::Block, "dsc.no_files", tr("check.dsc.no_files"), {}, d.path});
    }
    if (!in.codename.empty() && d.has("Distribution") && !d.field("Distribution").empty() &&
        d.field("Distribution") != in.codename) {
      r.push_back({Severity::Warn, "dsc.distribution",
                   trf("pf.dist_mismatch", d.field("Distribution"), in.codename), {}, d.path});
    }

    for (const auto& f : d.files) {
      listed_files.insert(f.name);
      fs::path fp = d.path.parent_path() / f.name;
      std::error_code ec;
      if (!fs::exists(fp, ec)) {
        r.push_back({Severity::Block, "file.missing", trf("check.file.missing", f.name), {}, fp});
        continue;
      }
      auto actual = file_size_of(fp);
      if (f.size != 0 && actual != f.size) {
        r.push_back({Severity::Block, "file.size",
                     trf("check.file.size", f.name, f.size, actual), {}, fp});
        continue;
      }
      if (!f.md5.empty()) {
        auto got = md5_file(fp);
        if (got != f.md5) {
          r.push_back({Severity::Block, "file.md5", trf("check.file.md5", f.name), {}, fp});
          continue;
        }
      }
      if (!f.sha256.empty()) {
        auto got = sha256_file(fp);
        if (got != f.sha256) {
          auto sev = policy_sev(pol.sha256, Severity::Block);
          if (sev != Severity::Ok) {
            r.push_back({sev, "file.sha256", trf("check.file.sha256", f.name), {}, fp});
          }
          continue;
        }
      }
      r.push_back({Severity::Ok, "file.ok", trf("check.file.ok", f.name), {}, fp});
    }

  }

  // ---- orphan source artifacts ------------------------------------------
  if (!a.dscs.empty()) {
    const DscInfo& source_dsc = a.dscs.front();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(a.dir, fs::directory_options::skip_permission_denied, ec)) {
      if (!e.is_regular_file(ec)) continue;
      auto name = e.path().filename().string();
      auto ext = e.path().extension().string();
      if (ext == ".deb" || ext == ".dsc" || ext == ".changes" || ext == ".buildinfo" ||
          ext == ".asc" || ext == ".sig")
        continue;
      if (!is_source_artifact_for(e.path(), source_dsc)) continue;
      if (!listed_files.count(name)) {
        r.push_back({Severity::Warn, "file.unlisted", trf("check.file.unlisted", name), {}, e.path()});
      }
    }
  }

  // ---- binary packages ---------------------------------------------------
  std::set<std::string> have_archs;
  std::set<std::string> have_pkgs;
  std::set<std::string> dsc_versions;
  std::set<std::string> dsc_sources;
  for (const auto& d : a.dscs) {
    dsc_versions.insert(d.field("Version"));
    dsc_sources.insert(dsc_source_name(d));
  }
  const DscInfo* dsc = a.dscs.empty() ? nullptr : &a.dscs.front();
  std::set<std::string> dsc_bins;
  if (dsc) {
    for (const auto& b : dsc_binaries(*dsc)) dsc_bins.insert(b);
  }

  std::set<std::string> buildinfo_archs;
  for (const auto& b : a.buildinfos) {
    auto stem = b.stem().string();  // foo_1.0_amd64
    auto us = stem.rfind('_');
    if (us != std::string::npos) buildinfo_archs.insert(stem.substr(us + 1));
  }

  for (const auto& d : a.debs) {
    if (d.package.empty() || d.version.empty() || d.arch.empty()) {
      r.push_back({Severity::Block, "deb.unreadable",
                   trf("err.cannot_read", d.path.filename().string(), "dpkg-deb -f"), {}, d.path});
      continue;
    }
    have_archs.insert(d.arch);
    have_pkgs.insert(d.package);

    if (dsc) {
      if (!dsc->field("Version").empty() && d.version != dsc->field("Version")) {
        r.push_back({Severity::Block, "deb.version",
                     trf("check.deb.version", d.path.filename().string(), d.version, dsc->field("Version")),
                     {}, d.path});
      }
      auto src = deb_source_name(d);
      if (!dsc_sources.empty() && !dsc_sources.count(src)) {
        r.push_back({Severity::Block, "deb.source",
                     trf("check.deb.source", d.path.filename().string(), src, dsc_source_name(*dsc)),
                     {}, d.path});
      }
      if (!dsc_bins.empty() && !dsc_bins.count(d.package)) {
        r.push_back({Severity::Warn, "deb.binary",
                     trf("check.deb.binary", d.package), {}, d.path});
      }
    }

    if (d.arch != "all" && !targets.empty() && !targets.count(d.arch)) {
      r.push_back({Severity::Warn, "deb.arch",
                   trf("check.deb.arch", d.path.filename().string(), d.arch), {}, d.path});
    }
    if (d.priority.empty()) {
      r.push_back({Severity::Block, "deb.no_priority",
                   trf("check.deb.no_priority", d.path.filename().string()), {}, d.path});
    }
    if (d.arch == "all") {
      r.push_back({Severity::Ok, "deb.all", trf("check.deb.all", d.package), {}, d.path});
    } else {
      r.push_back({Severity::Ok, "deb", d.package + " " + d.version + " (" + d.arch + ")", {}, d.path});
    }

    auto bi_sev = policy_sev(pol.buildinfo, Severity::Warn);
    if (bi_sev != Severity::Ok && !buildinfo_archs.count(d.arch)) {
      r.push_back({bi_sev, "buildinfo.missing", trf("check.buildinfo.missing", d.path.filename().string()),
                   {}, d.path});
    }
  }

  // Binaries declared by the .dsc but not shipped in this directory.  This is
  // legitimate when only a subset is being published, so it stays a warning.
  if (dsc && !dsc_bins.empty()) {
    for (const auto& b : dsc_bins) {
      if (!have_pkgs.count(b)) {
        r.push_back({Severity::Warn, "deb.declared_missing",
                     trf("check.deb.declared_missing", b), {}, dsc->path});
      }
    }
  }

  // Architecture coverage.  A source that only builds Architecture: all
  // binaries is available on every architecture, so "all" alone satisfies
  // every target binary architecture.
  if (!targets.empty() && !a.debs.empty() && !a.dscs.empty()) {
    auto miss_sev = policy_sev(pol.missing_arch, Severity::Block);
    bool only_all = have_archs.size() == 1 && have_archs.count("all") != 0;
    for (const auto& t : targets) {
      if (!have_archs.count(t) && !only_all) {
        if (miss_sev != Severity::Ok) {
          r.push_back({miss_sev, "arch.missing", trf("check.arch.missing", t), {}, a.dir});
        }
      }
    }
    for (const auto& h : have_archs) {
      if (h != "all" && !targets.count(h)) {
        r.push_back({Severity::Warn, "arch.extra", trf("check.arch.extra", h), {}, a.dir});
      }
    }
  }

  // Binary-only consistency.
  if (a.dscs.empty() && a.debs.size() > 1) {
    std::set<std::string> versions, sources;
    for (const auto& d : a.debs) {
      if (!d.version.empty()) versions.insert(d.version);
      auto s = deb_source_name(d);
      if (!s.empty()) sources.insert(s);
    }
    if (versions.size() > 1) {
      r.push_back({Severity::Block, "binary.version_drift",
                   trf("check.binary.version_drift", *versions.begin(), *versions.rbegin()), {}, a.dir});
    }
    if (sources.size() > 1) {
      r.push_back({Severity::Block, "binary.source_drift",
                   trf("check.binary.source_drift", a.debs.front().path.filename().string(), *sources.begin()),
                   {}, a.dir});
    }
  }

  return r;
}

}  // namespace rpp
