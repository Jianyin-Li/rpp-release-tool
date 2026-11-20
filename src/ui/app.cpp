#include "ui/app.hpp"

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>
#include <sstream>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "checks.hpp"
#include "config.hpp"
#include "diff.hpp"
#include "exec.hpp"
#include "i18n.hpp"
#include "publish.hpp"
#include "repo_state.hpp"
#include "reprepro_conf.hpp"
#include "scan.hpp"
#include "stage.hpp"
#include "version.hpp"

namespace rpp {
namespace {

using namespace ftxui;

enum class Screen { Home, Config, Repos, Sources, Browse, Maint, Report, Diff, Confirm, Verify, Settings };
enum class ReposPurpose { Upload, Maintain };

struct Field {
  enum class Type { Text, Int, Bool, Choice } type = Type::Text;
  std::string key;
  std::string label_key;
  std::string value;
  std::vector<std::string> choices;
};

struct MaintItem {
  enum class Kind { Binary, Source } kind = Kind::Binary;
  std::string name;
  std::string version;
  std::vector<std::string> archs;
  bool selected = false;
};

enum class SetStatus { Upgrade = 0, New = 1, Downgrade = 2, Same = 3 };

struct SrcRow {
  enum class Type { Group, Set } type = Type::Group;
  int group = 0;
  int set = 0;
};

struct MaintRow {
  enum class Type { Group, Item } type = Type::Group;
  int group = 0;
  int item = 0;
};

struct RepoRow {
  enum class Kind { Repo, Codename, Component } kind = Kind::Repo;
  std::string text;
  std::string meta;
  int repo = 0;
  std::string codename;
  std::string component;
  bool selectable = false;
};

struct App {
  AppOptions opts;
  Config cfg;
  fs::path cfg_path;
  bool cfg_exists = true;
  std::string cfg_error;
  Screen screen = Screen::Repos;
  Screen return_screen = Screen::Report;
  int home_sel = 0;
  ReposPurpose repos_purpose = ReposPurpose::Upload;

  int repo_idx = 0;
  std::string codename;
  std::string component;
  RepoConf conf;

  std::vector<RepoRow> repo_rows;
  int repo_sel = 0;

  Artifacts art;
  std::vector<Issue> issues;
  DiffTree diff;
  std::vector<Command> cmds;
  bool blocked = false;
  bool needs_ack = false;
  bool replace_needed = false;
  std::string scene_warning;
  int issue_scroll = 0;

  // Local package-set picker.  `source_dir` is the user's build directory;
  // `art` points at the staged snapshot once `stage_dir` is set.
  fs::path source_dir;
  fs::path stage_dir;
  std::vector<fs::path> stale_stages;
  Artifacts source_art;
  std::vector<PackageSet> package_sets;
  std::vector<bool> set_selected;
  std::vector<SetStatus> set_status;
  std::vector<std::string> set_repo_ver;
  std::vector<SrcRow> src_rows;
  bool src_group_open[4] = {true, true, true, false};
  int src_sel = 0;
  std::string source_note;
  Screen sources_return = Screen::Repos;
  fs::path browse_dir;
  std::vector<fs::path> browse_dirs;
  std::vector<int> browse_counts;
  int browse_sel = 0;
  std::string browse_filter;
  std::string browse_note;
  bool detail_open = false;
  std::string detail_title;
  std::vector<std::string> detail_lines;
  int detail_scroll = 0;

  std::vector<MaintItem> maint_items;
  std::vector<MaintRow> maint_rows;
  bool maint_group_open[2] = {true, true};
  int maint_sel = 0;
  std::string maint_note;
  bool maint_confirm = false;
  std::string maint_action;  // delete | export | clean
  std::vector<Command> maint_cmds;
  std::string maint_typed;
  std::string maint_error;

  std::vector<DiffRow> rows;
  int row_sel = 0;
  bool show_all_unchanged = false;
  std::set<std::string> collapsed;

  bool ack_scene = false;
  std::string typed;
  std::string confirm_error;

  ExecResult exec;
  std::vector<Issue> verify_issues;
  bool published = false;
  std::string exec_log;

  std::vector<Field> fields;
  int focus = 0;
  int edit_repo = 0;
  int tab = 0;

  fs::path state_path;
  bool quit_dialog = false;
  int quit_sel = 1;  // default to "No" so Enter does not accidentally quit
  std::string quit_error;
  bool help_open = false;
  int help_scroll = 0;

  std::string toast;
  int exit_code = 0;
};

constexpr int kListWindow = 16;

std::string human_issue_mark(Severity s) {
  switch (s) {
    case Severity::Block: return "✗";
    case Severity::Warn: return "!";
    default: return "✓";
  }
}

Color sev_color(Severity s) {
  switch (s) {
    case Severity::Block: return Color::Red;
    case Severity::Warn: return Color::Yellow;
    default: return Color::Green;
  }
}

Color change_color(Change c) {
  switch (c) {
    case Change::Added: return Color::Green;
    case Change::Modified: return Color::Yellow;
    case Change::Deleted: return Color::Red;
    default: return Color::GrayDark;
  }
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) out += sep;
    out += v[i];
  }
  return out;
}

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  std::string w;
  while (is >> w) out.push_back(w);
  return out;
}

Repo& current_repo(App& a) {
  if (a.cfg.repos.empty()) a.cfg.repos.push_back(Repo{});
  a.repo_idx = std::clamp(a.repo_idx, 0, static_cast<int>(a.cfg.repos.size()) - 1);
  return a.cfg.repos[static_cast<std::size_t>(a.repo_idx)];
}

void set_language(App& a, Lang l) {
  set_lang(l);
  a.cfg.ui.language = lang_code(l);
}

void load_conf(App& a) {
  a.conf = parse_repo_conf(repo_conf_path(current_repo(a)));
}

void ensure_selection(App& a) {
  if (a.cfg.repos.empty()) return;
  current_repo(a);
  Repo& r = current_repo(a);
  load_conf(a);

  if (!a.opts.codename.empty()) a.codename = a.opts.codename;
  if (a.codename.empty()) a.codename = r.codename;
  if (a.codename.empty() && !a.conf.distributions.empty()) a.codename = a.conf.distributions.front().codename;
  if (a.codename.empty()) a.codename = r.codename;

  const Distribution* d = find_dist(a.conf, a.codename);
  if (!a.opts.component.empty()) a.component = a.opts.component;
  if (a.component.empty()) a.component = r.component;
  if (a.component.empty() && d && !d->components.empty()) a.component = d->components.front();
  if (a.component.empty()) a.component = "main";
}

std::vector<std::string> selection_archs(App& a) {
  Repo& r = current_repo(a);
  const Distribution* d = find_dist(a.conf, a.codename);
  std::vector<std::string> out;
  if (d) out = d->architectures;
  if (out.empty()) out = r.architectures;
  return out;
}

void rebuild_repo_rows(App& a) {
  a.repo_rows.clear();
  for (std::size_t i = 0; i < a.cfg.repos.size(); ++i) {
    const Repo& r = a.cfg.repos[i];
    RepoConf rc = parse_repo_conf(repo_conf_path(r));
    RepoRow rr;
    rr.kind = RepoRow::Kind::Repo;
    rr.repo = static_cast<int>(i);
    rr.text = r.name;
    rr.meta = r.base.string();
    a.repo_rows.push_back(rr);

    if (!rc.ok || rc.distributions.empty()) {
      RepoRow cn;
      cn.kind = RepoRow::Kind::Codename;
      cn.repo = static_cast<int>(i);
      cn.text = r.codename.empty() ? "?" : r.codename;
      cn.meta = rc.ok ? "" : rc.error;
      a.repo_rows.push_back(cn);
      RepoRow cp;
      cp.kind = RepoRow::Kind::Component;
      cp.repo = static_cast<int>(i);
      cp.codename = cn.text;
      cp.component = r.component.empty() ? "main" : r.component;
      cp.text = cp.component;
      cp.selectable = true;
      a.repo_rows.push_back(cp);
      continue;
    }
    for (const auto& d : rc.distributions) {
      RepoRow cn;
      cn.kind = RepoRow::Kind::Codename;
      cn.repo = static_cast<int>(i);
      cn.text = d.codename;
      cn.meta = join(d.architectures, " ");
      a.repo_rows.push_back(cn);
      std::vector<std::string> comps = d.components;
      if (comps.empty()) comps.push_back(r.component.empty() ? "main" : r.component);
      for (const auto& c : comps) {
        RepoRow cp;
        cp.kind = RepoRow::Kind::Component;
        cp.repo = static_cast<int>(i);
        cp.codename = d.codename;
        cp.component = c;
        cp.text = c;
        cp.meta = join(d.architectures, " ");
        cp.selectable = true;
        a.repo_rows.push_back(cp);
      }
    }
  }
  if (a.repo_sel >= static_cast<int>(a.repo_rows.size())) a.repo_sel = 0;
  while (a.repo_sel < static_cast<int>(a.repo_rows.size()) && !a.repo_rows[a.repo_sel].selectable) {
    ++a.repo_sel;
    if (a.repo_sel >= static_cast<int>(a.repo_rows.size())) {
      a.repo_sel = 0;
      break;
    }
  }
}

void evaluate_art(App& a) {
  ensure_selection(a);
  Repo& r = current_repo(a);

  CheckInput ci;
  ci.art = &a.art;
  ci.policy = &a.cfg.policy;
  ci.target_archs = selection_archs(a);
  ci.codename = a.codename;
  a.issues = run_checks(ci);

  PreflightInput pi;
  pi.repo = r;
  pi.conf = a.conf;
  pi.art = &a.art;
  pi.policy = &a.cfg.policy;
  pi.target_archs = selection_archs(a);
  auto pf = run_preflight(pi);
  a.issues.insert(a.issues.end(), pf.begin(), pf.end());

  fs::path base = effective_base(r, a.conf);
  std::vector<std::string> archs = selection_archs(a);
  RepoState st = load_repo_state(base, a.codename, a.component, archs);

  DiffInput di;
  di.art = &a.art;
  di.state = &st;
  di.codename = a.codename;
  di.component = a.component;
  di.archs = archs;
  a.diff = build_diff(di);

  FoldPolicy fp;
  fp.unchanged_fold = a.cfg.ui.unchanged_fold;
  fp.show_unchanged = a.cfg.ui.show_unchanged;
  fp.detail = a.cfg.ui.detail;
  fp.collapsed = &a.collapsed;
  a.rows = flatten_diff(a.diff, fp, a.show_all_unchanged);
  a.row_sel = std::min(a.row_sel, std::max(0, static_cast<int>(a.rows.size()) - 1));
  a.issue_scroll = 0;
  a.ack_scene = false;
  a.typed.clear();
  a.confirm_error.clear();
  a.published = false;
  a.exec = {};
  a.verify_issues.clear();

  a.blocked = has_blocking(a.issues);
  a.needs_ack = a.art.scene == Scene::BinaryOnly || a.art.scene == Scene::SourceOnly;
  a.replace_needed = a.diff.modified > 0;
  if (a.replace_needed && !a.cfg.policy.allow_replace) a.blocked = true;

  a.scene_warning.clear();
  if (a.art.scene == Scene::BinaryOnly) a.scene_warning = tr("confirm.binary_only");
  else if (a.art.scene == Scene::SourceOnly) a.scene_warning = tr("confirm.source_only");

  a.cmds = build_commands(r, base, a.codename, a.component, a.art, a.diff, a.cfg.policy.allow_replace);
}

void rescan(App& a) {
  if (!a.stage_dir.empty()) {
    a.art = scan_dir(a.stage_dir);
    evaluate_art(a);
    return;
  }
  a.art = scan_dir(a.source_dir.empty() ? a.cfg.artifacts : a.source_dir);
  evaluate_art(a);
}

bool is_dir(const fs::path& p) {
  std::error_code ec;
  return !p.empty() && fs::is_directory(p, ec);
}

// Rescan the user's build directory, classify each package set against the
// current repository state and rebuild the status tree.  This deliberately
// does not touch the staged snapshot used by the publish flow.
SetStatus classify_set(const RepoState& st, const Artifacts& art, const PackageSet& s,
                       const std::vector<std::string>& archs, std::string* repo_version) {
  bool missing = false;
  bool upgrade = false;
  bool downgrade = false;
  std::string found;

  auto check = [&](const std::string& pkg, const std::string& arch, const std::string& local_ver) {
    if (pkg.empty() || local_ver.empty()) return;
    if (!st.ok) {
      missing = true;
      return;
    }
    if (arch == "all") {
      bool any = false;
      for (const auto& a : archs) {
        if (a == "source" || a == "all") continue;
        auto it = st.index.find(pkg + "|" + a);
        if (it == st.index.end()) continue;
        any = true;
        if (found.empty()) found = it->second;
        int c = deb_version_compare(it->second, local_ver);
        if (c < 0) upgrade = true;
        else if (c > 0) downgrade = true;
        break;
      }
      if (!any) missing = true;
      return;
    }
    auto it = st.index.find(pkg + "|" + arch);
    if (it == st.index.end()) {
      missing = true;
      return;
    }
    if (found.empty()) found = it->second;
    int c = deb_version_compare(it->second, local_ver);
    if (c < 0) upgrade = true;
    else if (c > 0) downgrade = true;
  };

  for (std::size_t idx : s.dscs) {
    if (idx >= art.dscs.size()) continue;
    check(dsc_source_name(art.dscs[idx]), "source", art.dscs[idx].field("Version"));
  }
  for (std::size_t idx : s.debs) {
    if (idx >= art.debs.size()) continue;
    check(art.debs[idx].package, art.debs[idx].arch, art.debs[idx].version);
  }
  if (repo_version) *repo_version = found;
  if (missing) return SetStatus::New;
  if (upgrade) return SetStatus::Upgrade;
  if (downgrade) return SetStatus::Downgrade;
  return SetStatus::Same;
}

