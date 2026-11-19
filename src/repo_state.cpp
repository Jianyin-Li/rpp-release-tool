#include "repo_state.hpp"

#include <fstream>
#include <sstream>

#include "exec.hpp"

namespace rpp {
namespace {

std::string read_text(const fs::path& p) {
  std::error_code ec;
  if (fs::exists(p, ec)) {
    std::ifstream in(p);
    if (in) {
      std::ostringstream os;
      os << in.rdbuf();
      return os.str();
    }
  }
  fs::path gz = p;
  gz += ".gz";
  if (fs::exists(gz, ec)) {
    auto r = run_capture({"zcat", "--", gz.string()});
    if (r.exit_code == 0) return r.out;
  }
  fs::path xz = p;
  xz += ".xz";
  if (fs::exists(xz, ec)) {
    auto r = run_capture({"xzcat", "--", xz.string()});
    if (r.exit_code == 0) return r.out;
  }
  return {};
}

struct Stanza {
  std::map<std::string, std::string> f;
};

std::vector<Stanza> parse_stanzas(const std::string& text) {
  std::vector<Stanza> out;
  Stanza cur;
  std::string line, key;
  std::istringstream is(text);
  auto flush = [&] {
    if (!cur.f.empty()) out.push_back(cur);
    cur = {};
    key.clear();
  };
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) {
      flush();
      continue;
    }
    if (line[0] == ' ' || line[0] == '\t') {
      if (!key.empty()) cur.f[key] += "\n" + line.substr(1);
      continue;
    }
    auto c = line.find(':');
    if (c == std::string::npos) continue;
    key = line.substr(0, c);
    std::string v = line.substr(c + 1);
    while (!v.empty() && v.front() == ' ') v.erase(0, 1);
    cur.f[key] = v;
  }
  flush();
  return out;
}

std::uint64_t to_u64(const std::string& s) {
  std::uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return 0;
    v = v * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return v;
}

std::string get(const Stanza& s, const char* k) {
  auto it = s.f.find(k);
  return it == s.f.end() ? std::string{} : it->second;
}

}  // namespace

RepoState load_repo_state(const fs::path& base, const std::string& codename,
                          const std::string& component, const std::vector<std::string>& archs) {
  RepoState st;
  bool any = false;

  for (const auto& arch : archs) {
    if (arch == "source") continue;
    fs::path p = base / "dists" / codename / component / ("binary-" + arch) / "Packages";
    auto text = read_text(p);
    if (text.empty()) continue;
    any = true;
    for (const auto& s : parse_stanzas(text)) {
      auto pkg = get(s, "Package");
      auto ver = get(s, "Version");
      auto file = get(s, "Filename");
      if (pkg.empty()) continue;
      st.index[pkg + "|" + arch] = ver;
      if (!file.empty()) {
        PoolFile pf;
        pf.name = fs::path(file).filename().string();
        pf.pool_path = file;
        pf.sha256 = get(s, "SHA256");
        pf.size = to_u64(get(s, "Size"));
        st.pool[pf.name] = pf;
      }
    }
  }

  fs::path sp = base / "dists" / codename / component / "source" / "Sources";
  auto stext = read_text(sp);
  if (!stext.empty()) {
    any = true;
    for (const auto& s : parse_stanzas(stext)) {
      auto pkg = get(s, "Package");
      auto ver = get(s, "Version");
      if (!pkg.empty()) st.index[pkg + "|source"] = ver;

      auto dir = get(s, "Directory");
      auto sha_block = get(s, "Checksums-Sha256");
      std::istringstream is(sha_block);
      std::string line;
      std::getline(is, line);  // inline remainder
      while (std::getline(is, line)) {
        std::istringstream ls(line);
        std::string sha, sz, name;
        if (!(ls >> sha >> sz >> name)) continue;
        PoolFile pf;
        pf.name = name;
        pf.pool_path = (fs::path(dir) / name).string();
        pf.sha256 = sha;
        pf.size = to_u64(sz);
        st.pool[name] = pf;
      }
    }
  }

  if (!any) {
    st.error = (base / "dists" / codename).string();
    return st;
  }
  st.ok = true;
  return st;
}

}  // namespace rpp
