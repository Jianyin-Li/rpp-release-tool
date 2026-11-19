#include "reprepro_conf.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace rpp {
namespace {

std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  std::string w;
  while (is >> w) out.push_back(w);
  return out;
}

using Stanza = std::map<std::string, std::string>;

std::vector<Stanza> parse_stanzas(std::istream& in) {
  std::vector<Stanza> out;
  Stanza cur;
  std::string last_key;
  std::string line;
  auto flush = [&] {
    if (!cur.empty()) out.push_back(cur);
    cur.clear();
    last_key.clear();
  };
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) {
      flush();
      continue;
    }
    if (line[0] == '#') continue;
    if (line[0] == ' ' || line[0] == '\t') {
      if (!last_key.empty()) cur[last_key] += " " + trim(line);
      continue;
    }
    auto c = line.find(':');
    if (c == std::string::npos) continue;
    std::string key = trim(line.substr(0, c));
    std::string val = trim(line.substr(c + 1));
    if (key.empty()) continue;
    last_key = key;
    cur[key] = val;
  }
  flush();
  return out;
}

}  // namespace

fs::path resolve_distributions_path(const fs::path& conf) {
  std::error_code ec;
  if (fs::is_directory(conf, ec)) return conf / "distributions";
  return conf;
}

RepoConf parse_repo_conf(const fs::path& distributions_path) {
  RepoConf r;
  fs::path dist_path = resolve_distributions_path(distributions_path);
  std::ifstream in(dist_path);
  if (!in) {
    r.error = dist_path.string();
    return r;
  }
  for (auto& st : parse_stanzas(in)) {
    Distribution d;
    auto get = [&](const std::string& k) -> std::string {
      auto it = st.find(k);
      return it == st.end() ? std::string{} : it->second;
    };
    d.codename = get("Codename");
    d.suite = get("Suite");
    d.origin = get("Origin");
    d.label = get("Label");
    d.sign_with = get("SignWith");
    d.components = split_ws(get("Components"));
    d.architectures = split_ws(get("Architectures"));
    if (d.codename.empty() && d.suite.empty()) continue;
    if (d.codename.empty()) d.codename = d.suite;
    r.distributions.push_back(std::move(d));
  }

  fs::path opt = dist_path.parent_path() / "options";
  std::ifstream oin(opt);
  if (oin) {
    std::string line;
    while (std::getline(oin, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      auto sp = line.find_first_of(" \t");
      if (sp == std::string::npos) continue;
      std::string key = trim(line.substr(0, sp));
      std::string val = trim(line.substr(sp + 1));
      if (!key.empty()) r.options[key] = val;
    }
  }

  r.ok = true;
  return r;
}

const Distribution* find_dist(const RepoConf& conf, const std::string& codename) {
  for (const auto& d : conf.distributions) {
    if (d.codename == codename) return &d;
  }
  return nullptr;
}

const Distribution* find_dist_for_repo(const RepoConf& conf, const Repo& repo) {
  if (auto* d = find_dist(conf, repo.codename)) return d;
  if (conf.distributions.size() == 1) return &conf.distributions.front();
  return nullptr;
}

fs::path effective_base(const Repo& repo, const RepoConf& conf) {
  auto it = conf.options.find("basedir");
  if (it != conf.options.end() && !it->second.empty()) return fs::path(it->second);
  return repo.base;
}

}  // namespace rpp
