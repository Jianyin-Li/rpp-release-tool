#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <toml++/toml.hpp>

#include "i18n.hpp"

namespace rpp {
namespace {

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

std::vector<std::string> str_array(const toml::node* n) {
  std::vector<std::string> out;
  if (!n) return out;
  if (auto* arr = n->as_array()) {
    for (auto&& e : *arr) {
      if (auto s = e.value<std::string>()) out.push_back(*s);
    }
  } else if (auto s = n->value<std::string>()) {
    std::istringstream is(*s);
    std::string item;
    while (is >> item) out.push_back(item);
  }
  return out;
}

std::string str(const toml::table& t, std::string_view k, std::string def = {}) {
  if (auto v = t[k].value<std::string>()) return *v;
  return def;
}

bool boolean(const toml::table& t, std::string_view k, bool def) {
  if (auto v = t[k].value<bool>()) return *v;
  return def;
}

int integer(const toml::table& t, std::string_view k, int def) {
  if (auto v = t[k].value<int64_t>()) return static_cast<int>(*v);
  return def;
}

std::string quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c);
    }
  }
  out += '"';
  return out;
}

std::string arr(const std::vector<std::string>& v) {
  std::string out = "[";
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) out += ", ";
    out += quote(v[i]);
  }
  out += "]";
  return out;
}

}  // namespace

fs::path expand_user(const std::string& p) {
  if (p.empty()) return {};
  if (p[0] == '~' && (p.size() == 1 || p[1] == '/')) {
    std::string home = env("HOME");
    if (home.empty()) return fs::path(p);
    return fs::path(home + p.substr(1));
  }
  return fs::path(p);
}

fs::path default_config_path() {
  std::string xdg = env("XDG_CONFIG_HOME");
  fs::path base = xdg.empty() ? expand_user("~/.config") : fs::path(xdg);
  return base / "rpp-release-tool" / "config.toml";
}

std::vector<fs::path> config_candidates() {
  std::vector<fs::path> out;
  if (auto e = env("RPP_RELEASE_CONFIG"); !e.empty()) out.push_back(expand_user(e));
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  if (!ec) {
    for (const char* name : {"rpp-release.toml", "rpp-release-tool.toml", ".rpp-release.toml"}) {
      out.push_back(cwd / name);
    }
  }
  out.push_back(default_config_path());
  return out;
}

Config default_config() {
  Config c;
  std::error_code ec;
  c.artifacts = fs::current_path(ec);
  c.ui.language = "auto";
  return c;
}

