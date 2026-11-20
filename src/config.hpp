#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace rpp {

struct ConfigLoad {
  Config cfg;
  fs::path path;
  bool exists = false;
  bool ok = false;
  std::string error;
};

fs::path expand_user(const std::string& p);
fs::path default_config_path();
std::vector<fs::path> config_candidates();

// Repo.conf may be empty ("use base/conf/distributions") or relative to base.
fs::path repo_conf_path(const Repo& repo);

Config default_config();
ConfigLoad load_config(const fs::path& explicit_path = {});
bool save_config(const Config& cfg, const fs::path& path, std::string* err);

bool validate_config(const Config& cfg, std::vector<std::string>* errs);

Repo* find_repo(Config& cfg, const std::string& name);
const Repo* find_repo(const Config& cfg, const std::string& name);
Repo& ensure_repo(Config& cfg, const std::string& name);

std::string config_to_string(const Config& cfg);

// Lightweight UI state cache (selection + view settings), separate from config.
struct UiState {
  std::string repo;
  std::string codename;
  std::string component;
  int tab = 0;
  bool show_unchanged = false;
};

fs::path default_state_path();
UiState load_state(const fs::path& path);
bool save_state(const UiState& st, const fs::path& path, std::string* err);

}  // namespace rpp
