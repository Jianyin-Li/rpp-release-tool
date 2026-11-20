#include <unistd.h>

#include <iostream>
#include <sstream>

#include "checks.hpp"
#include "config.hpp"
#include "diff.hpp"
#include "exec.hpp"
#include "i18n.hpp"
#include "publish.hpp"
#include "repo_state.hpp"
#include "reprepro_conf.hpp"
#include "scan.hpp"
#include "ui/app.hpp"

namespace rpp {
namespace {

constexpr int kExitOk = 0;
constexpr int kExitBlock = 1;
constexpr int kExitNeedAck = 2;
constexpr int kExitConfig = 3;
constexpr int kExitUsage = 4;
constexpr int kExitExec = 5;
constexpr int kExitVerify = 6;

struct Cli {
  fs::path config;
  std::string repo;
  std::string codename;
  std::string component;
  fs::path dir;
  bool no_tui = false;
  bool check_only = false;
  bool show_config = false;
  bool print_cmd = false;
  bool ack = false;
  bool replace = false;
  bool help = false;
  bool version = false;
  std::string detail;
  std::string lang;
  std::string error;
};

Cli parse_cli(int argc, char** argv) {
  Cli c;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* opt) -> std::string {
      if (i + 1 >= argc) {
        c.error = std::string(opt) + " needs a value";
        return {};
      }
      return argv[++i];
    };
    if (a == "--config") c.config = expand_user(next("--config"));
    else if (a == "--repo") c.repo = next("--repo");
    else if (a == "--codename") c.codename = next("--codename");
    else if (a == "--component") c.component = next("--component");
    else if (a == "--detail") c.detail = next("--detail");
    else if (a == "--lang") c.lang = next("--lang");
    else if (a == "--no-tui") c.no_tui = true;
    else if (a == "--check-only") c.check_only = true;
    else if (a == "--show-config") c.show_config = true;
    else if (a == "--print-cmd") c.print_cmd = true;
    else if (a == "--ack") c.ack = true;
    else if (a == "--replace") c.replace = true;
    else if (a == "--help" || a == "-h") c.help = true;
    else if (a == "--version" || a == "-V") c.version = true;
    else if (!a.empty() && a[0] == '-') {
      c.error = "unknown option: " + a;
    } else if (c.dir.empty()) {
      c.dir = expand_user(a);
    } else {
      c.error = "unexpected argument: " + a;
    }
  }
  return c;
}

void print_usage(std::ostream& os) {
  os << tr("cli.usage") << ": rpp-release-tool [options] [dir]\n";
  for (int i = 1; i <= 15; ++i) os << tr("cli.help." + std::to_string(i)) << "\n";
}

const char* sev_mark(Severity s) {
  switch (s) {
    case Severity::Block: return "x";
    case Severity::Warn: return "!";
    default: return "+";
  }
}

void print_issues(const std::vector<Issue>& issues) {
  for (const auto& i : issues) {
    std::cout << "[" << sev_mark(i.sev) << "] " << i.msg;
    if (!i.file.empty()) std::cout << "  (" << i.file.string() << ")";
    std::cout << "\n";
  }
}

std::string pick_repo_name(const Config& cfg, const std::string& cli_repo) {
  if (!cli_repo.empty()) return cli_repo;
  if (!cfg.default_repo.empty()) return cfg.default_repo;
  if (!cfg.repos.empty()) return cfg.repos.front().name;
  return {};
}

std::vector<std::string> target_archs_for(const Repo& repo, const Distribution* dist) {
  std::vector<std::string> out;
  if (dist) out = dist->architectures;
  if (out.empty()) out = repo.architectures;
  return out;
}

