#include "diff.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "exec.hpp"
#include "i18n.hpp"
#include "scan.hpp"

namespace rpp {
namespace {

ChangeNode* child_of(ChangeNode& parent, ChangeNode::Kind k, const std::string& name) {
  for (auto& c : parent.children) {
    if (c.kind == k && c.name == name) return &c;
  }
  ChangeNode n;
  n.kind = k;
  n.name = name;
  parent.children.push_back(std::move(n));
  return &parent.children.back();
}

Change file_change(const RepoState* st, const std::string& name, std::uint64_t size,
                   const std::string& sha256) {
  if (!st || !st->ok) return Change::Added;
  auto it = st->pool.find(name);
  if (it == st->pool.end()) return Change::Added;
  const PoolFile& pf = it->second;
  if (!sha256.empty() && !pf.sha256.empty()) {
    return sha256 == pf.sha256 ? Change::Unchanged : Change::Modified;
  }
  if (size != 0 && pf.size != 0) {
    return size == pf.size ? Change::Unchanged : Change::Modified;
  }
  return Change::Modified;
}

Change index_change(const RepoState* st, const std::string& pkg, const std::string& arch,
                    const std::string& version) {
  if (!st || !st->ok) return Change::Added;
  auto it = st->index.find(pkg + "|" + arch);
  if (it == st->index.end()) return Change::Added;
  return it->second == version ? Change::Unchanged : Change::Added;
}

void assign_index_node(ChangeNode& pkg_node, const std::string& pkg, const std::string& arch,
                       const std::string& version, Change c) {
  auto* idx = child_of(pkg_node, ChangeNode::Kind::Index, pkg + "|" + arch);
  idx->change = c;
  idx->meta = version;
  idx->name = pkg;
}

void recount(const ChangeNode& n, DiffTree& tree) {
  if (n.kind == ChangeNode::Kind::File) {
    switch (n.change) {
      case Change::Added: ++tree.added; break;
      case Change::Modified: ++tree.modified; break;
      case Change::Unchanged: ++tree.unchanged; break;
      default: break;
    }
  }
  for (const auto& c : n.children) recount(c, tree);
}

int arch_rank(const std::string& arch, const std::vector<std::string>& order) {
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (order[i] == arch) return static_cast<int>(i);
  }
  if (arch == "all") return static_cast<int>(order.size()) + 1;
  if (arch == "source") return static_cast<int>(order.size()) + 2;
  return static_cast<int>(order.size()) + 3;
}

void sort_tree(ChangeNode& n, const std::vector<std::string>& arch_order) {
  if (n.kind == ChangeNode::Kind::Codename || n.kind == ChangeNode::Kind::Component) {
    std::stable_sort(n.children.begin(), n.children.end(), [&](const ChangeNode& a, const ChangeNode& b) {
      return arch_rank(a.name, arch_order) < arch_rank(b.name, arch_order);
    });
  } else if (n.kind == ChangeNode::Kind::Package || n.kind == ChangeNode::Kind::File ||
             n.kind == ChangeNode::Kind::Index) {
    std::stable_sort(n.children.begin(), n.children.end(),
                     [](const ChangeNode& a, const ChangeNode& b) { return a.name < b.name; });
  }
  for (auto& c : n.children) sort_tree(c, arch_order);
}

}  // namespace

const char* change_symbol(Change c) {
  switch (c) {
    case Change::Added: return "+";
    case Change::Modified: return "*";
    case Change::Deleted: return "-";
    default: return " ";
  }
}

int change_rank(Change c) {
  switch (c) {
    case Change::Deleted: return 4;
    case Change::Modified: return 2;
    case Change::Added: return 1;
    default: return 0;
  }
}

Change max_change(Change a, Change b) { return change_rank(a) >= change_rank(b) ? a : b; }