ConfigLoad load_config(const fs::path& explicit_path) {
  ConfigLoad r;
  r.cfg = default_config();

  std::vector<fs::path> candidates;
  if (!explicit_path.empty()) {
    candidates.push_back(explicit_path);
  } else {
    candidates = config_candidates();
  }

  for (const auto& p : candidates) {
    std::error_code ec;
    if (!fs::exists(p, ec)) continue;
    r.path = p;
    r.exists = true;
    break;
  }
  if (!r.exists) {
    r.path = explicit_path.empty() ? default_config_path() : explicit_path;
    r.ok = false;
    r.error.clear();
    return r;
  }

  try {
    toml::table parsed = toml::parse_file(r.path.string());
    if (parsed.empty() && !fs::exists(r.path)) {
      r.error = trf("err.cannot_read", r.path.string(), "");
      return r;
    }
    const toml::table& t = parsed;

    Config c = default_config();
    c.version = integer(t, "version", 1);
    c.default_repo = str(t, "default_repo");

    if (const toml::table* paths = t["paths"].as_table()) {
      if (auto a = (*paths)["artifacts"].value<std::string>()) c.artifacts = expand_user(*a);
    }

    if (const toml::table* pol = t["policy"].as_table()) {
      c.policy.require_both = boolean(*pol, "require_both", c.policy.require_both);
      c.policy.missing_arch = str(*pol, "missing_arch", c.policy.missing_arch);
      c.policy.buildinfo = str(*pol, "buildinfo", c.policy.buildinfo);
      c.policy.sha256 = str(*pol, "sha256", c.policy.sha256);
      c.policy.allow_replace = boolean(*pol, "allow_replace", c.policy.allow_replace);
    }

    if (const toml::table* ui = t["ui"].as_table()) {
      c.ui.detail = str(*ui, "detail", c.ui.detail);
      c.ui.unchanged_fold = integer(*ui, "unchanged_fold", c.ui.unchanged_fold);
      c.ui.language = str(*ui, "language", c.ui.language);
      c.ui.show_unchanged = boolean(*ui, "show_unchanged", c.ui.show_unchanged);
    }

    if (const toml::array* repos = t["repo"].as_array()) {
      for (auto&& node : *repos) {
        const toml::table* rt = node.as_table();
        if (!rt) continue;
        Repo rp;
        rp.name = str(*rt, "name");
        rp.base = expand_user(str(*rt, "base"));
        rp.conf = expand_user(str(*rt, "conf", (rp.base / "conf" / "distributions").string()));
        rp.codename = str(*rt, "codename");
        rp.component = str(*rt, "component", "main");
        rp.architectures = str_array(rt->get("architectures"));
        rp.incoming = expand_user(str(*rt, "incoming"));
        if (rp.name.empty() && !rp.base.empty()) rp.name = rp.base.filename().string();
        c.repos.push_back(std::move(rp));
      }
    }

    r.cfg = std::move(c);
    r.ok = true;
    return r;
  } catch (const toml::parse_error& e) {
    std::ostringstream os;
    os << e.description() << " (" << trf("common.line", e.source().begin.line) << ")";
    r.error = os.str();
    return r;
  } catch (const std::exception& e) {
    r.error = e.what();
    return r;
  }
}

bool save_config(const Config& cfg, const fs::path& path, std::string* err) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
      if (err) *err = trf("err.cannot_write", path.parent_path().string(), ec.message());
      return false;
    }
  }
  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      if (err) *err = trf("err.cannot_write", tmp.string(), std::strerror(errno));
      return false;
    }
    out << config_to_string(cfg);
    out.flush();
    if (!out) {
      if (err) *err = trf("err.cannot_write", tmp.string(), "write failed");
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp);
    if (err) *err = trf("err.cannot_write", path.string(), ec.message());
    return false;
  }
  return true;
}

bool validate_config(const Config& cfg, std::vector<std::string>* errs) {
  std::vector<std::string> local;
  if (cfg.repos.empty()) local.push_back(tr("config.need_repo"));
  for (const auto& r : cfg.repos) {
    if (r.name.empty()) local.push_back(trf("config.need_field", tr("field.name")));
    if (r.base.empty()) local.push_back(trf("config.need_field", tr("field.base")));
    if (r.conf.empty()) local.push_back(trf("config.need_field", tr("field.conf")));
    if (r.codename.empty()) local.push_back(trf("config.need_field", tr("field.codename")));
    if (r.component.empty()) local.push_back(trf("config.need_field", tr("field.component")));
  }
  if (errs) *errs = local;
  return local.empty();
}

Repo* find_repo(Config& cfg, const std::string& name) {
  for (auto& r : cfg.repos) {
    if (r.name == name) return &r;
  }
  return nullptr;
}

const Repo* find_repo(const Config& cfg, const std::string& name) {
  for (const auto& r : cfg.repos) {
    if (r.name == name) return &r;
  }
  return nullptr;
}

Repo& ensure_repo(Config& cfg, const std::string& name) {
  if (auto* r = find_repo(cfg, name)) return *r;
  Repo r;
  r.name = name.empty() ? "repo" : name;
  cfg.repos.push_back(std::move(r));
  return cfg.repos.back();
}