void rebuild_src_rows(App& a) {
  a.src_rows.clear();
  for (int g = 0; g < 4; ++g) {
    std::vector<int> members;
    for (std::size_t i = 0; i < a.package_sets.size(); ++i) {
      if (static_cast<int>(a.set_status[i]) == g) members.push_back(static_cast<int>(i));
    }
    if (members.empty()) continue;
    SrcRow row;
    row.type = SrcRow::Type::Group;
    row.group = g;
    a.src_rows.push_back(row);
    if (a.src_group_open[g]) {
      for (int i : members) {
        SrcRow sr;
        sr.type = SrcRow::Type::Set;
        sr.group = g;
        sr.set = i;
        a.src_rows.push_back(sr);
      }
    }
  }
  a.src_sel = std::min(a.src_sel, std::max(0, static_cast<int>(a.src_rows.size()) - 1));
}

int current_set(App& a) {
  if (a.src_sel < 0 || a.src_sel >= static_cast<int>(a.src_rows.size())) return -1;
  const SrcRow& r = a.src_rows[static_cast<std::size_t>(a.src_sel)];
  return r.type == SrcRow::Type::Set ? r.set : -1;
}

void reload_sources(App& a) {
  a.source_note.clear();
  a.source_dir = a.cfg.artifacts;
  a.package_sets.clear();
  a.set_selected.clear();
  a.set_status.clear();
  a.set_repo_ver.clear();
  a.source_art = Artifacts{};
  a.src_rows.clear();
  a.src_sel = 0;
  if (!is_dir(a.source_dir)) {
    a.source_note = a.source_dir.empty() ? tr("sources.missing_dir")
                                         : trf("sources.scan_failed", a.source_dir.string(),
                                               tr("sources.missing_dir"));
    return;
  }

  ensure_selection(a);
  Repo& repo = current_repo(a);
  RepoState st = load_repo_state(effective_base(repo, a.conf), a.codename, a.component,
                                selection_archs(a));

  a.source_art = scan_dir(a.source_dir);
  a.package_sets = group_artifacts(a.source_art);
  a.set_selected.assign(a.package_sets.size(), false);
  a.set_status.resize(a.package_sets.size(), SetStatus::Same);
  a.set_repo_ver.assign(a.package_sets.size(), "");
  for (std::size_t i = 0; i < a.package_sets.size(); ++i) {
    std::string repo_ver;
    a.set_status[i] = classify_set(st, a.source_art, a.package_sets[i], selection_archs(a),
                                   &repo_ver);
    a.set_repo_ver[i] = repo_ver;
    // Aptitude-like default: pre-mark only what actually does something.
    a.set_selected[i] = a.set_status[i] == SetStatus::Upgrade || a.set_status[i] == SetStatus::New;
  }
  rebuild_src_rows(a);
  if (!a.source_art.changes.empty()) a.source_note = tr("sources.changes_note");
  if (a.package_sets.empty()) a.source_note = tr("sources.empty");
}

void open_sources(App& a) {
  a.sources_return = (a.screen == Screen::Repos || a.screen == Screen::Browse)
                         ? Screen::Repos
                         : a.screen;
  reload_sources(a);
  a.screen = Screen::Sources;
  a.src_sel = 0;
  a.toast.clear();
}

void load_browse(App& a) {
  a.browse_dirs.clear();
  a.browse_counts.clear();
  a.browse_filter.clear();
  a.browse_note.clear();
  std::error_code ec;
  for (auto& e : fs::directory_iterator(a.browse_dir,
                                        fs::directory_options::skip_permission_denied, ec)) {
    std::error_code de;
    if (!e.is_directory(de)) continue;
    a.browse_dirs.push_back(e.path());
    int count = 0;
    std::error_code ne;
    for (auto& f : fs::directory_iterator(e.path(),
                                          fs::directory_options::skip_permission_denied, ne)) {
      std::error_code fe;
      if (!f.is_regular_file(fe)) continue;
      auto ext = f.path().extension().string();
      if (ext == ".deb" || ext == ".dsc" || ext == ".changes") ++count;
    }
    a.browse_counts.push_back(count);
  }
  if (ec) a.browse_note = trf("browse.error", a.browse_dir.string(), ec.message());
  std::sort(a.browse_dirs.begin(), a.browse_dirs.end());
  // Row 0 is the "use this directory" action, so Enter always activates the
  // highlighted row; the first real subdirectory follows it.
  a.browse_sel = a.browse_dirs.empty() ? 0 : 1;
}

void open_browse(App& a) {
  a.browse_dir = is_dir(a.cfg.artifacts) ? a.cfg.artifacts : fs::current_path();
  load_browse(a);
  a.screen = Screen::Browse;
  a.toast.clear();
}

