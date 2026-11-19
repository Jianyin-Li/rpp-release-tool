#pragma once

#include <string>

#include "model.hpp"

namespace rpp {

struct AppOptions {
  Config cfg;
  fs::path cfg_path;
  bool cfg_exists = false;
  std::string cfg_error;
  std::string repo_name;
  std::string codename;
  std::string component;
  fs::path artifacts;
  bool check_only = false;
};

int run_tui(AppOptions opts);

}  // namespace rpp