std::string config_to_string(const Config& cfg) {
  std::ostringstream os;
  os << "# rpp-release-tool configuration\n";
  os << "# generated/updated by the tool; hand editing is fine\n\n";
  os << "version = " << cfg.version << "\n";
  if (!cfg.default_repo.empty()) os << "default_repo = " << quote(cfg.default_repo) << "\n";
  os << "\n[paths]\n";
  os << "artifacts = " << quote(cfg.artifacts.string()) << "\n";
  os << "\n[policy]\n";
  os << "require_both = " << (cfg.policy.require_both ? "true" : "false") << "\n";
  os << "missing_arch = " << quote(cfg.policy.missing_arch) << "\n";
  os << "buildinfo = " << quote(cfg.policy.buildinfo) << "\n";
  os << "sha256 = " << quote(cfg.policy.sha256) << "\n";
  os << "allow_replace = " << (cfg.policy.allow_replace ? "true" : "false") << "\n";
  os << "\n[ui]\n";
  os << "detail = " << quote(cfg.ui.detail) << "\n";
  os << "unchanged_fold = " << cfg.ui.unchanged_fold << "\n";
  os << "language = " << quote(cfg.ui.language) << "\n";
  os << "show_unchanged = " << (cfg.ui.show_unchanged ? "true" : "false") << "\n";

  for (const auto& r : cfg.repos) {
    os << "\n[[repo]]\n";
    os << "name = " << quote(r.name) << "\n";
    os << "base = " << quote(r.base.string()) << "\n";
    os << "conf = " << quote(r.conf.string()) << "\n";
    os << "codename = " << quote(r.codename) << "\n";
    os << "component = " << quote(r.component) << "\n";
    os << "architectures = " << arr(r.architectures) << "\n";
    if (!r.incoming.empty()) os << "incoming = " << quote(r.incoming.string()) << "\n";
  }
  return os.str();
}

fs::path default_state_path() {
  if (auto e = env("RPP_RELEASE_STATE"); !e.empty()) return expand_user(e);
  std::string xdg = env("XDG_CACHE_HOME");
  fs::path base = xdg.empty() ? expand_user("~/.cache") : fs::path(xdg);
  return base / "rpp-release-tool" / "state.toml";
}

UiState load_state(const fs::path& path) {
  UiState st;
  std::error_code ec;
  if (path.empty() || !fs::exists(path, ec)) return st;
  try {
    toml::table t = toml::parse_file(path.string());
    if (auto v = t["repo"].value<std::string>()) st.repo = *v;
    if (auto v = t["codename"].value<std::string>()) st.codename = *v;
    if (auto v = t["component"].value<std::string>()) st.component = *v;
    if (auto v = t["tab"].value<int64_t>()) st.tab = static_cast<int>(*v);
    if (auto v = t["show_unchanged"].value<bool>()) st.show_unchanged = *v;
  } catch (const toml::parse_error&) {
    // A corrupt cache must never prevent startup; fall back to defaults.
  }
  return st;
}

bool save_state(const UiState& st, const fs::path& path, std::string* err) {
  if (path.empty()) {
    if (err) *err = tr("err.state_path");
    return false;
  }
  std::error_code ec;
  if (!path.parent_path().empty()) {
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
      if (err) *err = trf("err.cannot_write", path.parent_path().string(), ec.message());
      return false;
    }
  }
  std::ostringstream os;
  os << "# rpp-release-tool UI state cache; safe to delete\n";
  os << "repo = " << quote(st.repo) << "\n";
  os << "codename = " << quote(st.codename) << "\n";
  os << "component = " << quote(st.component) << "\n";
  os << "tab = " << st.tab << "\n";
  os << "show_unchanged = " << (st.show_unchanged ? "true" : "false") << "\n";

  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      if (err) *err = trf("err.cannot_write", tmp.string(), std::strerror(errno));
      return false;
    }
    out << os.str();
    out.flush();
    if (!out) {
      if (err) *err = trf("err.cannot_write", tmp.string(), "write failed");
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp);
    if (err) *err = trf("err.cannot_write", path.string(), ec.message());
    return false;
  }
  return true;
}

}  // namespace rpp