std::string human_size(std::uint64_t bytes) {
  static const char* unit[] = {"B", "KiB", "MiB", "GiB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 3) {
    v /= 1024.0;
    ++u;
  }
  std::string out;
  if (u == 0) out = std::to_string(bytes) + " B";
  else {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f %s", v, unit[u]);
    out = buf;
  }
  return out;
}

DiffTree build_diff(const DiffInput& in) {
  DiffTree tree;
  const Artifacts& a = *in.art;

  ChangeNode codename;
  codename.kind = ChangeNode::Kind::Codename;
  codename.name = in.codename;
  ChangeNode* component = child_of(codename, ChangeNode::Kind::Component, in.component);
  component->meta = "";

  // --- source -------------------------------------------------------------
  for (const auto& d : a.dscs) {
    auto src = dsc_source_name(d);
    if (src.empty()) continue;
    auto version = d.field("Version");
    auto* arch_node = child_of(*component, ChangeNode::Kind::Arch, "source");
    auto* pkg_node = child_of(*arch_node, ChangeNode::Kind::Package, src);
    pkg_node->meta = version;

    // The .dsc file itself is published as well.
    fs::path dsc_path = d.path;
    auto* f = child_of(*pkg_node, ChangeNode::Kind::File, dsc_path.filename().string());
    f->change = file_change(in.state, dsc_path.filename().string(), file_size_of(dsc_path),
                            sha256_file(dsc_path));
    f->meta = human_size(file_size_of(dsc_path));

    for (const auto& fe : d.files) {
      auto* n = child_of(*pkg_node, ChangeNode::Kind::File, fe.name);
      std::string sha = fe.sha256;
      if (sha.empty()) sha = sha256_file(dsc_path.parent_path() / fe.name);
      n->change = file_change(in.state, fe.name, fe.size, sha);
      n->meta = human_size(fe.size);
    }

    auto ic = index_change(in.state, src, "source", version);
    assign_index_node(*pkg_node, src, "source", version, ic);
    pkg_node->change = max_change(pkg_node->change, ic);
    for (const auto& c : pkg_node->children) pkg_node->change = max_change(pkg_node->change, c.change);
  }

  // --- binaries -----------------------------------------------------------
  for (const auto& d : a.debs) {
    if (d.package.empty()) continue;
    auto* arch_node = child_of(*component, ChangeNode::Kind::Arch, d.arch.empty() ? "unknown" : d.arch);
    auto* pkg_node = child_of(*arch_node, ChangeNode::Kind::Package, d.package);
    pkg_node->meta = d.version;

    auto name = d.path.filename().string();
    auto* f = child_of(*pkg_node, ChangeNode::Kind::File, name);
    f->change = file_change(in.state, name, file_size_of(d.path), sha256_file(d.path));
    f->meta = human_size(file_size_of(d.path));

    auto ic = index_change(in.state, d.package, d.arch, d.version);
    assign_index_node(*pkg_node, d.package, d.arch, d.version, ic);
    pkg_node->change = max_change(pkg_node->change, ic);
    for (const auto& c : pkg_node->children) pkg_node->change = max_change(pkg_node->change, c.change);
  }

  if (component->children.empty()) return tree;

  for (auto& ac : component->children) {
    if (ac.kind != ChangeNode::Kind::Arch) continue;
    for (auto& pc : ac.children) {
      if (pc.kind != ChangeNode::Kind::Package) continue;
      ac.change = max_change(ac.change, pc.change);
    }
    component->change = max_change(component->change, ac.change);
  }
  codename.change = component->change;

  sort_tree(codename, in.archs);
  recount(codename, tree);
  tree.roots.push_back(std::move(codename));
  return tree;
}

NodeStats node_stats(const ChangeNode& n) {
  NodeStats s;
  if (n.kind == ChangeNode::Kind::File) {
    ++s.total;
    if (n.change != Change::Unchanged) ++s.changed;
    return s;
  }
  if (n.kind == ChangeNode::Kind::Index) return s;
  for (const auto& c : n.children) {
    auto cs = node_stats(c);
    s.changed += cs.changed;
    s.total += cs.total;
  }
  return s;
}