std::string ascii_lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Jump to the first directory starting with the typed prefix (case
// insensitive).  Matches do not reset the filter, so more letters keep
// narrowing it like a file manager.
void browse_jump(App& a) {
  a.browse_note.clear();
  if (a.browse_filter.empty()) return;
  std::string f = ascii_lower(a.browse_filter);
  for (std::size_t i = 0; i < a.browse_dirs.size(); ++i) {
    if (ascii_lower(a.browse_dirs[i].filename().string()).rfind(f, 0) == 0) {
      a.browse_sel = static_cast<int>(i) + 1;
      return;
    }
  }
  a.browse_note = tr("browse.no_match");
}

void choose_dir(App& a, const fs::path& dir) {
  if (!a.stage_dir.empty()) a.stale_stages.push_back(a.stage_dir);
  a.stage_dir.clear();
  a.cfg.artifacts = dir;
  reload_sources(a);
  a.screen = Screen::Sources;
  a.toast = tr("browse.chosen");
}

// Take an immutable snapshot of the selected sets and continue with it.
void stage_selection(App& a) {
  Artifacts sel = select_artifacts(a.source_art, a.package_sets, a.set_selected);
  if (sel.debs.empty() && sel.dscs.empty()) {
    a.source_note = tr("stage.nothing");
    return;
  }
  StageResult st = stage_artifacts(sel);
  if (!st.ok) {
    a.source_note = st.error;
    return;
  }
  if (!a.stage_dir.empty()) a.stale_stages.push_back(a.stage_dir);
  a.stage_dir = st.dir;
  a.art = st.art;
  a.toast = trf("sources.staged", st.dir.string());
  evaluate_art(a);
  a.screen = Screen::Report;
}

std::string set_arch_list(const Artifacts& art, const PackageSet& s) {
  std::set<std::string> archs;
  for (std::size_t i : s.debs) {
    if (i < art.debs.size() && !art.debs[i].arch.empty()) archs.insert(art.debs[i].arch);
  }
  std::string out;
  for (const auto& x : archs) {
    if (!out.empty()) out += " ";
    out += x;
  }
  return out;
}

void open_detail(App& a, std::string title, std::vector<std::string> lines) {
  a.detail_open = true;
  a.detail_title = std::move(title);
  a.detail_lines = std::move(lines);
  a.detail_scroll = 0;
}

void show_repo_details(App& a) {
  if (a.repo_rows.empty() || a.repo_sel < 0 ||
      a.repo_sel >= static_cast<int>(a.repo_rows.size())) {
    return;
  }
  const RepoRow& row = a.repo_rows[static_cast<std::size_t>(a.repo_sel)];
  if (row.repo < 0 || row.repo >= static_cast<int>(a.cfg.repos.size())) return;
  Repo& repo = a.cfg.repos[static_cast<std::size_t>(row.repo)];
  RepoConf rc = parse_repo_conf(repo_conf_path(repo));

  std::string codename = row.codename.empty() ? repo.codename : row.codename;
  std::string component = row.component.empty() ? repo.component : row.component;
  const Distribution* d = find_dist(rc, codename);
  if (component.empty() && d && !d->components.empty()) component = d->components.front();
  if (component.empty()) component = "main";
  std::vector<std::string> archs = d ? d->architectures : repo.architectures;

  std::vector<std::string> lines;
  lines.push_back(tr("detail.repo") + ": " + repo.name);
  lines.push_back(tr("detail.base") + ": " + repo.base.string());
  lines.push_back(tr("detail.conf") + ": " + repo.conf.string());
  lines.push_back(tr("detail.codename") + ": " + codename);
  lines.push_back(tr("detail.component") + ": " + component);
  lines.push_back(tr("detail.archs") + ": " + (archs.empty() ? "-" : join(archs, " ")));
  if (d) {
    if (!d->suite.empty()) lines.push_back(tr("detail.suite") + ": " + d->suite);
    if (!d->components.empty())
      lines.push_back(tr("detail.components") + ": " + join(d->components, " "));
    if (!d->sign_with.empty()) lines.push_back(tr("detail.sign") + ": " + d->sign_with);
    if (!d->label.empty()) lines.push_back(tr("detail.label") + ": " + d->label);
    if (!d->origin.empty()) lines.push_back(tr("detail.origin") + ": " + d->origin);
  } else if (!rc.ok) {
    lines.push_back(tr("detail.conf_error") + ": " + rc.error);
  }

  fs::path base = effective_base(repo, rc);
  fs::path dists = base / "dists" / codename;
  lines.push_back("");
  lines.push_back(tr("detail.dists") + ": " + dists.string());
  std::error_code ec;
  lines.push_back(tr("detail.inrelease") + ": " +
                  (fs::exists(dists / "InRelease", ec) ? tr("detail.present") : tr("detail.absent")));
  lines.push_back(tr("detail.dist_dir") + ": " +
                  (fs::exists(dists, ec) ? tr("detail.present") : tr("detail.absent")));

  RepoState st = load_repo_state(base, codename, component, archs);
  lines.push_back("");
  if (!st.ok) {
    lines.push_back(trf("detail.state_error", st.error));
  } else {
    lines.push_back(trf("detail.contents", st.index.size()));
    if (st.index.empty()) lines.push_back("  " + tr("detail.empty"));
    int n = 0;
    for (const auto& [key, ver] : st.index) {
      auto bar = key.rfind('|');
      std::string pkg = bar == std::string::npos ? key : key.substr(0, bar);
      std::string arch = bar == std::string::npos ? "" : key.substr(bar + 1);
      lines.push_back("  " + pkg + "  " + ver + "  (" + arch + ")");
      if (++n >= 80) {
        lines.push_back("  ...");
        break;
      }
    }
  }
  open_detail(a, trf("detail.repo_title", repo.name), std::move(lines));
}

void show_set_details(App& a) {
  int si = current_set(a);
  if (si < 0 || si >= static_cast<int>(a.package_sets.size())) return;
  const PackageSet& s = a.package_sets[static_cast<std::size_t>(si)];
  std::vector<std::string> lines;
  lines.push_back(tr("detail.source") + ": " + (s.source.empty() ? "?" : s.source));
  lines.push_back(tr("detail.version") + ": " + (s.version.empty() ? "?" : s.version));
  lines.push_back(tr("detail.dir") + ": " + a.source_dir.string());
  std::string scene = tr("scan.scene.empty");
  if (s.has_source() && s.has_binary()) scene = tr("scan.scene.both");
  else if (s.has_binary()) scene = tr("scan.scene.binary");
  else if (s.has_source()) scene = tr("scan.scene.source");
  lines.push_back(tr("detail.scene") + ": " + scene);
  std::string archs = set_arch_list(a.source_art, s);
  if (!archs.empty()) lines.push_back(tr("detail.archs") + ": " + archs);
  lines.push_back("");
  lines.push_back(trf("detail.set_files", s.dscs.size() + s.debs.size()));
  std::error_code ec;
  for (std::size_t idx : s.dscs) {
    if (idx >= a.source_art.dscs.size()) continue;
    const auto& d = a.source_art.dscs[idx];
    std::uint64_t sz = fs::file_size(d.path, ec);
    lines.push_back("  dsc  " + d.path.filename().string() + "  " + human_size(ec ? 0 : sz));
  }
  for (std::size_t idx : s.debs) {
    if (idx >= a.source_art.debs.size()) continue;
    const auto& d = a.source_art.debs[idx];
    std::uint64_t sz = fs::file_size(d.path, ec);
    lines.push_back("  deb  " + d.path.filename().string() + "  [" + d.arch + "]  " +
                    human_size(ec ? 0 : sz));
  }
  open_detail(a, trf("detail.set_title", s.source, s.version), std::move(lines));
}

// ---------------------------------------------------------------------------
// repository maintenance
// ---------------------------------------------------------------------------

Command make_cmd(std::vector<std::string> argv, bool destructive) {
  Command c;
  c.display = display_argv(argv);
  c.argv = std::move(argv);
  c.destructive = destructive;
  return c;
}

void rebuild_maint_rows(App& a) {
  a.maint_rows.clear();
  for (int g = 0; g < 2; ++g) {
    MaintItem::Kind kind = g == 0 ? MaintItem::Kind::Source : MaintItem::Kind::Binary;
    std::vector<int> members;
    for (std::size_t i = 0; i < a.maint_items.size(); ++i) {
      if (a.maint_items[i].kind == kind) members.push_back(static_cast<int>(i));
    }
    if (members.empty()) continue;
    MaintRow row;
    row.type = MaintRow::Type::Group;
    row.group = g;
    a.maint_rows.push_back(row);
    if (a.maint_group_open[g]) {
      for (int i : members) {
        MaintRow mr;
        mr.type = MaintRow::Type::Item;
        mr.group = g;
        mr.item = i;
        a.maint_rows.push_back(mr);
      }
    }
  }
  a.maint_sel = std::min(a.maint_sel, std::max(0, static_cast<int>(a.maint_rows.size()) - 1));
}

int current_maint(App& a) {
  if (a.maint_sel < 0 || a.maint_sel >= static_cast<int>(a.maint_rows.size())) return -1;
  const MaintRow& r = a.maint_rows[static_cast<std::size_t>(a.maint_sel)];
  return r.type == MaintRow::Type::Item ? r.item : -1;
}

void load_maint(App& a) {
  ensure_selection(a);
  Repo& r = current_repo(a);
  fs::path base = effective_base(r, a.conf);
  a.maint_items.clear();
  a.maint_rows.clear();
  a.maint_sel = 0;
  a.maint_note.clear();
  a.maint_error.clear();
  a.maint_confirm = false;
  a.maint_typed.clear();
  a.maint_cmds.clear();
  a.maint_action.clear();

  RepoState st = load_repo_state(base, a.codename, a.component, selection_archs(a));
  if (!st.ok) {
    a.maint_note = trf("detail.state_error", st.error);
    rebuild_maint_rows(a);
    return;
  }
  std::map<std::string, std::string> sources;  // name -> version
  std::map<std::pair<std::string, std::string>, std::set<std::string>> binaries;
  for (const auto& [key, ver] : st.index) {
    auto bar = key.rfind('|');
    std::string pkg = bar == std::string::npos ? key : key.substr(0, bar);
    std::string arch = bar == std::string::npos ? "" : key.substr(bar + 1);
    if (arch == "source") sources[pkg] = ver;
    else binaries[{pkg, ver}].insert(arch);
  }
  for (const auto& [name, ver] : sources) {
    MaintItem it;
    it.kind = MaintItem::Kind::Source;
    it.name = name;
    it.version = ver;
    a.maint_items.push_back(std::move(it));
  }
  for (const auto& [pv, archs] : binaries) {
    MaintItem it;
    it.kind = MaintItem::Kind::Binary;
    it.name = pv.first;
    it.version = pv.second;
    it.archs.assign(archs.begin(), archs.end());
    a.maint_items.push_back(std::move(it));
  }
  if (a.maint_items.empty()) a.maint_note = tr("maint.empty");
  rebuild_maint_rows(a);
}

void open_maint(App& a) {
  load_maint(a);
  a.screen = Screen::Maint;
  a.toast.clear();
}

void prepare_maint_delete(App& a) {
  Repo& r = current_repo(a);
  fs::path base = effective_base(r, a.conf);
  std::vector<Command> cmds;
  for (const auto& it : a.maint_items) {
    if (!it.selected) continue;
    if (it.kind == MaintItem::Kind::Source) {
      // removesrc refuses -C/-A/-T ("cannot be restricted to a component").
      std::vector<std::string> v = {"reprepro", "-b", base.string(),
                                    "removesrc", a.codename, it.name};
      if (!it.version.empty()) v.push_back(it.version);
      cmds.push_back(make_cmd(std::move(v), true));
    } else {
      cmds.push_back(make_cmd({"reprepro", "-b", base.string(), "-C", a.component, "-T", "deb",
                               "remove", a.codename, it.name},
                              true));
    }
  }
  if (cmds.empty()) {
    a.maint_note = tr("maint.no_selection");
    return;
  }
  // Re-export explicitly so dists/ and the signature always match the db.
  cmds.push_back(make_cmd({"reprepro", "-b", base.string(), "export", a.codename}, false));
  a.maint_action = "delete";
  a.maint_cmds = std::move(cmds);
  a.maint_confirm = true;
  a.maint_typed.clear();
  a.maint_error.clear();
}

void prepare_maint_export(App& a) {
  Repo& r = current_repo(a);
  fs::path base = effective_base(r, a.conf);
  a.maint_action = "export";
  a.maint_cmds = {make_cmd({"reprepro", "-b", base.string(), "export", a.codename}, false)};
  a.maint_confirm = true;
  a.maint_typed.clear();
  a.maint_error.clear();
}

void prepare_maint_clean(App& a) {
  Repo& r = current_repo(a);
  fs::path base = effective_base(r, a.conf);
  a.maint_action = "clean";
  a.maint_cmds = {make_cmd({"reprepro", "-b", base.string(), "deleteunreferenced"}, true)};
  a.maint_confirm = true;
  a.maint_typed.clear();
  a.maint_error.clear();
}

void run_maint(App& a, ScreenInteractive& scr) {
  Repo& r = current_repo(a);
  fs::path base = effective_base(r, a.conf);
  std::string action = a.maint_action;
  ExecResult er;
  scr.WithRestoredIO([&] {
    std::cout << "\n";
    er = execute_commands(a.maint_cmds, base, [&](std::string_view s) {
      std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
      std::cout.flush();
    });
    std::cout.flush();
  })();
  a.maint_confirm = false;
  a.maint_typed.clear();

  std::vector<std::string> lines;
  for (const auto& res : er.results) {
    lines.push_back("$ " + display_argv(res.argv));
    if (res.skipped) {
      lines.push_back("  (skipped)");
    } else {
      std::istringstream is(res.output);
      std::string line;
      while (std::getline(is, line)) lines.push_back("  " + line);
      if (res.exit_code != 0) lines.push_back("  exit " + std::to_string(res.exit_code));
    }
    lines.push_back("");
  }
  lines.push_back(er.all_ok ? tr("maint.done") : tr("maint.failed"));

  load_maint(a);
  std::string label = tr("maint.export");
  if (action == "delete") label = tr("maint.delete");
  else if (action == "clean") label = tr("maint.clean");
  open_detail(a, trf("maint.result_title", label), std::move(lines));
}

// ---------------------------------------------------------------------------
// form
// ---------------------------------------------------------------------------

Field text_field(std::string key, std::string label, std::string value) {
  Field f;
  f.type = Field::Type::Text;
  f.key = std::move(key);
  f.label_key = std::move(label);
  f.value = std::move(value);
  return f;
}

Field int_field(std::string key, std::string label, std::string value) {
  Field f = text_field(std::move(key), std::move(label), std::move(value));
  f.type = Field::Type::Int;
  return f;
}

Field bool_field(std::string key, std::string label, bool value) {
  Field f = text_field(std::move(key), std::move(label), value ? "true" : "false");
  f.type = Field::Type::Bool;
  return f;
}

Field choice_field(std::string key, std::string label, const std::vector<std::string>& choices,
                   const std::string& value) {
  Field f = text_field(std::move(key), std::move(label), value);
  f.type = Field::Type::Choice;
  f.choices = choices;
  return f;
}

void build_fields(App& a) {
  a.fields.clear();
  if (a.cfg.repos.empty()) a.cfg.repos.push_back(Repo{});
  a.edit_repo = std::clamp(a.edit_repo, 0, static_cast<int>(a.cfg.repos.size()) - 1);
  Repo& r = a.cfg.repos[static_cast<std::size_t>(a.edit_repo)];

  auto repo_fields = [&] {
    a.fields.push_back(text_field("name", "field.name", r.name));
    a.fields.push_back(text_field("base", "field.base", r.base.string()));
    a.fields.push_back(text_field("conf", "field.conf", r.conf.string()));
    a.fields.push_back(text_field("codename", "field.codename", r.codename));
    a.fields.push_back(text_field("component", "field.component", r.component));
    a.fields.push_back(text_field("architectures", "field.architectures", join(r.architectures, " ")));
    a.fields.push_back(text_field("incoming", "field.incoming", r.incoming.string()));
  };

  if (a.screen != Screen::Settings) {
    repo_fields();
    a.fields.push_back(text_field("artifacts", "field.artifacts", a.cfg.artifacts.string()));
    a.fields.push_back(choice_field("detail", "field.detail", {"summary", "normal", "full"}, a.cfg.ui.detail));
    a.fields.push_back(int_field("unchanged_fold", "field.unchanged_fold",
                                 std::to_string(a.cfg.ui.unchanged_fold)));
    a.fields.push_back(choice_field("language", "field.language", {"auto", "zh-CN", "en-US"},
                                    a.cfg.ui.language));
  } else if (a.tab == 0) {
    repo_fields();
  } else if (a.tab == 1) {
    a.fields.push_back(text_field("artifacts", "field.artifacts", a.cfg.artifacts.string()));
  } else if (a.tab == 2) {
    a.fields.push_back(bool_field("require_both", "field.require_both", a.cfg.policy.require_both));
    a.fields.push_back(choice_field("missing_arch", "field.missing_arch", {"error", "warn", "ignore"},
                                    a.cfg.policy.missing_arch));
    a.fields.push_back(choice_field("buildinfo", "field.buildinfo", {"error", "warn", "ignore"},
                                    a.cfg.policy.buildinfo));
    a.fields.push_back(choice_field("sha256", "field.sha256", {"error", "warn", "ignore"},
                                    a.cfg.policy.sha256));
    a.fields.push_back(bool_field("allow_replace", "field.allow_replace", a.cfg.policy.allow_replace));
  } else {
    a.fields.push_back(choice_field("detail", "field.detail", {"summary", "normal", "full"}, a.cfg.ui.detail));
    a.fields.push_back(int_field("unchanged_fold", "field.unchanged_fold",
                                 std::to_string(a.cfg.ui.unchanged_fold)));
    a.fields.push_back(bool_field("show_unchanged", "field.show_unchanged", a.cfg.ui.show_unchanged));
    a.fields.push_back(choice_field("language", "field.language", {"auto", "zh-CN", "en-US"},
                                    a.cfg.ui.language));
  }
  a.focus = std::min(a.focus, std::max(0, static_cast<int>(a.fields.size()) - 1));
}

std::string field_value(const App& a, const std::string& key) {
  for (const auto& f : a.fields) {
    if (f.key == key) return f.value;
  }
  return {};
}

bool parse_bool(const std::string& v) { return v == "true" || v == "1" || v == "yes"; }

void apply_fields(App& a) {
  if (a.cfg.repos.empty()) a.cfg.repos.push_back(Repo{});
  Repo& r = a.cfg.repos[static_cast<std::size_t>(std::clamp(a.edit_repo, 0,
                                                            static_cast<int>(a.cfg.repos.size()) - 1))];
  auto has = [&](const char* k) {
    return std::any_of(a.fields.begin(), a.fields.end(), [&](const Field& f) { return f.key == k; });
  };
  if (has("name")) r.name = field_value(a, "name");
  if (has("base")) r.base = expand_user(field_value(a, "base"));
  if (r.name.empty() && !r.base.empty()) r.name = r.base.filename().string();
  if (has("conf")) r.conf = expand_user(field_value(a, "conf"));
  if (has("codename")) r.codename = field_value(a, "codename");
  if (has("component")) r.component = field_value(a, "component");
  if (has("architectures")) r.architectures = split(field_value(a, "architectures"));
  if (has("incoming")) r.incoming = expand_user(field_value(a, "incoming"));
  if (has("artifacts")) {
    fs::path next = expand_user(field_value(a, "artifacts"));
    if (next != a.cfg.artifacts) {
      if (!a.stage_dir.empty()) a.stale_stages.push_back(a.stage_dir);
      a.stage_dir.clear();
      a.source_dir = next;
    }
    a.cfg.artifacts = next;
  }
  if (has("detail")) a.cfg.ui.detail = field_value(a, "detail");
  if (has("unchanged_fold")) {
    try {
      a.cfg.ui.unchanged_fold = std::stoi(field_value(a, "unchanged_fold"));
    } catch (...) {
      a.cfg.ui.unchanged_fold = 15;
    }
  }
  if (has("language")) {
    bool ok = false;
    Lang l = parse_lang(field_value(a, "language"), &ok);
    set_lang(l);
    a.cfg.ui.language = field_value(a, "language");
  }
  if (has("require_both")) a.cfg.policy.require_both = parse_bool(field_value(a, "require_both"));
  if (has("missing_arch")) a.cfg.policy.missing_arch = field_value(a, "missing_arch");
  if (has("buildinfo")) a.cfg.policy.buildinfo = field_value(a, "buildinfo");
  if (has("sha256")) a.cfg.policy.sha256 = field_value(a, "sha256");
  if (has("allow_replace")) a.cfg.policy.allow_replace = parse_bool(field_value(a, "allow_replace"));
  if (has("show_unchanged")) a.cfg.ui.show_unchanged = parse_bool(field_value(a, "show_unchanged"));
}

bool save_form(App& a) {
  apply_fields(a);
  std::vector<std::string> errs;
  if (!validate_config(a.cfg, &errs)) {
    a.toast = errs.empty() ? tr("config.need_repo") : errs.front();
    return false;
  }
  std::string err;
  if (!save_config(a.cfg, a.cfg_path, &err)) {
    a.toast = err;
    return false;
  }
  a.cfg_exists = true;
  a.cfg_error.clear();
  a.toast = trf("config.saved", a.cfg_path.string());
  a.opts.cfg = a.cfg;
  if (a.return_screen == Screen::Sources) reload_sources(a);
  return true;
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

Element header(App& a) {
  return hbox({
             text(tr("app.title")) | bold | color(Color::Cyan),
             text("  " + tr("app.subtitle")) | dim,
             filler(),
             text(lang_code(current_lang())) | dim,
         }) |
         border;
}

Element footer(const std::string& hints) {
  return hbox({text(hints) | dim, filler()}) | border;
}

// A short, always-visible reminder of the unified navigation keys.  The full
// list stays behind '?'; this only shows the few keys for the current screen.
std::string footer_hint(App& a) {
  std::vector<std::string> parts;
  switch (a.screen) {
    case Screen::Home:
      parts = {tr("hint.enter_open"), tr("hint.quit"), tr("hint.help")};
      break;
    case Screen::Config:
      parts = {tr("hint.save"), tr("hint.tab"), tr("hint.help")};
      break;
    case Screen::Settings:
      parts = {tr("hint.save"), tr("hint.esc"), tr("hint.tab"), tr("hint.help")};
      break;
    case Screen::Repos:
      parts = {tr("hint.enter_select"), tr("hint.details"), tr("hint.esc"), tr("hint.help")};
      break;
    case Screen::Sources:
      parts = {tr("hint.mark"), tr("hint.g_preview"), tr("hint.esc"), tr("hint.help")};
      break;
    case Screen::Browse:
      parts = {tr("hint.enter_open"), tr("hint.esc"), tr("hint.help")};
      break;
    case Screen::Maint:
      parts = {tr("hint.mark"), tr("hint.g_run"), tr("hint.esc"), tr("hint.help")};
      break;
    case Screen::Report:
      parts = {tr("hint.enter_next"), tr("hint.esc"), tr("hint.help")};
      break;
    case Screen::Diff:
      parts = {tr("hint.enter_next"), tr("hint.esc"), tr("hint.help")};
      break;
    case Screen::Confirm:
      parts = {tr("hint.enter_run"), tr("hint.esc"), tr("hint.help")};
      break;
    case Screen::Verify:
      parts = {tr("hint.enter_done"), tr("hint.esc"), tr("hint.help")};
      break;
  }
  std::string out;
  for (const auto& p : parts) {
    if (!out.empty()) out += "   ";
    out += p;
  }
  if (a.screen == Screen::Sources) {
    int selected = static_cast<int>(std::count(a.set_selected.begin(), a.set_selected.end(), true));
    out += "   " + trf("sources.pending", selected);
  } else if (a.screen == Screen::Maint) {
    int selected = 0;
    for (const auto& it : a.maint_items) selected += it.selected ? 1 : 0;
    out += "   " + trf("maint.pending", selected);
  }
  return out;
}

Element field_element(const Field& f, bool focused) {
  Element label = text(tr(f.label_key) + ": ") | size(WIDTH, EQUAL, 40);
  Element value;
  switch (f.type) {
    case Field::Type::Bool:
      value = text(f.value == "true" ? "[x] " + tr("common.yes") : "[ ] " + tr("common.no"));
      break;
    case Field::Type::Choice:
      value = text("< " + f.value + " >");
      break;
    default:
      value = text(f.value.empty() ? " " : f.value);
  }
  if (focused) {
    value |= inverted;
    label |= bold;
  }
  return hbox({label, value, filler()});
}

Element render_form(App& a) {
  Elements lines;
  if (a.screen == Screen::Settings) {
    std::vector<std::string> tabs = {tr("settings.tab.repo"), tr("settings.tab.paths"),
                                     tr("settings.tab.policy"), tr("settings.tab.ui")};
    Elements t;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
      Element e = text(" " + tabs[i] + " ");
      if (static_cast<int>(i) == a.tab) { e |= inverted; e |= bold; }
      else e |= dim;
      t.push_back(e);
      t.push_back(text(" "));
    }
    lines.push_back(hbox(std::move(t)));
    lines.push_back(separator());

    if (a.tab == 0) {
      Repo& r = a.cfg.repos[static_cast<std::size_t>(
          std::clamp(a.edit_repo, 0, static_cast<int>(a.cfg.repos.size()) - 1))];
      lines.push_back(hbox({text(tr("field.name") + ": "), text(r.name.empty() ? "?" : r.name) | bold,
                            filler(),
                            text("[" + std::to_string(a.edit_repo + 1) + "/" +
                                 std::to_string(a.cfg.repos.size()) + "]") | dim}));
    }
  } else {
    lines.push_back(text(tr("config.title")) | bold);
    if (!a.cfg_exists) lines.push_back(text(tr("config.missing")) | color(Color::Yellow));
    if (!a.cfg_error.empty()) lines.push_back(text(trf("config.conf_bad", a.cfg_error)) | color(Color::Red));
  }
  lines.push_back(separator());

  for (std::size_t i = 0; i < a.fields.size(); ++i) {
    lines.push_back(field_element(a.fields[i], static_cast<int>(i) == a.focus));
  }
  if (a.screen == Screen::Config || a.tab == 0) {
    lines.push_back(text(tr("config.auto_hint")) | dim);
  }

  // live conf parse status
  if (!a.fields.empty() && a.fields.front().key == "name") {
    Repo edited;
    edited.base = expand_user(field_value(a, "base"));
    edited.conf = expand_user(field_value(a, "conf"));
    fs::path conf_path = repo_conf_path(edited);
    RepoConf rc = parse_repo_conf(conf_path);
    Element status = rc.ok
        ? text(trf("config.conf_ok", rc.distributions.size())) | color(Color::Green)
        : text(trf("config.conf_bad", rc.error)) | color(Color::Red);
    lines.push_back(separator());
    lines.push_back(status);
  }

  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Yellow));

  Element box = vbox(std::move(lines)) | border | yframe | flex;

  std::string hints = footer_hint(a);
  return vbox({header(a), box, footer(hints)});
}

