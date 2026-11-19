#pragma once

#include <map>
#include <string>
#include <vector>

#include "model.hpp"

namespace rpp {

struct RepoConf {
  std::vector<Distribution> distributions;
  std::map<std::string, std::string> options;
  std::string error;
  bool ok = false;
};

fs::path resolve_distributions_path(const fs::path& conf);
RepoConf parse_repo_conf(const fs::path& distributions_path);
const Distribution* find_dist(const RepoConf& conf, const std::string& codename);
const Distribution* find_dist_for_repo(const RepoConf& conf, const Repo& repo);
fs::path effective_base(const Repo& repo, const RepoConf& conf);

}  // namespace rpp