int run_batch(const Cli& cli, const ConfigLoad& load) {
  Config cfg = load.cfg;
  if (!cfg.ui.detail.empty() && cfg.ui.detail == "normal") {
    // cli detail override is applied below
  }
  if (!cli.detail.empty()) cfg.ui.detail = cli.detail;

  std::string repo_name = pick_repo_name(cfg, cli.repo);
  const Repo* repo = find_repo(cfg, repo_name);
  if (!repo && !cfg.repos.empty()) repo = &cfg.repos.front();
  if (!repo) {
    std::cerr << tr("config.need_repo") << "\n";
    return kExitConfig;
  }
  Repo r = *repo;
  if (!cli.codename.empty()) r.codename = cli.codename;
  if (!cli.component.empty()) r.component = cli.component;

  fs::path artifacts = cli.dir.empty() ? cfg.artifacts : cli.dir;
  if (artifacts.empty()) artifacts = fs::current_path();

  Artifacts art = scan_dir(artifacts);
  RepoConf conf = parse_repo_conf(repo_conf_path(r));
  if (r.codename.empty() && !conf.distributions.empty()) {
    r.codename = conf.distributions.front().codename;
  }
  const Distribution* dist = find_dist(conf, r.codename);
  if (r.component.empty()) {
    r.component = (dist && !dist->components.empty()) ? dist->components.front() : "main";
  }

  CheckInput ci;
  ci.art = &art;
  ci.policy = &cfg.policy;
  ci.target_archs = target_archs_for(r, dist);
  ci.codename = r.codename;
  auto issues = run_checks(ci);

  PreflightInput pi;
  pi.repo = r;
  pi.conf = conf;
  pi.art = &art;
  pi.policy = &cfg.policy;
  pi.target_archs = target_archs_for(r, dist);
  auto pf = run_preflight(pi);
  issues.insert(issues.end(), pf.begin(), pf.end());

  std::cout << trf("diff.target", r.name, r.codename, r.component) << "\n";
  std::cout << trf("scan.files", art.debs.size(), art.dscs.size()) << "\n\n";
  print_issues(issues);
  std::cout << "\n";

  fs::path base = effective_base(r, conf);
  RepoState st = load_repo_state(base, r.codename, r.component, target_archs_for(r, dist));
  DiffInput di;
  di.art = &art;
  di.state = &st;
  di.codename = r.codename;
  di.component = r.component;
  di.archs = target_archs_for(r, dist);
  DiffTree tree = build_diff(di);

  FoldPolicy fp;
  fp.unchanged_fold = cfg.ui.unchanged_fold;
  fp.show_unchanged = cfg.ui.show_unchanged;
  fp.detail = cfg.ui.detail;
  for (const auto& line : render_diff_text(tree, fp)) std::cout << line << "\n";
  std::cout << "\n";

  bool blocked = has_blocking(issues);
  bool needs_ack = cfg.policy.require_both &&
                   (art.scene == Scene::BinaryOnly || art.scene == Scene::SourceOnly);
  if (tree.modified > 0 && !cfg.policy.allow_replace) blocked = true;
  // Overwriting an existing version is destructive: --ack alone is not enough,
  // non-interactive callers must also pass --replace.
  bool needs_replace_ack = tree.modified > 0 && !cli.replace;

  auto cmds = build_commands(r, base, r.codename, r.component, art, tree, cfg.policy.allow_replace);
  if (cli.print_cmd || cli.check_only) {
    for (const auto& c : cmds) std::cout << c.display << "\n";
  }

  if (blocked) return kExitBlock;
  if (needs_ack && !cli.ack) return kExitNeedAck;
  if (needs_replace_ack) {
    std::cerr << trf("confirm.replace", tree.modified) << "\n"
              << tr("cli.replace_hint") << "\n";
    return kExitNeedAck;
  }
  if (cli.check_only || cli.print_cmd) return kExitOk;
  if (!tree.has_changes() || cmds.empty()) {
    std::cout << tr("diff.no_changes") << "\n";
    return kExitOk;
  }

  ExecResult er = execute_commands(cmds, artifacts, [](std::string_view s) {
    std::cout.write(s.data(), static_cast<std::streamsize>(s.size()));
    std::cout.flush();
  });
  if (!er.all_ok) return kExitExec;

  auto vissues = verify_publish(base, r.codename, r.component, art, target_archs_for(r, dist));
  print_issues(vissues);
  if (has_blocking(vissues)) return kExitVerify;
  std::cout << tr("verify.all_ok") << "\n";
  return kExitOk;
}

}  // namespace
}  // namespace rpp

int main(int argc, char** argv) {
  using namespace rpp;
  Cli cli = parse_cli(argc, argv);

  ConfigLoad early = load_config(cli.config);
  Lang lang = detect_lang();
  if (early.exists && early.ok && early.cfg.ui.language != "auto") {
    bool ok = false;
    lang = parse_lang(early.cfg.ui.language, &ok);
    if (!ok) lang = detect_lang();
  }
  if (!cli.lang.empty()) {
    bool ok = false;
    lang = parse_lang(cli.lang, &ok);
    if (!ok) {
      set_lang(detect_lang());
      std::cerr << trf("err.bad_value", "--lang", cli.lang) << "\n";
      return kExitUsage;
    }
  }
  set_lang(lang);

  if (!cli.error.empty()) {
    std::cerr << cli.error << "\n";
    print_usage(std::cerr);
    return kExitUsage;
  }
  if (cli.help) {
    print_usage(std::cout);
    return kExitOk;
  }
  if (cli.version) {
    std::cout << "rpp-release-tool 0.0.4\n";
    return kExitOk;
  }
  if (cli.show_config) {
    if (!early.ok) {
      std::cerr << tr("err.no_config") << "\n";
      return kExitConfig;
    }
    std::cout << config_to_string(early.cfg);
    return kExitOk;
  }

  bool tui = !cli.no_tui && isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);
  if (!tui) {
    if (!early.exists) {
      std::cerr << trf("config.conf_missing", early.path.string()) << "\n";
      std::cerr << tr("err.no_config") << "\n";
      return kExitConfig;
    }
    if (!early.ok) {
      std::cerr << trf("config.conf_bad", early.error) << "\n";
      return kExitConfig;
    }
    if (!cli.dir.empty()) early.cfg.artifacts = cli.dir;
    return run_batch(cli, early);
  }

  AppOptions opts;
  opts.cfg = early.cfg;
  opts.cfg_path = early.path;
  opts.cfg_exists = early.exists;
  opts.cfg_error = early.error;
  opts.repo_name = cli.repo;
  opts.codename = cli.codename;
  opts.component = cli.component;
  opts.artifacts = cli.dir.empty() ? early.cfg.artifacts : cli.dir;
  opts.check_only = cli.check_only;
  if (!cli.detail.empty()) opts.cfg.ui.detail = cli.detail;
  return run_tui(opts);
}