Element render_home(App& a) {
  Elements lines;
  lines.push_back(text(tr("home.title")) | bold | color(Color::Cyan));
  lines.push_back(hbox({text(tr("home.config") + ": ") | dim,
                        text(a.cfg_path.string()) | color(Color::Cyan)}));
  lines.push_back(hbox({text(tr("home.artifacts") + ": ") | dim,
                        text(a.cfg.artifacts.string()) | color(Color::Cyan)}));
  if (a.cfg.repos.empty()) lines.push_back(text(tr("home.no_config")) | color(Color::Yellow));
  lines.push_back(separator());

  std::string titles[4] = {tr("home.upload"), tr("home.manage"),
                           tr("home.repo_settings"), tr("home.app_settings")};
  std::string descs[4] = {tr("home.upload.desc"), tr("home.manage.desc"),
                          tr("home.repo_settings.desc"), tr("home.app_settings.desc")};
  for (int i = 0; i < 4; ++i) {
    Element title = text("  " + titles[i] + "  ");
    if (i == a.home_sel) title |= inverted;
    if (i == a.home_sel) title |= bold;
    lines.push_back(hbox({text(i == a.home_sel ? "▸" : " "), title}));
    lines.push_back(text("    " + descs[i]) | dim);
  }
  lines.push_back(separator());
  lines.push_back(text(tr("home.hint")) | dim);
  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Yellow));

  return vbox({header(a), vbox(std::move(lines)) | border | flex, footer(footer_hint(a))});
}

Element render_repos(App& a) {
  Elements lines;
  lines.push_back(text(tr("repo.title")) | bold);
  lines.push_back(text(a.repos_purpose == ReposPurpose::Maintain ? tr("repo.purpose.maint")
                                                                 : tr("repo.purpose.upload")) |
                  color(Color::Yellow));
  lines.push_back(text(tr("repo.hint")) | dim);
  lines.push_back(separator());

  int start = std::max(0, a.repo_sel - kListWindow + 3);
  start = std::min(start, std::max(0, static_cast<int>(a.repo_rows.size()) - kListWindow));
  for (int i = start; i < static_cast<int>(a.repo_rows.size()) && i < start + kListWindow; ++i) {
    const RepoRow& r = a.repo_rows[static_cast<std::size_t>(i)];
    Element line;
    switch (r.kind) {
      case RepoRow::Kind::Repo:
        line = hbox({text("▸ " + r.text) | bold | color(Color::Cyan), text("  " + r.meta) | dim});
        break;
      case RepoRow::Kind::Codename:
        line = hbox({text("  " + r.text) | bold, text("  " + r.meta) | dim});
        break;
      case RepoRow::Kind::Component:
        line = hbox({text("    " + r.text),
                     text("  " + r.meta) | dim});
        if (i == a.repo_sel) line |= inverted;
        break;
    }
    lines.push_back(std::move(line));
  }
  if (a.repo_rows.empty()) lines.push_back(text(tr("repo.empty")) | color(Color::Yellow));

  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Yellow));

  std::string hints = footer_hint(a);
  return vbox({header(a), vbox(std::move(lines)) | border | yframe | flex, footer(hints)});
}

std::string src_group_name(int g) {
  switch (g) {
    case 0: return tr("sources.group.update");
    case 1: return tr("sources.group.new");
    case 2: return tr("sources.group.downgrade");
    default: return tr("sources.group.same");
  }
}

Color src_status_color(SetStatus s) {
  switch (s) {
    case SetStatus::Upgrade: return Color::Yellow;
    case SetStatus::New: return Color::Green;
    case SetStatus::Downgrade: return Color::Red;
    default: return Color::GrayDark;
  }
}

std::string src_status_text(const App& a, int i) {
  const std::string& local = a.package_sets[static_cast<std::size_t>(i)].version;
  const std::string& repo = a.set_repo_ver[static_cast<std::size_t>(i)];
  switch (a.set_status[static_cast<std::size_t>(i)]) {
    case SetStatus::Upgrade:
      return trf("sources.status.update", repo.empty() ? "?" : repo, local);
    case SetStatus::New:
      return tr("sources.status.new");
    case SetStatus::Downgrade:
      return trf("sources.status.downgrade", repo.empty() ? "?" : repo);
    default:
      return trf("sources.status.same", repo.empty() ? local : repo);
  }
}

int src_group_members(const App& a, int g) {
  int n = 0;
  for (SetStatus s : a.set_status) {
    if (static_cast<int>(s) == g) ++n;
  }
  return n;
}

Element render_sources(App& a) {
  Elements lines;
  lines.push_back(text(tr("sources.title")) | bold);
  lines.push_back(hbox({text(tr("sources.dir") + ": ") | dim,
                        text(a.source_dir.string()) | color(Color::Cyan)}));
  lines.push_back(text(tr("sources.hint")) | dim);
  lines.push_back(separator());

  int total = static_cast<int>(a.src_rows.size());
  int start = std::max(0, a.src_sel - kListWindow + 3);
  start = std::min(start, std::max(0, total - kListWindow));
  for (int i = start; i < total && i < start + kListWindow; ++i) {
    const SrcRow& row = a.src_rows[static_cast<std::size_t>(i)];
    Element line;
    if (row.type == SrcRow::Type::Group) {
      bool open = a.src_group_open[row.group];
      line = hbox({text(open ? "▾ " : "▸ ") | color(Color::Cyan),
                   text(src_group_name(row.group)) | bold,
                   text(" (" + std::to_string(src_group_members(a, row.group)) + ")") | dim});
    } else {
      int si = row.set;
      if (si < 0 || si >= static_cast<int>(a.package_sets.size())) continue;
      const PackageSet& s = a.package_sets[static_cast<std::size_t>(si)];
      bool sel = si < static_cast<int>(a.set_selected.size()) &&
                 a.set_selected[static_cast<std::size_t>(si)];
      Element mark = text(sel ? "+ " : "  ") | color(sel ? Color::Green : Color::GrayDark);
      Element name = text("  " + (s.source.empty() ? "?" : s.source)) | bold;
      Element ver = text(" " + s.version);
      Element status = text("  [" + src_status_text(a, si) + "]") |
                       color(src_status_color(a.set_status[static_cast<std::size_t>(si)]));
      std::string archs = set_arch_list(a.source_art, s);
      Elements parts = {mark, name, ver, status,
                        text("  " + trf("sources.files", s.dscs.size(), s.debs.size())) | dim};
      if (!archs.empty()) parts.push_back(text("  " + archs) | dim);
      line = hbox(std::move(parts));
    }
    if (i == a.src_sel) line |= inverted;
    lines.push_back(std::move(line));
  }

  int selected = static_cast<int>(
      std::count(a.set_selected.begin(), a.set_selected.end(), true));
  lines.push_back(separator());
  lines.push_back(hbox({text(trf("sources.count", selected,
                                 static_cast<int>(a.package_sets.size()))),
                        filler(),
                        text(trf("sources.files", a.source_art.dscs.size(),
                                 a.source_art.debs.size())) | dim}));
  if (!a.source_note.empty()) lines.push_back(text(a.source_note) | color(Color::Yellow));
  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Green));

  std::string hints = footer_hint(a);
  return vbox({header(a), vbox(std::move(lines)) | border | yframe | flex, footer(hints)});
}

std::string maint_group_name(int g) {
  return g == 0 ? tr("maint.source_group") : tr("maint.binary_group");
}

