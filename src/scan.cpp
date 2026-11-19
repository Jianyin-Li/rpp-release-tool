#include "scan.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "exec.hpp"
#include "i18n.hpp"

namespace rpp {
namespace {

struct Stanza {
  std::map<std::string, std::string> fields;
};

Stanza parse_rfc822(std::istream& in, bool stop_at_signature = false) {
  Stanza st;
  std::string line, last_key;
  bool in_signed_body = true;
  bool saw_begin = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("-----BEGIN PGP SIGNED MESSAGE", 0) == 0) {
      saw_begin = true;
      in_signed_body = false;
      continue;
    }
    if (stop_at_signature && line.rfind("-----BEGIN PGP SIGNATURE", 0) == 0) break;
    if (saw_begin && !in_signed_body) {
      if (line.empty()) in_signed_body = true;
      continue;
    }
    if (line.empty()) {
      if (!st.fields.empty()) break;  // only the first stanza matters
      continue;
    }
    if (line[0] == ' ' || line[0] == '\t') {
      if (!last_key.empty()) st.fields[last_key] += "\n" + line.substr(1);
      continue;
    }
    auto c = line.find(':');
    if (c == std::string::npos) continue;
    last_key = line.substr(0, c);
    std::string val = line.substr(c + 1);
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
    st.fields[last_key] = val;
  }
  return st;
}

std::uint64_t to_u64(const std::string& s) {
  std::uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return 0;
    v = v * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return v;
}

}  // namespace

std::vector<std::string> split_words(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  std::string w;
  while (is >> w) out.push_back(w);
  return out;
}

std::string strip_source_version(const std::string& s) {
  auto sp = s.find(" (");
  return sp == std::string::npos ? s : s.substr(0, sp);
}

std::string dsc_source_name(const DscInfo& d) {
  auto s = d.field("Source");
  if (!s.empty()) return s;
  return d.field("Package");
}

std::string deb_source_name(const DebInfo& d) {
  if (!d.source.empty()) return strip_source_version(d.source);
  return d.package;
}

DscInfo parse_dsc(const fs::path& path) {
  DscInfo d;
  d.path = path;
  std::ifstream in(path);
  if (!in) return d;

  Stanza st = parse_rfc822(in, true);
  d.fields = st.fields;

  // Files / Checksums-Sha256 blocks are multi-line values in fields.
  auto merge = [&](const std::string& key, bool md5_side) {
    auto it = d.fields.find(key);
    if (it == d.fields.end()) return;
    std::istringstream is(it->second);
    std::string line;
    std::getline(is, line);  // first line is inline after the colon (usually empty)
    while (std::getline(is, line)) {
      std::istringstream ls(line);
      std::string a, sz, name;
      if (!(ls >> a >> sz >> name)) continue;
      FileEntry* e = nullptr;
      for (auto& f : d.files) {
        if (f.name == name) e = &f;
      }
      if (!e) {
        d.files.push_back(FileEntry{name, 0, {}, {}});
        e = &d.files.back();
      }
      e->size = to_u64(sz);
      if (md5_side) e->md5 = a;
      else e->sha256 = a;
    }
  };
  d.has_files_block = d.fields.count("Files") != 0;
  d.has_sha256_block = d.fields.count("Checksums-Sha256") != 0;
  merge("Files", true);
  merge("Checksums-Sha256", false);

  // Order follows the Files block; entries only in Checksums-Sha256 go last.
  return d;
}

DebInfo read_deb(const fs::path& path) {
  DebInfo d;
  d.path = path;
  auto r = run_capture({"dpkg-deb", "-f", path.string()});
  if (r.exit_code != 0 || r.out.empty()) return d;
  std::istringstream is(r.out);
  Stanza st = parse_rfc822(is);
  auto get = [&](const char* k) -> std::string {
    auto it = st.fields.find(k);
    return it == st.fields.end() ? std::string{} : it->second;
  };
  d.package = get("Package");
  d.version = get("Version");
  d.arch = get("Architecture");
  d.source = get("Source");
  d.section = get("Section");
  d.priority = get("Priority");
  d.depends = get("Depends");
  return d;
}

