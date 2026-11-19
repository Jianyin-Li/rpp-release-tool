#pragma once

#include <map>
#include <string>
#include <vector>

#include "model.hpp"

namespace rpp {

struct PoolFile {
  std::string name;
  std::string pool_path;
  std::string sha256;
  std::uint64_t size = 0;
};

struct RepoState {
  std::map<std::string, std::string> index;  // "pkg|arch" -> version ("arch" may be "source")
  std::map<std::string, PoolFile> pool;      // basename -> file
  bool ok = false;
  std::string error;
};

RepoState load_repo_state(const fs::path& base, const std::string& codename,
                          const std::string& component, const std::vector<std::string>& archs);

}  // namespace rpp