Element render_maint(App& a) {
  Elements lines;
  lines.push_back(text(tr("maint.title")) | bold | color(Color::Cyan));
  lines.push_back(hbox({text(tr("maint.target") + ": ") | dim,
                        text(current_repo(a).name + "/" + a.codename + "/" + a.component) |
                            color(Color::Yellow)}));
  lines.push_back(text(tr("maint.hint")) | dim);
  bool has_source = std::any_of(a.maint_items.begin(), a.maint_items.end(),
                                [](const MaintItem& it) {
                                  return it.kind == MaintItem::Kind::Source;
                                });
  if (has_source) lines.push_back(text(tr("maint.source_note")) | color(Color::Yellow));
  lines.push_back(separator());

  int total = static_cast<int>(a.maint_rows.size());
  int start = std::max(0, a.maint_sel - kListWindow + 3);
  start = std::min(start, std::max(0, total - kListWindow));
  for (int i = start; i < total && i < start + kListWindow; ++i) {
    const MaintRow& row = a.maint_rows[static_cast<std::size_t>(i)];
    Element line;
    if (row.type == MaintRow::Type::Group) {
      bool open = a.maint_group_open[row.group];
      int count = 0;
      for (const auto& it : a.maint_items) {
        if ((it.kind == MaintItem::Kind::Source) == (row.group == 0)) ++count;
      }
      line = hbox({text(open ? "▾ " : "▸ ") | color(Color::Cyan),
                   text(maint_group_name(row.group)) | bold,
                   text(" (" + std::to_string(count) + ")") | dim});
    } else {
      if (row.item < 0 || row.item >= static_cast<int>(a.maint_items.size())) continue;
      const MaintItem& it = a.maint_items[static_cast<std::size_t>(row.item)];
      Element mark = it.selected ? text("- ") | color(Color::Red) | bold : text("  ");
      Elements parts = {mark, text(it.name) | bold, text(" " + it.version)};
      if (!it.archs.empty()) {
        std::string archs;
        for (const auto& x : it.archs) {
          if (!archs.empty()) archs += " ";
          archs += x;
        }
        parts.push_back(text("  " + archs) | dim);
      }
      line = hbox(std::move(parts));
    }
    if (i == a.maint_sel) line |= inverted;
    lines.push_back(std::move(line));
  }
  if (total == 0) lines.push_back(text(tr("maint.empty")) | dim);

  int selected = 0;
  for (const auto& it : a.maint_items) selected += it.selected ? 1 : 0;
  lines.push_back(separator());
  lines.push_back(text(trf("maint.count", selected,
                           static_cast<int>(a.maint_items.size()))));
  if (!a.maint_note.empty()) lines.push_back(text(a.maint_note) | color(Color::Yellow));
  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Yellow));

  std::string hints = footer_hint(a);
  return vbox({header(a), vbox(std::move(lines)) | border | yframe | flex, footer(hints)});
}

Element render_browse(App& a) {
  Elements lines;
  lines.push_back(text(tr("browse.title")) | bold);
  lines.push_back(hbox({text(tr("browse.dir") + ": ") | dim,
                        text(a.browse_dir.string()) | color(Color::Cyan)}));
  if (!a.browse_filter.empty()) {
    lines.push_back(hbox({text(trf("browse.filter", a.browse_filter)) | color(Color::Yellow) | bold,
                          text("   " + tr("browse.filter_hint")) | dim}));
  }
  lines.push_back(separator());

  int total = static_cast<int>(a.browse_dirs.size()) + 1;
  int start = std::max(0, a.browse_sel - kListWindow + 3);
  start = std::min(start, std::max(0, total - kListWindow));
  std::string needle = ascii_lower(a.browse_filter);
  for (int idx = start; idx < total && idx < start + kListWindow; ++idx) {
    Element line;
    if (idx == 0) {
      line = hbox({text("✓ ") | color(Color::Green) | bold,
                   text(tr("help.usecur")) | bold,
                   text("  " + a.browse_dir.string()) | dim});
    } else {
      int i = idx - 1;
      int count = i < static_cast<int>(a.browse_counts.size())
                      ? a.browse_counts[static_cast<std::size_t>(i)]
                      : 0;
      std::string name = a.browse_dirs[static_cast<std::size_t>(i)].filename().string();
      Elements parts;
      parts.push_back(text("▸ ") | color(Color::Cyan));
      if (!needle.empty() && ascii_lower(name).rfind(needle, 0) == 0) {
        parts.push_back(text(name.substr(0, needle.size())) | color(Color::Yellow) | bold);
        parts.push_back(text(name.substr(needle.size())));
      } else {
        parts.push_back(text(name));
      }
      parts.push_back(text("  " + trf("browse.artifacts", count)) | dim);
      line = hbox(std::move(parts));
    }
    if (idx == a.browse_sel) line |= inverted;
    lines.push_back(std::move(line));
  }
  if (a.browse_dirs.empty()) {
    lines.push_back(text(tr("browse.empty")) | dim);
  }
  lines.push_back(separator());
  if (!a.browse_note.empty()) lines.push_back(text(a.browse_note) | color(Color::Yellow));
  lines.push_back(text(std::string("[enter] ") + tr("help.open") + "   [←] " + tr("help.parent") +
                       "   [a-z] " + tr("help.typing") + "   [esc] " + tr("help.back")) |
                   dim);

  return vbox({header(a), vbox(std::move(lines)) | border | yframe | flex,
               footer(footer_hint(a))});
}

Element render_report(App& a) {
  Elements lines;
  lines.push_back(hbox({text(trf("diff.target", current_repo(a).name, a.codename, a.component)) | bold,
                        filler(),
                        text(trf("scan.files", a.art.debs.size(), a.art.dscs.size())) | dim}));
  lines.push_back(separator());

  int start = std::max(0, a.issue_scroll - kListWindow + 3);
  start = std::min(start, std::max(0, static_cast<int>(a.issues.size()) - kListWindow));
  for (int i = start; i < static_cast<int>(a.issues.size()) && i < start + kListWindow; ++i) {
    const Issue& is = a.issues[static_cast<std::size_t>(i)];
    Element icon = text(human_issue_mark(is.sev) + " ") | color(sev_color(is.sev));
    Element msg = text(is.msg);
    if (is.sev == Severity::Block) msg |= color(Color::Red);
    else if (is.sev == Severity::Warn) msg |= color(Color::Yellow);
    else msg |= dim;
    lines.push_back(hbox({icon, msg}));
  }

  lines.push_back(separator());
  int blocks = count_issues(a.issues, Severity::Block);
  int warns = count_issues(a.issues, Severity::Warn);
  Element verdict;
  if (blocks > 0) {
    verdict = text(trf("confirm.blocked", blocks)) | color(Color::Red) | bold;
  } else if (warns > 0) {
    verdict = text(trf("diff.counts", a.diff.added, a.diff.modified, a.diff.unchanged)) |
              color(Color::Yellow);
  } else {
    verdict = text(tr("common.ok")) | color(Color::Green) | bold;
  }
  lines.push_back(hbox({verdict, filler(),
                        text(trf("diff.counts", a.diff.added, a.diff.modified, a.diff.unchanged)) | dim}));

  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Yellow));

  std::string hints = footer_hint(a);
  return vbox({header(a), vbox(std::move(lines)) | border | yframe | flex, footer(hints)});
}

Element render_diff_row(const DiffRow& row, bool selected) {
  std::string indent(static_cast<std::size_t>(row.depth) * 2, ' ');
  if (row.fold) {
    Element e = text(indent + trf("diff.fold", row.fold_count)) | color(Color::Yellow) | dim;
    if (selected) e |= inverted;
    return e;
  }
  const ChangeNode& n = *row.node;
  bool leaf = n.kind == ChangeNode::Kind::File || n.kind == ChangeNode::Kind::Index;
  Elements parts;
  parts.push_back(text(indent));
  if (leaf) {
    parts.push_back(text(std::string(change_symbol(n.change)) + " ") | color(change_color(n.change)) | bold);
  } else if (n.change == Change::Unchanged) {
    parts.push_back(text("· ") | dim);
  } else {
    parts.push_back(text("  "));
  }
  Element name;
  if (n.kind == ChangeNode::Kind::Index) {
    name = text(n.change == Change::Unchanged ? trf("diff.index.same", n.name, n.meta)
                                              : trf("diff.index.new", n.name, n.meta)) |
           color(change_color(n.change));
  } else if (n.kind == ChangeNode::Kind::Package) {
    NodeStats st = node_stats(n);
    name = hbox({text(n.name) | bold, text("  " + n.meta) | dim,
                 text("  " + trf("diff.pkg_stats", st.changed, st.total)) | dim});
  } else {
    name = text(n.name + (leaf ? "" : "/")) | color(change_color(n.change));
    if (!n.meta.empty() && current_lang() == Lang::EnUS) name = hbox({name, text("  " + n.meta) | dim});
  }
  parts.push_back(name);
  parts.push_back(filler());
  Element line = hbox(std::move(parts));
  if (selected) line |= inverted;
  return line;
}

Element render_diff(App& a) {
  Elements lines;
  lines.push_back(hbox({text(tr("diff.title")) | bold, filler(),
                        text(trf("diff.counts", a.diff.added, a.diff.modified, a.diff.unchanged)) | dim}));
  lines.push_back(text(trf("diff.target", current_repo(a).name, a.codename, a.component)) | dim);
  lines.push_back(separator());

  if (a.rows.empty()) {
    lines.push_back(text(tr("diff.no_changes")) | color(Color::Yellow));
  } else {
    int start = std::max(0, a.row_sel - kListWindow + 3);
    start = std::min(start, std::max(0, static_cast<int>(a.rows.size()) - kListWindow));
    for (int i = start; i < static_cast<int>(a.rows.size()) && i < start + kListWindow; ++i) {
      lines.push_back(render_diff_row(a.rows[static_cast<std::size_t>(i)], i == a.row_sel));
    }
  }
  lines.push_back(separator());
  lines.push_back(text(tr("diff.legend")) | dim);
  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Yellow));

  std::string hints = footer_hint(a);
  return vbox({header(a), vbox(std::move(lines)) | border | yframe | flex, footer(hints)});
}

Element render_confirm(App& a) {
  Elements lines;
  lines.push_back(text(tr("confirm.title")) | bold | color(Color::Yellow));
  lines.push_back(separator());
  lines.push_back(text(trf("diff.counts", a.diff.added, a.diff.modified, a.diff.unchanged)));
  if (!a.scene_warning.empty() && !a.ack_scene) {
    lines.push_back(text(a.scene_warning) | color(Color::Yellow));
  }
  if (a.replace_needed && a.cfg.policy.allow_replace) {
    lines.push_back(text(trf("confirm.replace", a.diff.modified)) | color(Color::Yellow));
  }
  lines.push_back(separator());
  for (std::size_t i = 0; i < a.cmds.size() && i < 8; ++i) {
    lines.push_back(text("$ " + a.cmds[i].display) | dim);
  }
  if (a.cmds.size() > 8) lines.push_back(text(trf("diff.collapsed", a.cmds.size() - 8)) | dim);
  lines.push_back(separator());

  if (!a.scene_warning.empty() && !a.ack_scene) {
    lines.push_back(text(tr("confirm.publish_hint")) | bold);
  } else if (a.replace_needed) {
    lines.push_back(text(trf("confirm.type_codename", a.codename)) | color(Color::Yellow));
    lines.push_back(hbox({text("> "), text(a.typed) | bold | inverted}));
  } else {
    lines.push_back(text(tr("confirm.publish_hint")) | bold);
  }
  if (!a.confirm_error.empty()) lines.push_back(text(a.confirm_error) | color(Color::Red));
  if (!a.toast.empty()) lines.push_back(text(a.toast) | color(Color::Yellow));

  return vbox({header(a), vbox(std::move(lines)) | border | flex,
               footer(footer_hint(a))});
}

Element render_verify(App& a) {
  Elements lines;
  lines.push_back(text(tr("verify.title")) | bold);
  lines.push_back(separator());
  for (const auto& is : a.verify_issues) {
    lines.push_back(hbox({text(human_issue_mark(is.sev) + " ") | color(sev_color(is.sev)),
                          text(is.msg) | color(is.sev == Severity::Block ? Color::Red : Color::White)}));
  }
  lines.push_back(separator());
  if (!a.published) {
    lines.push_back(text(tr("verify.skip")) | dim);
  } else if (has_blocking(a.verify_issues)) {
    lines.push_back(text(tr("verify.failed")) | color(Color::Red) | bold);
  } else {
    lines.push_back(text(tr("verify.all_ok")) | color(Color::Green) | bold);
  }
  lines.push_back(separator());
  lines.push_back(text(tr("exec.log")) | bold);
  {
    std::istringstream is(a.exec_log);
    std::string line;
    int n = 0;
    std::vector<std::string> all;
    while (std::getline(is, line)) all.push_back(line);
    int start = std::max(0, static_cast<int>(all.size()) - 12);
    for (int i = start; i < static_cast<int>(all.size()); ++i) {
      lines.push_back(text(all[static_cast<std::size_t>(i)]) | dim);
    }
    (void)n;
  }

  std::string hints = footer_hint(a);
  return vbox({header(a), vbox(std::move(lines)) | border | yframe | flex, footer(hints)});
}

struct HelpItem {
  std::string key;
  std::string desc;
};