Artifacts scan_dir(const fs::path& dir) {
  Artifacts a;
  a.dir = dir;
  std::vector<fs::path> debs, dscs;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
    if (!e.is_regular_file(ec)) continue;
    auto ext = e.path().extension().string();
    if (ext == ".deb") debs.push_back(e.path());
    else if (ext == ".dsc") dscs.push_back(e.path());
    else if (ext == ".changes") a.changes.push_back(e.path());
    else if (ext == ".buildinfo") a.buildinfos.push_back(e.path());
    else if (ext == ".asc" || ext == ".sig") a.detached_sigs.push_back(e.path());
  }
  auto by_name = [](const fs::path& x, const fs::path& y) { return x.filename() < y.filename(); };
  std::sort(debs.begin(), debs.end(), by_name);
  std::sort(dscs.begin(), dscs.end(), by_name);
  std::sort(a.changes.begin(), a.changes.end(), by_name);
  std::sort(a.buildinfos.begin(), a.buildinfos.end(), by_name);
  std::sort(a.detached_sigs.begin(), a.detached_sigs.end(), by_name);

  for (auto& p : dscs) a.dscs.push_back(parse_dsc(p));
  for (auto& p : debs) a.debs.push_back(read_deb(p));

  if (!a.debs.empty() && !a.dscs.empty()) a.scene = Scene::Both;
  else if (!a.debs.empty()) a.scene = Scene::BinaryOnly;
  else if (!a.dscs.empty()) a.scene = Scene::SourceOnly;
  else a.scene = Scene::Empty;
  return a;
}

namespace {

Scene scene_of(std::size_t dscs, std::size_t debs) {
  if (dscs && debs) return Scene::Both;
  if (debs) return Scene::BinaryOnly;
  if (dscs) return Scene::SourceOnly;
  return Scene::Empty;
}

}  // namespace

std::vector<PackageSet> group_artifacts(const Artifacts& art) {
  std::vector<PackageSet> sets;
  if (!art.changes.empty()) {
    // Include-based uploads are all-or-nothing; offer one opaque set.
    PackageSet all;
    all.source = tr("sources.changes_set");
    for (std::size_t i = 0; i < art.dscs.size(); ++i) all.dscs.push_back(i);
    for (std::size_t i = 0; i < art.debs.size(); ++i) all.debs.push_back(i);
    if (!all.dscs.empty() || !all.debs.empty()) sets.push_back(std::move(all));
    return sets;
  }

  // Key: (source name, version).  A directory may contain several versions of
  // the same source (e.g. an old upload next to a rebuild).
  std::map<std::pair<std::string, std::string>, PackageSet> by_key;
  for (std::size_t i = 0; i < art.dscs.size(); ++i) {
    const DscInfo& d = art.dscs[i];
    std::string src = dsc_source_name(d);
    if (src.empty()) src = d.path.stem().string();
    auto& s = by_key[{src, d.field("Version")}];
    s.source = src;
    s.version = d.field("Version");
    s.dscs.push_back(i);
  }
  for (std::size_t i = 0; i < art.debs.size(); ++i) {
    const DebInfo& d = art.debs[i];
    std::string src = deb_source_name(d);
    if (src.empty()) src = d.package;
    if (src.empty()) src = d.path.stem().string();
    auto& s = by_key[{src, d.version}];
    s.source = src;
    s.version = d.version;
    s.debs.push_back(i);
  }
  for (auto& [key, set] : by_key) {
    (void)key;
    sets.push_back(std::move(set));
  }
  std::sort(sets.begin(), sets.end(), [](const PackageSet& x, const PackageSet& y) {
    if (x.source != y.source) return x.source < y.source;
    return x.version < y.version;
  });
  return sets;
}

Artifacts select_artifacts(const Artifacts& art, const std::vector<PackageSet>& sets,
                           const std::vector<bool>& selected) {
  Artifacts out;
  out.dir = art.dir;
  bool all = true;
  for (std::size_t i = 0; i < sets.size(); ++i) {
    if (i >= selected.size() || !selected[i]) {
      all = false;
      continue;
    }
    for (std::size_t idx : sets[i].dscs) {
      if (idx < art.dscs.size()) out.dscs.push_back(art.dscs[idx]);
    }
    for (std::size_t idx : sets[i].debs) {
      if (idx < art.debs.size()) out.debs.push_back(art.debs[idx]);
    }
  }
  // Extra metadata belongs to the whole upload.  Keep it only when nothing was
  // filtered out, otherwise it would describe files that are no longer part of
  // the selection.
  if (all) {
    out.changes = art.changes;
    out.buildinfos = art.buildinfos;
    out.detached_sigs = art.detached_sigs;
  }
  out.scene = scene_of(out.dscs.size(), out.debs.size());
  return out;
}

}  // namespace rpp