namespace {

bool child_visible(const ChangeNode& c, const FoldPolicy& pol) {
  if (c.kind == ChangeNode::Kind::Index) return pol.detail == "full";
  return true;
}

bool has_visible_change(const ChangeNode& n, const FoldPolicy& pol) {
  if (n.change != Change::Unchanged) return true;
  for (const auto& c : n.children) {
    if (has_visible_change(c, pol)) return true;
  }
  return false;
}

void walk(const ChangeNode& n, int depth, const std::string& path, const FoldPolicy& pol,
          bool force, std::vector<DiffRow>& rows) {
  if (pol.detail == "summary" && n.kind == ChangeNode::Kind::Package) return;
  if (pol.collapsed && pol.collapsed->count(path)) return;

  std::vector<const ChangeNode*> changed, unchanged;
  for (const auto& c : n.children) {
    if (!child_visible(c, pol)) continue;
    if (c.change == Change::Unchanged) unchanged.push_back(&c);
    else changed.push_back(&c);
  }

  for (const auto* c : changed) {
    rows.push_back(DiffRow{depth + 1, c, false, 0});
    if (c->kind != ChangeNode::Kind::File && c->kind != ChangeNode::Kind::Index) {
      walk(*c, depth + 1, path + "/" + c->name, pol, force, rows);
    }
  }

  if ((!pol.show_unchanged && !force) || (pol.unchanged_fold == 0 && !force)) return;
  if (!force && pol.unchanged_fold >= 0 &&
      static_cast<int>(unchanged.size()) > pol.unchanged_fold) {
    rows.push_back(DiffRow{depth + 1, nullptr, true, static_cast<int>(unchanged.size())});
    return;
  }
  for (const auto* c : unchanged) {
    rows.push_back(DiffRow{depth + 1, c, false, 0});
    // unchanged subtrees are auto-collapsed: never recursed into
  }
}

}  // namespace

std::vector<DiffRow> flatten_diff(const DiffTree& tree, const FoldPolicy& pol,
                                  bool force_show_unchanged) {
  std::vector<DiffRow> rows;
  for (const auto& root : tree.roots) {
    if (!force_show_unchanged && !has_visible_change(root, pol)) continue;
    rows.push_back(DiffRow{0, &root, false, 0});
    walk(root, 0, root.name, pol, force_show_unchanged, rows);
  }
  return rows;
}

std::vector<std::string> render_diff_text(const DiffTree& tree, const FoldPolicy& pol) {
  std::vector<std::string> out;
  auto rows = flatten_diff(tree, pol);
  for (const auto& row : rows) {
    std::string indent(static_cast<std::size_t>(row.depth) * 2, ' ');
    if (row.fold) {
      out.push_back(indent + trf("diff.fold", row.fold_count));
      continue;
    }
    const ChangeNode& n = *row.node;
    bool leaf = n.kind == ChangeNode::Kind::File || n.kind == ChangeNode::Kind::Index;
    std::string line = indent;
    if (leaf) {
      line += change_symbol(n.change);
      line += ' ';
    } else if (n.change == Change::Unchanged) {
      line += "· ";
    } else {
      line += "  ";
    }
    if (n.kind == ChangeNode::Kind::Index) {
      line += n.change == Change::Unchanged ? trf("diff.index.same", n.name, n.meta)
                                            : trf("diff.index.new", n.name, n.meta);
    } else if (n.kind == ChangeNode::Kind::Package) {
      auto st = node_stats(n);
      line += n.name;
      if (!n.meta.empty()) line += "  " + n.meta;
      line += "  " + trf("diff.pkg_stats", st.changed, st.total);
    } else {
      line += n.name;
      if (n.kind != ChangeNode::Kind::File) line += "/";
      if (!n.meta.empty()) line += "  " + n.meta;
    }
    out.push_back(std::move(line));
  }
  return out;
}

}  // namespace rpp