std::pair<std::vector<HelpItem>, std::vector<HelpItem>> help_items(App& a) {
  std::vector<HelpItem> page;
  std::vector<HelpItem> global;
  global.push_back({"?", tr("help.help")});
  if (a.screen != Screen::Config && a.screen != Screen::Settings &&
      a.screen != Screen::Browse) {
    global.push_back({"q", tr("help.quit")});
    global.push_back({"l", tr("help.lang")});
    global.push_back({"s", tr("help.settings")});
    if (a.screen != Screen::Repos) global.push_back({"t", tr("help.repo")});
  }
  switch (a.screen) {
    case Screen::Home:
      page = {{"↑/↓", tr("help.move")},
              {"enter", tr("help.open_menu")}};
      break;
    case Screen::Config:
      page = {{"↑/↓", tr("help.focus")},
              {"tab", tr("help.focus")},
              {"ctrl+s", tr("help.save")},
              {"esc", tr("help.back")}};
      break;
    case Screen::Settings:
      page = {{"↑/↓", tr("help.focus")},
              {"tab / ←/→", tr("help.tab")},
              {"space", tr("help.option")},
              {"ctrl+s", tr("help.save")},
              {"ctrl+a", tr("help.addrepo")},
              {"ctrl+d", tr("help.delrepo")},
              {"[ / ]", tr("help.prevrepo")},
              {"esc", tr("help.back")}};
      break;
    case Screen::Repos:
      page = {{"↑/↓", tr("help.move")},
              {"enter", tr("help.enter")},
              {"v", tr("help.details")},
              {"r", tr("help.rescan")},
              {"esc", tr("help.home")}};
      break;
    case Screen::Maint:
      page = {{"↑/↓", tr("help.move")},
              {"- / +", tr("help.mark")},
              {"enter", tr("help.expand")},
              {"g", tr("help.run")},
              {"e", tr("help.export")},
              {"c", tr("help.clean")},
              {"r", tr("help.rescan")},
              {"esc", tr("help.back")}};
      break;
    case Screen::Sources:
      page = {{"↑/↓", tr("help.move")},
              {"+ / -", tr("help.mark")},
              {"enter", tr("help.expand")},
              {"g", tr("help.go")},
              {"a / i", tr("help.selectall")},
              {"v", tr("help.details")},
              {"d", tr("help.pickdir")},
              {"r", tr("help.rescan")},
              {"esc", tr("help.back")}};
      break;
    case Screen::Browse:
      page = {{"↑/↓", tr("help.move")},
              {"enter / →", tr("help.open")},
              {"← / backspace", tr("help.parent")},
              {"a-z", tr("help.typing")},
              {"space", tr("help.usecur")},
              {"esc", tr("help.back")}};
      break;
    case Screen::Report:
      page = {{"↑/↓", tr("help.scroll")},
              {"enter", tr("help.enter")},
              {"o", tr("help.reselect")},
              {"r", tr("help.rescan")},
              {"p", tr("help.copy")},
              {"esc", tr("help.back")}};
      break;
    case Screen::Diff:
      page = {{"↑/↓", tr("help.move")},
              {"o", tr("help.reselect")},
              {"space", tr("help.fold")},
              {"u", tr("help.unchanged")},
              {"p", tr("help.copy")},
              {"enter", tr("help.enter")},
              {"esc", tr("help.back")}};
      break;
    case Screen::Confirm:
      if (!a.scene_warning.empty() && !a.ack_scene) {
        page = {{"y/enter", tr("help.ack")}, {"esc", tr("help.back")}};
      } else if (a.replace_needed) {
        page = {{"type", tr("help.type")}, {"esc", tr("help.back")}};
      } else {
        page = {{"x / y / enter", tr("help.publish")}, {"esc", tr("help.back")}};
      }
      break;
    case Screen::Verify:
      page = {{"enter", tr("help.done")}, {"esc", tr("help.back")}, {"r", tr("help.rescan")}};
      break;
  }
  return {page, global};
}

Element render_help(App& a) {
  auto items = help_items(a);
  Elements body;
  auto section = [&](const std::vector<HelpItem>& list) {
    for (const auto& it : list) {
      body.push_back(hbox({text(" " + it.key) | color(Color::Yellow) | bold |
                               size(WIDTH, EQUAL, 16),
                           text(it.desc)}));
    }
  };
  body.push_back(text(tr("help.current")) | bold);
  section(items.first);
  body.push_back(separator());
  body.push_back(text(tr("help.global")) | bold);
  section(items.second);

  // Keep the overlay usable on short terminals: scroll the body, never the
  // title or the hint line.
  constexpr int kVisibleRows = 14;
  int total = static_cast<int>(body.size());
  int max_scroll = std::max(0, total - kVisibleRows);
  a.help_scroll = std::clamp(a.help_scroll, 0, max_scroll);

  Elements lines;
  lines.push_back(text(tr("help.title")) | bold | color(Color::Cyan));
  lines.push_back(separator());
  int end = std::min(total, a.help_scroll + kVisibleRows);
  for (int i = a.help_scroll; i < end; ++i) {
    lines.push_back(std::move(body[static_cast<std::size_t>(i)]));
  }
  lines.push_back(separator());
  lines.push_back(text(tr("help.hint")) | dim);
  return vbox(std::move(lines)) | border | clear_under | bgcolor(Color::Black);
}

Element render_quit(App& a) {
  Elements lines;
  lines.push_back(text(tr("quit.title")) | bold | color(Color::Yellow));
  lines.push_back(separator());
  std::string labels[3] = {tr("quit.yes"), tr("quit.no"), tr("quit.save")};
  for (int i = 0; i < 3; ++i) {
    Element e = text(" " + labels[i] + " ");
    if (i == a.quit_sel) {
      e |= inverted;
      e |= bold;
    }
    lines.push_back(hbox({text(i == a.quit_sel ? "▸" : " "), e}));
  }
  if (!a.quit_error.empty()) {
    lines.push_back(separator());
    lines.push_back(text(a.quit_error) | color(Color::Red));
  }
  lines.push_back(separator());
  lines.push_back(text(tr("quit.hint")) | dim);
  return vbox(std::move(lines)) | border | clear_under | bgcolor(Color::Black);
}

Element render_screen(App& a) {
  switch (a.screen) {
    case Screen::Home:
      return render_home(a);
    case Screen::Config:
    case Screen::Settings:
      return render_form(a);
    case Screen::Repos:
      return render_repos(a);
    case Screen::Sources:
      return render_sources(a);
    case Screen::Browse:
      return render_browse(a);
    case Screen::Maint:
      return render_maint(a);
    case Screen::Report:
      return render_report(a);
    case Screen::Diff:
      return render_diff(a);
    case Screen::Confirm:
      return render_confirm(a);
    case Screen::Verify:
      return render_verify(a);
  }
  return text("?");
}

std::string maint_action_label(const App& a) {
  if (a.maint_action == "delete") return tr("maint.delete");
  if (a.maint_action == "clean") return tr("maint.clean");
  return tr("maint.export");
}

Element render_maint_confirm(App& a) {
  Elements lines;
  lines.push_back(text(maint_action_label(a)) | bold | color(Color::Yellow));
  lines.push_back(separator());
  lines.push_back(text(tr("maint.will_run")) | bold);
  for (std::size_t i = 0; i < a.maint_cmds.size() && i < 10; ++i) {
    lines.push_back(text("  " + a.maint_cmds[i].display) | dim);
  }
  if (a.maint_cmds.size() > 10) lines.push_back(text("  ...") | dim);
  if (a.maint_action == "delete") {
    bool any_src = std::any_of(a.maint_items.begin(), a.maint_items.end(),
                               [](const MaintItem& it) {
                                 return it.selected && it.kind == MaintItem::Kind::Source;
                               });
    if (any_src) lines.push_back(text(tr("maint.source_note")) | color(Color::Yellow));
  }
  lines.push_back(separator());
  if (a.maint_action == "export") {
    lines.push_back(text(tr("maint.export_hint")) | bold);
  } else {
    std::string hint = a.maint_action == "clean" ? trf("maint.clean_hint", a.codename)
                                                 : trf("maint.delete_hint", a.codename);
    lines.push_back(text(hint) | color(Color::Yellow));
    lines.push_back(hbox({text("> "), text(a.maint_typed) | bold | inverted}));
  }
  if (!a.maint_error.empty()) lines.push_back(text(a.maint_error) | color(Color::Red));
  lines.push_back(separator());
  lines.push_back(text(std::string("[enter/y] ") + tr("help.enter") + "   [esc] " +
                       tr("help.back")) |
                   dim);
  return vbox(std::move(lines)) | border | clear_under | bgcolor(Color::Black);
}

Element render_detail(App& a) {
  constexpr int kVisibleRows = 22;
  int total = static_cast<int>(a.detail_lines.size());
  int max_scroll = std::max(0, total - kVisibleRows);
  a.detail_scroll = std::clamp(a.detail_scroll, 0, max_scroll);
  Elements lines;
  lines.push_back(text(a.detail_title) | bold | color(Color::Cyan));
  lines.push_back(separator());
  int end = std::min(total, a.detail_scroll + kVisibleRows);
  for (int i = a.detail_scroll; i < end; ++i) {
    lines.push_back(text(a.detail_lines[static_cast<std::size_t>(i)]));
  }
  if (max_scroll > 0) {
    lines.push_back(text(trf("help.scroll_pos", a.detail_scroll + 1, max_scroll)) | dim);
  }
  lines.push_back(separator());
  lines.push_back(text(tr("help.hint")) | dim);
  return vbox(std::move(lines)) | border | clear_under | bgcolor(Color::Black);
}

Element modal_backdrop() {
  // Paint an opaque full-screen layer beneath a popup.  Without it, any cell
  // the popup does not explicitly draw would keep the underlying screen.
  return filler() | clear_under | bgcolor(Color::Black);
}

Element render_app(App& a) {
  Element base = render_screen(a);
  if (a.maint_confirm) return dbox({base, modal_backdrop(), render_maint_confirm(a) | center});
  if (a.help_open) return dbox({base, modal_backdrop(), render_help(a) | center});
  if (a.detail_open) return dbox({base, modal_backdrop(), render_detail(a) | center});
  if (a.quit_dialog) return dbox({base, modal_backdrop(), render_quit(a) | center});
  return base;
}

// ---------------------------------------------------------------------------
// event handling
// ---------------------------------------------------------------------------

void pop_utf8(std::string& s) {
  if (s.empty()) return;
  std::size_t i = s.size() - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  s.erase(i);
}

bool is_ctrl_s(const Event& e) { return e.input() == std::string(1, 19); }
bool is_ctrl_char(const Event& e, char letter) {
  return e.input() == std::string(1, static_cast<char>(letter - 'a' + 1));
}

int tab_count() { return 4; }

void switch_tab(App& a, int dir) {
  apply_fields(a);
  a.tab = (a.tab + dir + tab_count()) % tab_count();
  a.focus = 0;
  a.toast.clear();
  build_fields(a);
}

void add_repo_action(App& a) {
  apply_fields(a);
  Repo r;
  r.name = "repo" + std::to_string(a.cfg.repos.size() + 1);
  a.cfg.repos.push_back(r);
  a.edit_repo = static_cast<int>(a.cfg.repos.size()) - 1;
  build_fields(a);
  a.toast = tr("settings.added");
}

void delete_repo_action(App& a) {
  if (a.cfg.repos.size() <= 1) {
    a.toast = tr("settings.last_repo");
    return;
  }
  apply_fields(a);
  a.cfg.repos.erase(a.cfg.repos.begin() + a.edit_repo);
  a.edit_repo = std::max(0, a.edit_repo - 1);
  build_fields(a);
  a.toast = tr("settings.deleted");
}

void cycle_repo(App& a, int dir) {
  apply_fields(a);
  int n = static_cast<int>(a.cfg.repos.size());
  if (n <= 0) return;
  a.edit_repo = (a.edit_repo + dir + n) % n;
  a.focus = 0;
  build_fields(a);
}

bool edit_field(Field& f, const Event& e) {
  if (f.type == Field::Type::Bool) {
    if (e == Event::Character(' ')) {
      f.value = f.value == "true" ? "false" : "true";
      return true;
    }
    return false;
  }
  if (f.type == Field::Type::Choice) {
    if (e == Event::ArrowLeft || e == Event::ArrowRight || e == Event::Character(' ')) {
      int idx = 0;
      for (std::size_t i = 0; i < f.choices.size(); ++i) {
        if (f.choices[i] == f.value) idx = static_cast<int>(i);
      }
      int n = static_cast<int>(f.choices.size());
      if (n == 0) return true;
      bool back = e == Event::ArrowLeft;
      idx = (idx + (back ? n - 1 : 1)) % n;
      f.value = f.choices[static_cast<std::size_t>(idx)];
      return true;
    }
    return false;
  }
  if (e == Event::Backspace) {
    pop_utf8(f.value);
    return true;
  }
  if (e.is_character()) {
    std::string ch = e.character();
    if (f.type == Field::Type::Int) {
      for (char c : ch) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') f.value.push_back(c);
      }
    } else {
      f.value += ch;
    }
    return true;
  }
  return false;
}

void execute_publish(App& a, ScreenInteractive& scr) {
  a.published = true;
  a.exec_log.clear();
  scr.WithRestoredIO([&] {
    std::cout << "\n";
    a.exec = execute_commands(a.cmds, a.art.dir, [&](std::string_view s) {
      std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
      std::cout.flush();
    });
    std::cout.flush();
  })();
  for (const auto& r : a.exec.results) {
    a.exec_log += "$ " + display_argv(r.argv) + "\n";
    if (r.skipped) a.exec_log += trf("exec.skipped", display_argv(r.argv)) + "\n";
    else a.exec_log += r.output;
  }

  Repo& r = current_repo(a);
  fs::path base = effective_base(r, a.conf);
  a.verify_issues = verify_publish(base, a.codename, a.component, a.art, selection_archs(a));
  a.screen = Screen::Verify;
  scr.PostEvent(Event::Custom);
}

void open_quit(App& a) {
  a.quit_dialog = true;
  a.quit_sel = 1;  // safe default: "No"
  a.quit_error.clear();
}

void do_quit(App& a, ScreenInteractive& scr, bool save) {
  if (save) {
    UiState st;
    Repo& r = current_repo(a);
    st.repo = r.name;
    st.codename = a.codename.empty() ? r.codename : a.codename;
    st.component = a.component.empty() ? r.component : a.component;
    st.tab = a.tab;
    st.show_unchanged = a.show_all_unchanged;
    std::string err;
    if (!save_state(st, a.state_path, &err)) {
      a.quit_error = trf("quit.save_failed", err);
      return;
    }
  }
  scr.Exit();
}

