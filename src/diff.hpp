#pragma once

#include <set>
#include <string>
#include <vector>

#include "model.hpp"
#include "repo_state.hpp"

namespace rpp {

struct DiffInput {
  const Artifacts* art = nullptr;
  const RepoState* state = nullptr;  // may be null or !ok
  std::string codename;
  std::string component;
  std::vector<std::string> archs;  // binary architectures, may include "source"
};

DiffTree build_diff(const DiffInput& in);

struct FoldPolicy {
  int unchanged_fold = 15;      // -1 never fold, 0 hide unchanged
  bool show_unchanged = true;
  std::string detail = "normal";  // summary | normal | full
  std::set<std::string>* collapsed = nullptr;  // node paths (a/b/c) collapsed by the user
};

struct DiffRow {
  int depth = 0;
  const ChangeNode* node = nullptr;
  bool fold = false;
  int fold_count = 0;
};

struct NodeStats {
  int changed = 0;
  int total = 0;
};

NodeStats node_stats(const ChangeNode& n);
std::vector<DiffRow> flatten_diff(const DiffTree& tree, const FoldPolicy& pol,
                                  bool force_show_unchanged = false);
std::vector<std::string> render_diff_text(const DiffTree& tree, const FoldPolicy& pol);

const char* change_symbol(Change c);
int change_rank(Change c);
Change max_change(Change a, Change b);
std::string human_size(std::uint64_t bytes);

}  // namespace rpp
