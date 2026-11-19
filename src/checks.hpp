#pragma once

#include <string>
#include <vector>

#include "model.hpp"

namespace rpp {

struct CheckInput {
  const Artifacts* art = nullptr;
  const Policy* policy = nullptr;
  std::vector<std::string> target_archs;  // binary architectures, may contain "source"
  std::string codename;                   // for .dsc Distribution comparison
};

std::vector<Issue> run_checks(const CheckInput& in);

bool has_blocking(const std::vector<Issue>& issues);
int count_issues(const std::vector<Issue>& issues, Severity sev);

}  // namespace rpp