bool handle_quit_dialog(App& a, const Event& e, ScreenInteractive& scr) {
  if (e == Event::Escape || e == Event::Character('q') || e == Event::Character('Q')) {
    a.quit_dialog = false;
    return true;
  }
  if (e == Event::ArrowLeft || e == Event::ArrowUp) {
    a.quit_sel = (a.quit_sel + 2) % 3;
    return true;
  }
  if (e == Event::ArrowRight || e == Event::ArrowDown) {
    a.quit_sel = (a.quit_sel + 1) % 3;
    return true;
  }
  if (e == Event::Character('y') || e == Event::Character('Y')) {
    do_quit(a, scr, false);
    return true;
  }
  if (e == Event::Character('n') || e == Event::Character('N')) {
    a.quit_dialog = false;
    return true;
  }
  if (e == Event::Character('s') || e == Event::Character('S')) {
    do_quit(a, scr, true);
    return true;
  }
  if (e == Event::Return) {
    if (a.quit_sel == 0) do_quit(a, scr, false);
    else if (a.quit_sel == 1) a.quit_dialog = false;
    else do_quit(a, scr, true);
    return true;
  }
  return true;  // modal: swallow everything else
}

bool handle_help(App& a, const Event& e) {
  if (e == Event::Escape || e == Event::Character('?') || e == Event::Character('q') ||
      e == Event::Character('Q') || e == Event::Return) {
    a.help_open = false;
  } else if (e == Event::ArrowUp || e == Event::PageUp) {
    a.help_scroll = std::max(0, a.help_scroll - 1);
  } else if (e == Event::ArrowDown || e == Event::PageDown) {
    a.help_scroll += 1;  // clamped during rendering
  }
  return true;  // modal: swallow all keys
}

bool handle_detail(App& a, const Event& e) {
  if (e == Event::Escape || e == Event::Character('?') || e == Event::Character('q') ||
      e == Event::Character('Q') || e == Event::Return) {
    a.detail_open = false;
  } else if (e == Event::ArrowUp || e == Event::PageUp) {
    a.detail_scroll = std::max(0, a.detail_scroll - 1);
  } else if (e == Event::ArrowDown || e == Event::PageDown) {
    a.detail_scroll += 1;  // clamped while rendering
  }
  return true;
}

bool handle_maint_confirm(App& a, const Event& e, ScreenInteractive& scr) {
  if (e == Event::Escape) {
    a.maint_confirm = false;
    a.maint_typed.clear();
    a.maint_error.clear();
    return true;
  }
  if (a.maint_action == "export") {
    if (e == Event::Character('y') || e == Event::Character('Y') || e == Event::Return) {
      run_maint(a, scr);
    }
    return true;
  }
  if (e == Event::Backspace) {
    pop_utf8(a.maint_typed);
    a.maint_error.clear();
    return true;
  }
  if (e == Event::Return) {
    if (a.maint_typed != a.codename) {
      a.maint_error = tr("maint.wrong_codename");
      return true;
    }
    run_maint(a, scr);
    return true;
  }
  if (e.is_character()) {
    a.maint_typed += e.character();
    a.maint_error.clear();
  }
  return true;
}

bool handle_form(App& a, const Event& e, ScreenInteractive& scr) {
  bool editing = !a.fields.empty() &&
                 (a.focus >= 0 && a.focus < static_cast<int>(a.fields.size()));
  bool text_focus = editing &&
                    (a.fields[static_cast<std::size_t>(a.focus)].type == Field::Type::Text ||
                     a.fields[static_cast<std::size_t>(a.focus)].type == Field::Type::Int);

  if (is_ctrl_s(e)) {
    if (save_form(a)) {
      rebuild_repo_rows(a);
      if (a.screen == Screen::Config) {
        a.screen = Screen::Home;
        a.home_sel = 0;
        rebuild_repo_rows(a);
      }
    }
    return true;
  }
  if (e == Event::Escape) {
    if (a.screen == Screen::Settings) {
      a.screen = a.return_screen;
      build_fields(a);
    } else if (a.cfg_exists) {
      a.screen = Screen::Home;
    } else {
      a.toast = tr("hint.quit");
    }
    return true;
  }
  if (a.screen == Screen::Settings && (e == Event::Tab || e == Event::TabReverse)) {
    switch_tab(a, e == Event::Tab ? 1 : -1);
    return true;
  }
  if (e == Event::Tab || e == Event::TabReverse) {
    int n = static_cast<int>(a.fields.size());
    if (n > 0) {
      int dir = e == Event::Tab ? 1 : -1;
      a.focus = (a.focus + dir + n) % n;
    }
    return true;
  }
  if (a.screen == Screen::Settings) {
    if (is_ctrl_char(e, 'a')) {
      add_repo_action(a);
      return true;
    }
    if (is_ctrl_char(e, 'd')) {
      delete_repo_action(a);
      return true;
    }
    if (!text_focus && (e == Event::Character('[') || e == Event::Character(']'))) {
      cycle_repo(a, e == Event::Character(']') ? 1 : -1);
      return true;
    }
    if (!text_focus && e == Event::Character('a')) {
      add_repo_action(a);
      return true;
    }
    if (!text_focus && e == Event::Character('d')) {
      delete_repo_action(a);
      return true;
    }
    // Left/right always switch tabs here; text fields do not use arrows for
    // editing, and choice fields can be cycled with Space.
    if (e == Event::ArrowLeft || e == Event::ArrowRight) {
      switch_tab(a, e == Event::ArrowRight ? 1 : -1);
      return true;
    }
  }
  if (e == Event::ArrowUp || e == Event::ArrowDown) {
    int n = static_cast<int>(a.fields.size());
    if (n > 0) {
      int dir = e == Event::ArrowUp ? -1 : 1;
      a.focus = (a.focus + dir + n) % n;
    }
    return true;
  }
  if (editing && edit_field(a.fields[static_cast<std::size_t>(a.focus)], e)) return true;
  return false;
}

