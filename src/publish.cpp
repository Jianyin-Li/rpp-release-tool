#include "publish.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <set>

#include "exec.hpp"
#include "i18n.hpp"
#include "repo_state.hpp"
#include "scan.hpp"

namespace rpp {
namespace {

bool contains(const std::vector<std::string>& v, const std::string& x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}

std::string dist_archs(const Distribution& d) {
  std::string out;
  for (const auto& a : d.architectures) {
    if (!out.empty()) out += " ";
    out += a;
  }
  return out.empty() ? "-" : out;
}

std::uint64_t artifact_bytes(const Artifacts& art) {
  std::uint64_t total = 0;
  for (const auto& d : art.dscs) {
    for (const auto& f : d.files) {
      std::error_code ec;
      auto p = d.path.parent_path() / f.name;
      if (fs::exists(p, ec)) total += file_size_of(p);
    }
  }
  for (const auto& d : art.debs) total += file_size_of(d.path);
  return total;
}

}  // namespace

std::vector<Issue> run_preflight(const PreflightInput& in) {
  std::vector<Issue> r;
  const Repo& repo = in.repo;
  const RepoConf& conf = in.conf;
  const Artifacts& art = *in.art;
  const Policy& pol = *in.policy;

  if (!conf.ok) {
    r.push_back({Severity::Block, "pf.conf",
                 trf("pf.conf_missing", resolve_distributions_path(repo.conf).string()), conf.error, repo.conf});
    return r;
  }

  const Distribution* dist = find_dist(conf, repo.codename);
  if (!dist) {
    std::string names;
    for (const auto& d : conf.distributions) {
      if (!names.empty()) names += ", ";
      names += d.codename;
    }
    r.push_back({Severity::Block, "pf.codename",
                 trf("pf.codename_missing", repo.codename, resolve_distributions_path(repo.conf).string()),
                 names, repo.conf});
  } else {
    r.push_back({Severity::Ok, "pf.codename", trf("pf.codename_ok", dist->codename), {}, repo.conf});

    if (!dist->components.empty() && !contains(dist->components, repo.component)) {
      std::string names;
      for (const auto& c : dist->components) {
        if (!names.empty()) names += ", ";
        names += c;
      }
      r.push_back({Severity::Block, "pf.component",
                   trf("pf.component_missing", repo.component, dist->codename, names), {}, repo.conf});
    } else {
      r.push_back({Severity::Ok, "pf.component", trf("pf.component_ok", repo.component), {}, repo.conf});
    }

    if (!dist->architectures.empty()) {
      if (!art.dscs.empty() && !contains(dist->architectures, "source")) {
        r.push_back({Severity::Block, "pf.source_arch",
                     trf("pf.source_arch_missing", dist->codename), dist_archs(*dist), repo.conf});
      }
      for (const auto& d : art.debs) {
        if (d.arch.empty() || d.arch == "all") continue;
        if (!contains(dist->architectures, d.arch)) {
          r.push_back({Severity::Block, "pf.arch",
                       trf("pf.arch_missing", dist->codename, d.arch), dist_archs(*dist), d.path});
        }
      }
      for (const auto& a : in.target_archs) {
        if (a == "source") continue;
        if (!contains(dist->architectures, a)) {
          r.push_back({Severity::Warn, "pf.target_arch",
                       trf("pf.arch_missing", dist->codename, a), dist_archs(*dist), repo.conf});
        }
      }
    } else {
      r.push_back({Severity::Warn, "pf.arch_empty", tr("pf.arch_none"), {}, repo.conf});
    }

    if (dist->sign_with.empty()) {
      r.push_back({Severity::Warn, "pf.sign_none", tr("pf.sign_none"), {}, repo.conf});
    } else {
      auto g = run_capture({"gpg", "--batch", "--with-colons", "--list-secret-keys", dist->sign_with});
      bool has_secret = g.out.find("sec:") != std::string::npos;
      if (g.exit_code != 0 || !has_secret) {
        r.push_back({Severity::Block, "pf.sign", trf("pf.sign_missing", dist->sign_with), g.out, repo.conf});
      } else {
        r.push_back({Severity::Ok, "pf.sign", trf("pf.sign_ok", dist->sign_with), {}, repo.conf});
      }
    }
  }

  // ---- layout / permissions / lock --------------------------------------
  std::error_code ec;
  fs::path base = effective_base(repo, conf);
  if (!fs::exists(base, ec)) {
    r.push_back({Severity::Block, "pf.base_missing",
                 trf("config.base_missing", base.string()), {}, base});
  } else {
    std::set<fs::path> dirs = {base};
    for (const char* sub : {"db", "pool", "dists", "conf", "incoming"}) {
      fs::path p = base / sub;
      if (fs::exists(p, ec)) dirs.insert(p);
    }
    for (const auto& d : dirs) {
      if (access(d.c_str(), W_OK) != 0) {
        r.push_back({Severity::Block, "pf.writable", trf("pf.not_writable", d.string()), {}, d});
      }
    }

    fs::path lock = base / "db" / "lock";
    if (fs::exists(lock, ec)) {
      int fd = open(lock.c_str(), O_RDWR);
      if (fd >= 0) {
        bool busy = flock(fd, LOCK_EX | LOCK_NB) != 0;
        if (!busy) flock(fd, LOCK_UN);
        close(fd);
        if (busy) {
          r.push_back({Severity::Block, "pf.lock", trf("pf.locked", lock.string()), {}, lock});
        } else {
          r.push_back({Severity::Ok, "pf.lock", tr("pf.lock_ok"), {}, lock});
        }
      }
    }

    struct statvfs sv {};
    if (statvfs(base.c_str(), &sv) == 0) {
      std::uint64_t free_bytes = static_cast<std::uint64_t>(sv.f_bavail) * sv.f_frsize;
      std::uint64_t need = artifact_bytes(art);
      if (free_bytes < need + 50ull * 1024 * 1024) {
        r.push_back({Severity::Warn, "pf.space",
                     trf("pf.space_low", free_bytes / (1024 * 1024), need / (1024 * 1024)), {}, base});
      }
    }
  }

  if (conf.options.empty()) {
    r.push_back({Severity::Ok, "pf.options", tr("pf.options_missing"), {}, repo.conf});
  } else {
    auto it = conf.options.find("basedir");
    if (it != conf.options.end() && !it->second.empty()) {
      fs::path declared = it->second;
      if (declared != repo.base) {
        r.push_back({Severity::Warn, "pf.options_basedir",
                     trf("pf.options_mismatch", "basedir", declared.string()), {}, repo.conf});
      }
    }
  }

  // Missing target architectures covered by checks; policy kept here for clarity.
  (void)pol;
  return r;
}

std::vector<Command> build_commands(const Repo& repo, const fs::path& base, const std::string& codename,
                                    const std::string& component, const Artifacts& art,
                                    const DiffTree& diff, bool allow_replace) {
  (void)repo;  // base is passed separately (it honours conf/options basedir)
  std::vector<Command> cmds;
  auto add = [&](std::vector<std::string> argv, bool destructive = false) {
    Command c;
    c.argv = std::move(argv);
    c.display = display_argv(c.argv);
    c.destructive = destructive;
    cmds.push_back(std::move(c));
  };

  auto prefix = [&] {
    std::vector<std::string> v = {"reprepro", "-b", base.string()};
    if (!component.empty()) {
      v.push_back("-C");
      v.push_back(component);
    }
    return v;
  };

  bool need_replace = allow_replace && (diff.modified > 0);
  if (need_replace) {
    // Only the package nodes that actually changed need to be removed first.
    // Scoping by -A / -T keeps unrelated architectures and types untouched:
    // an amd64-only overwrite must not drop the arm64 build of the same package.
    struct Removal {
      std::string arch;  // "source" for source packages, "all" for arch:all debs
      std::string pkg;
      bool source = false;
    };
    std::vector<Removal> removals;
    std::set<std::string> seen;
    std::function<void(const ChangeNode&, const std::string&)> walk =
        [&](const ChangeNode& n, const std::string& arch) {
          const std::string cur = n.kind == ChangeNode::Kind::Arch ? n.name : arch;
          if (n.kind == ChangeNode::Kind::Package && n.change == Change::Modified) {
            Removal r;
            r.arch = cur;
            r.pkg = n.name;
            r.source = cur == "source";
            if (seen.insert(r.arch + "|" + r.pkg).second) removals.push_back(r);
            return;
          }
          for (const auto& c : n.children) walk(c, cur);
        };
    for (const auto& root : diff.roots) walk(root, "");

    // Source first: reprepro refuses to drop a source package still referenced
    // by binaries? It does not, but keeping the order stable makes logs readable.
    std::stable_sort(removals.begin(), removals.end(),
                     [](const Removal& a, const Removal& b) { return a.source > b.source; });
    for (const auto& r : removals) {
      auto v = prefix();
      if (r.source) {
        v.push_back("-T");
        v.push_back("dsc");
      } else if (!r.arch.empty() && r.arch != "all") {
        v.push_back("-A");
        v.push_back(r.arch);
        v.push_back("-T");
        v.push_back("deb");
      } else {
        // arch:all packages are registered under every concrete architecture,
        // and `-A all` matches nothing, so remove them unscoped.
        v.push_back("-T");
        v.push_back("deb");
      }
      v.push_back("remove");
      v.push_back(codename);
      v.push_back(r.pkg);
      add(std::move(v), true);
    }
  }

  if (!art.changes.empty() && !need_replace) {
    auto v = prefix();
    v.push_back("include");
    v.push_back(codename);
    v.push_back(art.changes.front().string());
    add(std::move(v));
    return cmds;
  }

  for (const auto& d : art.dscs) {
    auto v = prefix();
    v.push_back("includedsc");
    v.push_back(codename);
    v.push_back(d.path.string());
    add(std::move(v));
  }
  for (const auto& d : art.debs) {
    auto v = prefix();
    v.push_back("includedeb");
    v.push_back(codename);
    v.push_back(d.path.string());
    add(std::move(v));
  }
  return cmds;
}

ExecResult execute_commands(const std::vector<Command>& cmds, const fs::path& cwd,
                            const std::function<void(std::string_view)>& sink) {
  ExecResult r;
  bool stop = false;
  for (const auto& c : cmds) {
    CommandResult cr;
    cr.argv = c.argv;
    if (stop) {
      cr.skipped = true;
      r.results.push_back(std::move(cr));
      continue;
    }
    if (sink) sink("\n$ " + c.display + "\n");
    auto out = run_stream(c.argv, cwd, sink);
    cr.exit_code = out.exit_code;
    cr.output = out.out;
    if (out.exit_code != 0) {
      stop = true;
      r.all_ok = false;
    }
    r.results.push_back(std::move(cr));
  }
  return r;
}

std::vector<Issue> verify_publish(const fs::path& base, const std::string& codename,
                                  const std::string& component, const Artifacts& art,
                                  const std::vector<std::string>& archs) {
  std::vector<Issue> r;
  RepoState st = load_repo_state(base, codename, component, archs);
  if (!st.ok) {
    r.push_back({Severity::Block, "verify.state", trf("err.cannot_read", st.error, ""), {}, base});
    return r;
  }

  for (const auto& d : art.debs) {
    if (d.package.empty() || d.arch.empty()) continue;
    if (d.arch == "all") {
      // reprepro registers arch:all binaries under every concrete binary
      // architecture, so accept the package if any target arch has it.
      std::string seen_arch;
      for (const auto& a : archs) {
        if (a == "source" || a == "all") continue;
        auto it = st.index.find(d.package + "|" + a);
        if (it != st.index.end() && it->second == d.version) {
          seen_arch = a;
          break;
        }
      }
      if (!seen_arch.empty()) {
        r.push_back({Severity::Ok, "verify.present",
                     trf("verify.present", d.package, d.version, seen_arch), {}, d.path});
      } else {
        r.push_back({Severity::Block, "verify.missing",
                     trf("verify.missing", d.package, d.version, "all"), {}, d.path});
      }
      continue;
    }
    auto it = st.index.find(d.package + "|" + d.arch);
    if (it == st.index.end() || it->second != d.version) {
      r.push_back({Severity::Block, "verify.missing",
                   trf("verify.missing", d.package, d.version, d.arch), {}, d.path});
    } else {
      r.push_back({Severity::Ok, "verify.present",
                   trf("verify.present", d.package, d.version, d.arch), {}, d.path});
    }
  }
  for (const auto& d : art.dscs) {
    auto src = dsc_source_name(d);
    auto ver = d.field("Version");
    if (src.empty()) continue;
    auto it = st.index.find(src + "|source");
    if (it == st.index.end() || it->second != ver) {
      r.push_back({Severity::Block, "verify.missing",
                   trf("verify.missing", src, ver, "source"), {}, d.path});
    } else {
      r.push_back({Severity::Ok, "verify.present",
                   trf("verify.present", src, ver, "source"), {}, d.path});
    }
  }

  fs::path inrelease = base / "dists" / codename / "InRelease";
  std::error_code ec;
  if (!fs::exists(inrelease, ec)) {
    r.push_back({Severity::Warn, "verify.inrelease",
                 trf("verify.inrelease_missing", codename), {}, inrelease});
  } else {
    auto g = run_capture({"gpg", "--verify", inrelease.string()});
    if (g.exit_code == 0) {
      r.push_back({Severity::Ok, "verify.inrelease", tr("verify.inrelease_ok"), {}, inrelease});
    } else {
      r.push_back({Severity::Block, "verify.inrelease",
                   trf("verify.inrelease_bad", g.out), g.out, inrelease});
    }
  }
  return r;
}

}  // namespace rpp
