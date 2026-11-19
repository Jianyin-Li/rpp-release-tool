#pragma once

#include <functional>
#include <string>
#include <vector>

#include "diff.hpp"
#include "model.hpp"
#include "reprepro_conf.hpp"

namespace rpp {

struct PreflightInput {
  Repo repo;
  RepoConf conf;
  const Artifacts* art = nullptr;
  const Policy* policy = nullptr;
  std::vector<std::string> target_archs;
};

std::vector<Issue> run_preflight(const PreflightInput& in);

std::vector<Command> build_commands(const Repo& repo, const fs::path& base, const std::string& codename,
                                    const std::string& component, const Artifacts& art,
                                    const DiffTree& diff, bool allow_replace);

struct ExecResult {
  std::vector<CommandResult> results;
  bool all_ok = true;
};

ExecResult execute_commands(const std::vector<Command>& cmds, const fs::path& cwd,
                            const std::function<void(std::string_view)>& sink);

std::vector<Issue> verify_publish(const fs::path& base, const std::string& codename,
                                  const std::string& component, const Artifacts& art,
                                  const std::vector<std::string>& archs);

}  // namespace rpp