bool handle_event(App& a, const Event& e, ScreenInteractive& scr) {
  if (a.quit_dialog) return handle_quit_dialog(a, e, scr);
  if (a.maint_confirm) return handle_maint_confirm(a, e, scr);
  if (a.help_open) return handle_help(a, e);
  if (a.detail_open) return handle_detail(a, e);

  // While the user retypes the codename to confirm a replacing publish, plain
  // letters must reach the text input (codenames like "sid" or "buster" would
  // otherwise trigger the global single-key shortcuts below).
  const bool typing_codename =
      a.screen == Screen::Confirm && a.replace_needed &&
      (a.scene_warning.empty() || a.ack_scene);

  // In the directory browser, printable letters are a jump-to-prefix filter,
  // so they must not trigger the global shortcuts either.
  const bool browse_typing =
      a.screen == Screen::Browse && e.is_character() && e != Event::Character(' ') &&
      e != Event::Character('?');

  if (e == Event::Character('?') && !typing_codename) {
    a.help_open = true;
    a.help_scroll = 0;
    return true;
  }
  if (a.screen == Screen::Config || a.screen == Screen::Settings) {
    return handle_form(a, e, scr);
  }
  if (!typing_codename && !browse_typing && e == Event::Character('q')) {
    open_quit(a);
    return true;
  }
  if (!typing_codename && !browse_typing && e == Event::Character('l')) {
    set_language(a, current_lang() == Lang::ZhCN ? Lang::EnUS : Lang::ZhCN);
    if (a.cfg_exists) {
      std::string err;
      save_config(a.cfg, a.cfg_path, &err);
    }
    return true;
  }
  if (!typing_codename && !browse_typing && e == Event::Character('s')) {
    a.return_screen = a.screen;
    a.screen = Screen::Settings;
    a.tab = 0;
    a.toast.clear();
    build_fields(a);
    return true;
  }
  if (!typing_codename && !browse_typing && e == Event::Character('t') &&
      a.screen != Screen::Repos) {
    a.repos_purpose = ReposPurpose::Upload;
    a.screen = Screen::Repos;
    rebuild_repo_rows(a);
    return true;
  }
  if (!typing_codename && !browse_typing && e == Event::Character('o') &&
      (a.screen == Screen::Report || a.screen == Screen::Diff)) {
    open_sources(a);
    return true;
  }

  switch (a.screen) {
    case Screen::Home: {
      if (e == Event::ArrowUp || e == Event::ArrowDown) {
        int dir = e == Event::ArrowUp ? -1 : 1;
        a.home_sel = (a.home_sel + dir + 4) % 4;
        return true;
      }
      if (e == Event::Character('1') || e == Event::Character('2') ||
          e == Event::Character('3') || e == Event::Character('4')) {
        a.home_sel = e.character()[0] - '1';
        // fall through to open below
      }
      if (e == Event::Return || (e.is_character() && e.character().size() == 1 &&
                                 e.character()[0] >= '1' && e.character()[0] <= '4')) {
        switch (a.home_sel) {
          case 0:
            a.repos_purpose = ReposPurpose::Upload;
            a.screen = Screen::Repos;
            rebuild_repo_rows(a);
            break;
          case 1:
            a.repos_purpose = ReposPurpose::Maintain;
            a.screen = Screen::Repos;
            rebuild_repo_rows(a);
            break;
          case 2:
            a.return_screen = Screen::Home;
            a.screen = Screen::Settings;
            a.tab = 0;
            a.toast.clear();
            build_fields(a);
            break;
          case 3:
            a.return_screen = Screen::Home;
            a.screen = Screen::Settings;
            a.tab = 3;
            a.toast.clear();
            build_fields(a);
            break;
        }
        return true;
      }
      return false;
    }
    case Screen::Repos: {
      if (e == Event::ArrowUp || e == Event::ArrowDown) {
        int dir = e == Event::ArrowUp ? -1 : 1;
        int n = static_cast<int>(a.repo_rows.size());
        if (n == 0) return true;
        int i = a.repo_sel;
        do {
          i = (i + dir + n) % n;
        } while (!a.repo_rows[static_cast<std::size_t>(i)].selectable && i != a.repo_sel);
        a.repo_sel = i;
        return true;
      }
      if (e == Event::Return) {
        if (a.repo_sel >= 0 && a.repo_sel < static_cast<int>(a.repo_rows.size())) {
          const RepoRow& r = a.repo_rows[static_cast<std::size_t>(a.repo_sel)];
          if (r.selectable) {
            a.repo_idx = r.repo;
            a.codename = r.codename;
            a.component = r.component;
            a.cfg.default_repo = a.cfg.repos[static_cast<std::size_t>(a.repo_idx)].name;
            if (a.repos_purpose == ReposPurpose::Maintain) {
              load_maint(a);
              a.screen = Screen::Maint;
            } else {
              open_sources(a);
            }
          }
        }
        return true;
      }
      if (e == Event::Character('r')) {
        rebuild_repo_rows(a);
        return true;
      }
      if (e == Event::Character('v')) {
        show_repo_details(a);
        return true;
      }
      if (e == Event::Escape) {
        a.screen = Screen::Home;
        return true;
      }
      return false;
    }
    case Screen::Maint: {
      int rows = static_cast<int>(a.maint_rows.size());
      if (e == Event::ArrowUp || e == Event::ArrowDown) {
        if (rows > 0) {
          int dir = e == Event::ArrowUp ? -1 : 1;
          a.maint_sel = (a.maint_sel + dir + rows) % rows;
        }
        return true;
      }
      if (e == Event::Return) {
        if (a.maint_sel < 0 || a.maint_sel >= rows) return true;
        const MaintRow& row = a.maint_rows[static_cast<std::size_t>(a.maint_sel)];
        if (row.type == MaintRow::Type::Group) {
          a.maint_group_open[row.group] = !a.maint_group_open[row.group];
          rebuild_maint_rows(a);
        } else if (row.item >= 0 && row.item < static_cast<int>(a.maint_items.size())) {
          auto& it = a.maint_items[static_cast<std::size_t>(row.item)];
          it.selected = !it.selected;
        }
        return true;
      }
      auto mark_current = [&](bool on) {
        if (a.maint_sel < 0 || a.maint_sel >= rows) return;
        const MaintRow& row = a.maint_rows[static_cast<std::size_t>(a.maint_sel)];
        if (row.type == MaintRow::Type::Item) {
          if (row.item >= 0 && row.item < static_cast<int>(a.maint_items.size()))
            a.maint_items[static_cast<std::size_t>(row.item)].selected = on;
        } else {
          for (auto& it : a.maint_items) {
            if ((it.kind == MaintItem::Kind::Source) == (row.group == 0)) it.selected = on;
          }
        }
      };
      if (e == Event::Character('-')) {
        mark_current(true);
        return true;
      }
      if (e == Event::Character('+') || e == Event::Character('=')) {
        mark_current(false);
        return true;
      }
      if (e == Event::Character(' ')) {
        int item = current_maint(a);
        if (item >= 0 && item < static_cast<int>(a.maint_items.size())) {
          auto& it = a.maint_items[static_cast<std::size_t>(item)];
          it.selected = !it.selected;
        }
        return true;
      }
      if (e == Event::Character('a') || e == Event::Character('i')) {
        bool all_on = std::all_of(a.maint_items.begin(), a.maint_items.end(),
                                  [](const MaintItem& it) { return it.selected; });
        for (auto& it : a.maint_items) {
          it.selected = e == Event::Character('a') ? true : !all_on;
        }
        return true;
      }
      if (e == Event::Character('g') || e == Event::Character('x') ||
          e == Event::Character('d')) {
        prepare_maint_delete(a);
        return true;
      }
      if (e == Event::Character('e')) {
        prepare_maint_export(a);
        return true;
      }
      if (e == Event::Character('c')) {
        prepare_maint_clean(a);
        return true;
      }
      if (e == Event::Character('r')) {
        load_maint(a);
        return true;
      }
      if (e == Event::Escape) {
        a.screen = Screen::Repos;
        a.repos_purpose = ReposPurpose::Maintain;
        return true;
      }
      return false;
    }
    case Screen::Sources: {
      int rows = static_cast<int>(a.src_rows.size());
      if (e == Event::ArrowUp || e == Event::ArrowDown) {
        if (rows > 0) {
          int dir = e == Event::ArrowUp ? -1 : 1;
          a.src_sel = (a.src_sel + dir + rows) % rows;
        }
        return true;
      }
      if (e == Event::Return) {
        if (a.src_sel < 0 || a.src_sel >= rows) return true;
        const SrcRow& row = a.src_rows[static_cast<std::size_t>(a.src_sel)];
        if (row.type == SrcRow::Type::Group) {
          a.src_group_open[row.group] = !a.src_group_open[row.group];
          rebuild_src_rows(a);
        } else if (row.set >= 0 && row.set < static_cast<int>(a.set_selected.size())) {
          auto idx = static_cast<std::size_t>(row.set);
          a.set_selected[idx] = !a.set_selected[idx];
        }
        return true;
      }
      auto mark_current = [&](bool on) {
        if (a.src_sel < 0 || a.src_sel >= rows) return;
        const SrcRow& row = a.src_rows[static_cast<std::size_t>(a.src_sel)];
        if (row.type == SrcRow::Type::Set) {
          if (row.set >= 0 && row.set < static_cast<int>(a.set_selected.size()))
            a.set_selected[static_cast<std::size_t>(row.set)] = on;
        } else {
          for (std::size_t i = 0; i < a.package_sets.size(); ++i) {
            if (static_cast<int>(a.set_status[i]) == row.group)
              a.set_selected[i] = on;
          }
        }
      };
      if (e == Event::Character('+') || e == Event::Character('=')) {
        mark_current(true);
        return true;
      }
      if (e == Event::Character('-')) {
        mark_current(false);
        return true;
      }
      if (e == Event::Character(' ')) {
        mark_current(!(current_set(a) >= 0 &&
                       current_set(a) < static_cast<int>(a.set_selected.size()) &&
                       a.set_selected[static_cast<std::size_t>(current_set(a))]));
        return true;
      }
      if (e == Event::Character('a') || e == Event::Character('i')) {
        bool all_on = std::all_of(a.set_selected.begin(), a.set_selected.end(),
                                  [](bool v) { return v; });
        for (std::size_t i = 0; i < a.set_selected.size(); ++i) {
          a.set_selected[i] = e == Event::Character('a') ? true : !all_on;
        }
        return true;
      }
      if (e == Event::Character('g')) {
        stage_selection(a);
        return true;
      }
      if (e == Event::Character('d')) {
        open_browse(a);
        return true;
      }
      if (e == Event::Character('r')) {
        reload_sources(a);
        return true;
      }
      if (e == Event::Character('v')) {
        show_set_details(a);
        return true;
      }
      if (e == Event::Escape) {
        a.screen = a.sources_return;
        return true;
      }
      return false;
    }
    case Screen::Browse: {
      int n = static_cast<int>(a.browse_dirs.size()) + 1;  // row 0 = use current
      if (e == Event::ArrowUp || e == Event::ArrowDown) {
        int dir = e == Event::ArrowUp ? -1 : 1;
        a.browse_sel = (a.browse_sel + dir + n) % n;
        return true;
      }
      if (e == Event::Return || e == Event::ArrowRight) {
        if (a.browse_sel == 0) {
          choose_dir(a, a.browse_dir);
        } else if (a.browse_sel > 0 && a.browse_sel < n) {
          a.browse_dir = a.browse_dirs[static_cast<std::size_t>(a.browse_sel - 1)];
          load_browse(a);
        }
        return true;
      }
      if (is_ctrl_char(e, 'r')) {
        load_browse(a);
        return true;
      }
      if (e == Event::Backspace) {
        if (!a.browse_filter.empty()) {
          a.browse_filter.pop_back();
          browse_jump(a);
        } else {
          fs::path parent = a.browse_dir.parent_path();
          if (!parent.empty() && parent != a.browse_dir) {
            a.browse_dir = parent;
            load_browse(a);
          }
        }
        return true;
      }
      if (e == Event::ArrowLeft) {
        fs::path parent = a.browse_dir.parent_path();
        if (!parent.empty() && parent != a.browse_dir) {
          a.browse_dir = parent;
          load_browse(a);
        }
        return true;
      }
      if (e == Event::Escape) {
        if (!a.browse_filter.empty()) {
          a.browse_filter.clear();
          a.browse_note.clear();
        } else {
          a.screen = Screen::Sources;
        }
        return true;
      }
      if (e == Event::Character(' ')) {
        choose_dir(a, a.browse_dir);
        return true;
      }
      if (e.is_character()) {
        std::string ch = e.character();
        // Ignore control characters; everything printable narrows the jump
        // prefix.  Space stays reserved for "use this directory".
        for (char c : ch) {
          if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) a.browse_filter.push_back(c);
        }
        browse_jump(a);
        return true;
      }
      return false;
    }
    case Screen::Report: {
      if (e == Event::ArrowUp) {
        a.issue_scroll = std::max(0, a.issue_scroll - 1);
        return true;
      }
      if (e == Event::ArrowDown) {
        a.issue_scroll = std::min(std::max(0, static_cast<int>(a.issues.size()) - 1),
                                  a.issue_scroll + 1);
        return true;
      }
      if (e == Event::Character('r')) {
        rescan(a);
        return true;
      }
      if (e == Event::Return) {
        if (a.blocked) {
          a.toast = tr("confirm.blocked");
        } else {
          a.screen = Screen::Diff;
          a.row_sel = 0;
        }
        return true;
      }
      if (e == Event::Character('p')) {
        std::string all;
        for (const auto& c : a.cmds) all += c.display + "\n";
        auto tool = copy_to_clipboard(all);
        a.toast = tool.empty() ? tr("common.na") : tool;
        return true;
      }
      if (e == Event::Escape) {
        a.screen = Screen::Sources;
        return true;
      }
      return false;
    }
    case Screen::Diff: {
      if (e == Event::ArrowUp) {
        a.row_sel = std::max(0, a.row_sel - 1);
        return true;
      }
      if (e == Event::ArrowDown) {
        a.row_sel = std::min(std::max(0, static_cast<int>(a.rows.size()) - 1), a.row_sel + 1);
        return true;
      }
      if (e == Event::Character('u')) {
        a.show_all_unchanged = !a.show_all_unchanged;
        FoldPolicy fp;
        fp.unchanged_fold = a.cfg.ui.unchanged_fold;
        fp.show_unchanged = a.cfg.ui.show_unchanged;
        fp.detail = a.cfg.ui.detail;
        fp.collapsed = &a.collapsed;
        a.rows = flatten_diff(a.diff, fp, a.show_all_unchanged);
        return true;
      }
      if (e == Event::Character(' ')) {
        if (a.row_sel >= 0 && a.row_sel < static_cast<int>(a.rows.size())) {
          // rebuild the path of the selected node by walking the tree
          std::vector<std::string> path;
          const ChangeNode* target = a.rows[static_cast<std::size_t>(a.row_sel)].node;
          if (target) {
            std::function<bool(const ChangeNode&, std::vector<std::string>&)> find =
                [&](const ChangeNode& n, std::vector<std::string>& cur) {
                  cur.push_back(n.name);
                  if (&n == target) return true;
                  for (const auto& c : n.children) {
                    if (find(c, cur)) return true;
                  }
                  cur.pop_back();
                  return false;
                };
            for (const auto& root : a.diff.roots) {
              path.clear();
              if (find(root, path)) break;
            }
            std::string key;
            for (const auto& p : path) {
              if (!key.empty()) key += "/";
              key += p;
            }
            if (!key.empty()) {
              if (a.collapsed.count(key)) a.collapsed.erase(key);
              else a.collapsed.insert(key);
              FoldPolicy fp;
              fp.unchanged_fold = a.cfg.ui.unchanged_fold;
              fp.show_unchanged = a.cfg.ui.show_unchanged;
              fp.detail = a.cfg.ui.detail;
              fp.collapsed = &a.collapsed;
              a.rows = flatten_diff(a.diff, fp, a.show_all_unchanged);
            }
          }
        }
        return true;
      }
      if (e == Event::Character('p')) {
        std::string all;
        for (const auto& c : a.cmds) all += c.display + "\n";
        auto tool = copy_to_clipboard(all);
        a.toast = tool.empty() ? tr("common.na") : tool;
        return true;
      }
      if (e == Event::Escape) {
        a.screen = Screen::Report;
        return true;
      }
      if (e == Event::Return) {
        if (a.blocked) {
          a.toast = tr("confirm.blocked");
          return true;
        }
        if (!a.diff.has_changes()) {
          a.toast = tr("diff.no_changes");
          return true;
        }
        a.screen = Screen::Confirm;
        a.typed.clear();
        a.confirm_error.clear();
        return true;
      }
      return false;
    }
    case Screen::Confirm: {
      if (e == Event::Escape) {
        a.screen = Screen::Diff;
        return true;
      }
      if (!a.scene_warning.empty() && !a.ack_scene) {
        if (e == Event::Character('y') || e == Event::Return) {
          a.ack_scene = true;
          return true;
        }
        return false;
      }
      if (a.replace_needed) {
        if (e == Event::Backspace) {
          pop_utf8(a.typed);
          return true;
        }
        if (e.is_character()) {
          a.typed += e.character();
          return true;
        }
        if (e == Event::Return) {
          if (a.typed != a.codename) {
            a.confirm_error = trf("confirm.type_codename", a.codename);
            return true;
          }
          execute_publish(a, scr);
          return true;
        }
        return false;
      }
      if (e == Event::Character('y') || e == Event::Character('x') ||
          e == Event::Character('Y') || e == Event::Character('X') || e == Event::Return) {
        execute_publish(a, scr);
        return true;
      }
      return false;
    }
    case Screen::Verify: {
      if (e == Event::Return) {
        // A result screen: Enter means "done" and returns to the home menu.
        a.screen = Screen::Home;
        a.home_sel = 0;
        return true;
      }
      if (e == Event::Escape) {
        a.screen = a.published ? Screen::Diff : Screen::Report;
        return true;
      }
      if (e == Event::Character('r')) {
        rescan(a);
        a.screen = Screen::Report;
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

}  // namespace

// Some terminals keep software flow control (IXON) enabled, which swallows
// Ctrl+S/Ctrl+Q before they ever reach the application.  Disable it for the
// lifetime of the TUI and restore the previous settings on exit.
class FlowControlGuard {
 public:
  FlowControlGuard() {
    if (::isatty(STDIN_FILENO) == 0) return;
    if (::tcgetattr(STDIN_FILENO, &saved_) != 0) return;
    termios t = saved_;
    t.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF);
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0) active_ = true;
  }
  ~FlowControlGuard() {
    if (active_) ::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
  }
  FlowControlGuard(const FlowControlGuard&) = delete;
  FlowControlGuard& operator=(const FlowControlGuard&) = delete;

 private:
  termios saved_{};
  bool active_ = false;
};

int run_tui(AppOptions opts) {
  App a;
  a.opts = opts;
  a.cfg = opts.cfg;
  a.cfg_path = opts.cfg_path;
  a.cfg_exists = opts.cfg_exists;
  a.cfg_error = opts.cfg_error;
  a.repo_idx = 0;

  if (opts.cfg_exists && opts.cfg_error.empty() && !opts.cfg.repos.empty()) {
    a.screen = Screen::Home;
  } else {
    a.screen = Screen::Config;
    if (a.cfg.repos.empty()) a.cfg.repos.push_back(Repo{});
    a.edit_repo = 0;
    build_fields(a);
  }
  if (!opts.artifacts.empty()) a.cfg.artifacts = opts.artifacts;
  if (a.cfg.artifacts.empty()) a.cfg.artifacts = fs::current_path();
  a.source_dir = a.cfg.artifacts;
  a.opts.cfg = a.cfg;

  rebuild_repo_rows(a);
  if (a.repo_rows.empty()) {
    a.screen = Screen::Config;
    build_fields(a);
  }

  // Restore the last UI selection, if any.  The cache is optional and a
  // corrupt/missing file simply leaves the defaults untouched.
  a.state_path = default_state_path();
  UiState st = load_state(a.state_path);
  if (st.tab >= 0 && st.tab < tab_count()) a.tab = st.tab;
  a.show_all_unchanged = st.show_unchanged;
  if (!st.repo.empty()) {
    for (std::size_t i = 0; i < a.cfg.repos.size(); ++i) {
      if (a.cfg.repos[i].name == st.repo) {
        a.repo_idx = static_cast<int>(i);
        break;
      }
    }
  }
  a.codename = st.codename;
  a.component = st.component;
  for (std::size_t i = 0; i < a.repo_rows.size(); ++i) {
    const RepoRow& r = a.repo_rows[i];
    if (!r.selectable || r.repo != a.repo_idx) continue;
    if (!st.codename.empty() && r.codename != st.codename) continue;
    if (!st.component.empty() && r.component != st.component) continue;
    a.repo_sel = static_cast<int>(i);
    break;
  }

  auto scr = ScreenInteractive::Fullscreen();
  auto comp = Renderer([&] { return render_app(a); });
  comp |= CatchEvent([&](const Event& e) { return handle_event(a, e, scr); });
  FlowControlGuard flow_guard;
  scr.Loop(comp);
  cleanup_stage(a.stage_dir);
  for (const auto& p : a.stale_stages) cleanup_stage(p);
  return a.exit_code;
}

}  // namespace rpp
