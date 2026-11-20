#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "checks.hpp"
#include "config.hpp"
#include "diff.hpp"
#include "exec.hpp"
#include "i18n.hpp"
#include "reprepro_conf.hpp"
#include "scan.hpp"
#include "stage.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

static int g_fail = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond   \
                << "\n";                                                     \
      ++g_fail;                                                              \
    }                                                                        \
  } while (0)

static fs::path make_tmp() {
  std::string tmpl = "/tmp/rpp-test-XXXXXX";
  char* p = mkdtemp(tmpl.data());
  return p ? fs::path(p) : fs::path("/tmp");
}

static void write_file(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p);
  out << text;
}

static void test_dsc_parse() {
  fs::path dir = make_tmp();
  std::string content =
      "-----BEGIN PGP SIGNED MESSAGE-----\n"
      "Hash: SHA256\n"
      "\n"
      "Format: 3.0 (quilt)\n"
      "Source: ccid\n"
      "Binary: libccid, ccid\n"
      "Architecture: any\n"
      "Version: 1.8.4-1\n"
      "Distribution: trixie\n"
      "Priority: optional\n"
      "Maintainer: Test <t@example.com>\n"
      "Files:\n"
      " 0123456789abcdef0123456789abcdef 1234 ccid_1.8.4.orig.tar.xz\n"
      " fedcba9876543210fedcba9876543210 5678 ccid_1.8.4-1.debian.tar.xz\n"
      "Checksums-Sha256:\n"
      " aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 1234 ccid_1.8.4.orig.tar.xz\n"
      " bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb 5678 ccid_1.8.4-1.debian.tar.xz\n"
      "-----BEGIN PGP SIGNATURE-----\n"
      "iQIzBAEBCgAdFiEE\n"
      "-----END PGP SIGNATURE-----\n";
  auto p = dir / "ccid_1.8.4-1.dsc";
  write_file(p, content);
  auto d = rpp::parse_dsc(p);
  CHECK(d.field("Source") == "ccid");
  CHECK(d.field("Priority") == "optional");
  CHECK(d.field("Version") == "1.8.4-1");
  CHECK(!d.has("Hash") || d.field("Hash") != "SHA256");
  CHECK(d.files.size() == 2);
  CHECK(d.files[0].name == "ccid_1.8.4.orig.tar.xz");
  CHECK(d.files[0].size == 1234);
  CHECK(d.files[0].md5 == "0123456789abcdef0123456789abcdef");
  CHECK(d.files[0].sha256 == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(rpp::dsc_source_name(d) == "ccid");
  fs::remove_all(dir);
}

static void test_reprepro_conf() {
  fs::path dir = make_tmp();
  auto p = dir / "conf" / "distributions";
  write_file(p,
             "Codename: trixie\n"
             "Suite: stable\n"
             "Components: main contrib\n"
             "Architectures: amd64 arm64 source\n"
             "SignWith: ABCD1234\n"
             "\n"
             "Codename: bookworm\n"
             "Components: main\n"
             "Architectures: amd64\n");
  auto conf = rpp::parse_repo_conf(p);
  CHECK(conf.ok);
  CHECK(conf.distributions.size() == 2);
  CHECK(conf.distributions[0].codename == "trixie");
  CHECK(conf.distributions[0].components.size() == 2);
  CHECK(conf.distributions[0].architectures.size() == 3);
  CHECK(conf.distributions[0].sign_with == "ABCD1234");
  CHECK(rpp::find_dist(conf, "trixie") != nullptr);
  CHECK(rpp::find_dist(conf, "nope") == nullptr);
  fs::remove_all(dir);
}

static void test_config_roundtrip() {
  fs::path dir = make_tmp();
  auto path = dir / "config.toml";
  rpp::Config c;
  c.default_repo = "spark";
  c.artifacts = "/tmp/pub";
  c.ui.detail = "full";
  c.ui.unchanged_fold = 5;
  c.ui.language = "en-US";
  c.policy.allow_replace = true;
  rpp::Repo r;
  r.name = "spark";
  r.base = "/srv/reprepro";
  r.conf = "/srv/reprepro/conf/distributions";
  r.codename = "trixie";
  r.component = "main";
  r.architectures = {"amd64", "arm64"};
  c.repos.push_back(r);

  std::string err;
  CHECK(rpp::save_config(c, path, &err));
  auto load = rpp::load_config(path);
  CHECK(load.ok);
  CHECK(load.exists);
  CHECK(load.cfg.repos.size() == 1);
  CHECK(load.cfg.repos[0].name == "spark");
  CHECK(load.cfg.repos[0].architectures.size() == 2);
  CHECK(load.cfg.repos[0].codename == "trixie");
  CHECK(load.cfg.ui.detail == "full");
  CHECK(load.cfg.ui.unchanged_fold == 5);
  CHECK(load.cfg.ui.language == "en-US");
  CHECK(load.cfg.policy.allow_replace);
  fs::remove_all(dir);
}

static void test_minimal_repo_config() {
  fs::path dir = make_tmp();
  write_file(dir / "conf" / "distributions",
             "Origin: Test\n"
             "Codename: bookworm\n"
             "Suite: bookworm\n"
             "Architectures: amd64 arm64 source\n"
             "Components: main\n"
             "Description: test\n"
             "\n"
             "Origin: Test\n"
             "Codename: trixie\n"
             "Suite: trixie\n"
             "Architectures: amd64 armhf source\n"
             "Components: main test\n"
             "Description: test\n");

  auto path = dir / "config.toml";
  write_file(path,
             "version = 1\n"
             "[paths]\n"
             "artifacts = \"/tmp/pub\"\n"
             "[[repo]]\n"
             "base = \"" + dir.string() + "\"\n");

  auto load = rpp::load_config(path);
  CHECK(load.ok);
  CHECK(load.cfg.repos.size() == 1);
  const auto& r = load.cfg.repos[0];
  CHECK(r.name == dir.filename().string());
  CHECK(r.conf == dir / "conf" / "distributions");
  CHECK(r.codename.empty());
  CHECK(r.component.empty());
  CHECK(r.architectures.empty());
  CHECK(rpp::repo_conf_path(r) == dir / "conf" / "distributions");

  std::vector<std::string> errs;
  CHECK(rpp::validate_config(load.cfg, &errs));
  CHECK(errs.empty());

  // A relative conf is resolved against base, not the current directory.
  auto path2 = dir / "config2.toml";
  write_file(path2,
             "version = 1\n"
             "[[repo]]\n"
             "base = \"" + dir.string() + "\"\n"
             "conf = \"conf/distributions\"\n");
  auto load2 = rpp::load_config(path2);
  CHECK(load2.ok);
  CHECK(load2.cfg.repos[0].conf == "conf/distributions");
  CHECK(rpp::repo_conf_path(load2.cfg.repos[0]) == dir / "conf" / "distributions");
  errs.clear();
  CHECK(rpp::validate_config(load2.cfg, &errs));

  // Missing config file is reported by validation instead of being accepted.
  auto bad = dir / "bad.toml";
  write_file(bad, "version = 1\n[[repo]]\nbase = \"" + dir.string() + "\"\nconf = \"missing/distributions\"\n");
  auto load3 = rpp::load_config(bad);
  CHECK(load3.ok);
  errs.clear();
  CHECK(!rpp::validate_config(load3.cfg, &errs));
  CHECK(!errs.empty());
  fs::remove_all(dir);
}

static rpp::Artifacts make_artifacts(const fs::path& dir) {
  rpp::Artifacts a;
  a.dir = dir;
  auto dsc_path = dir / "ccid_1.0-1.dsc";
  auto deb_path = dir / "libccid_1.0-1_amd64.deb";
  write_file(dsc_path, "");
  write_file(deb_path, "");
  rpp::DscInfo d;
  d.path = dsc_path;
  d.fields["Source"] = "ccid";
  d.fields["Version"] = "1.0-1";
  d.fields["Binary"] = "libccid";
  d.fields["Priority"] = "optional";
  d.files.push_back({"ccid_1.0.orig.tar.xz", 10, "md5", ""});
  d.has_files_block = true;
  a.dscs.push_back(d);
  rpp::DebInfo b;
  b.path = deb_path;
  b.package = "libccid";
  b.version = "1.0-1";
  b.arch = "amd64";
  b.source = "ccid";
  b.priority = "optional";
  a.debs.push_back(b);
  a.scene = rpp::Scene::Both;
  return a;
}

static void test_diff() {
  fs::path dir = make_tmp();
  auto art = make_artifacts(dir);
  write_file(dir / "ccid_1.0.orig.tar.xz", "0123456789");

  rpp::RepoState empty;
  rpp::DiffInput in;
  in.art = &art;
  in.state = &empty;
  in.codename = "trixie";
  in.component = "main";
  in.archs = {"amd64", "source"};
  auto tree = rpp::build_diff(in);
  CHECK(tree.roots.size() == 1);
  CHECK(tree.added == 3);  // dsc + orig tar + deb
  CHECK(tree.modified == 0);

  rpp::RepoState st;
  st.ok = true;
  st.index["ccid|source"] = "1.0-1";
  st.index["libccid|amd64"] = "1.0-1";
  st.pool["ccid_1.0-1.dsc"] = {"ccid_1.0-1.dsc", "", rpp::sha256_file(dir / "ccid_1.0-1.dsc"), 0};
  st.pool["ccid_1.0.orig.tar.xz"] = {"ccid_1.0.orig.tar.xz", "",
                                     rpp::sha256_file(dir / "ccid_1.0.orig.tar.xz"), 10};
  st.pool["libccid_1.0-1_amd64.deb"] = {"libccid_1.0-1_amd64.deb", "",
                                        rpp::sha256_file(dir / "libccid_1.0-1_amd64.deb"), 0};
  in.state = &st;
  auto same = rpp::build_diff(in);
  CHECK(same.added == 0);
  CHECK(same.unchanged == 3);

  st.pool["ccid_1.0.orig.tar.xz"].sha256 = "other";
  auto mod = rpp::build_diff(in);
  CHECK(mod.modified == 1);

  fs::remove_all(dir);
}

static rpp::ChangeNode leaf(const std::string& name, rpp::Change c) {
  rpp::ChangeNode n;
  n.kind = rpp::ChangeNode::Kind::File;
  n.name = name;
  n.change = c;
  return n;
}

static void test_folding() {
  rpp::DiffTree tree;
  rpp::ChangeNode root;
  root.kind = rpp::ChangeNode::Kind::Codename;
  root.name = "trixie";
  root.change = rpp::Change::Added;
  rpp::ChangeNode comp;
  comp.kind = rpp::ChangeNode::Kind::Component;
  comp.name = "main";
  comp.change = rpp::Change::Added;
  rpp::ChangeNode arch;
  arch.kind = rpp::ChangeNode::Kind::Arch;
  arch.name = "amd64";
  arch.change = rpp::Change::Added;
  rpp::ChangeNode pkg;
  pkg.kind = rpp::ChangeNode::Kind::Package;
  pkg.name = "ccid";
  pkg.change = rpp::Change::Added;
  pkg.meta = "1.0";
  for (int i = 0; i < 20; ++i) {
    pkg.children.push_back(leaf("f" + std::to_string(i), i == 0 ? rpp::Change::Added : rpp::Change::Unchanged));
  }
  arch.children.push_back(pkg);
  comp.children.push_back(arch);
  root.children.push_back(comp);
  tree.roots.push_back(root);

  rpp::FoldPolicy pol;
  pol.unchanged_fold = 15;
  pol.detail = "normal";
  auto rows = rpp::flatten_diff(tree, pol);
  bool has_fold = false;
  int file_rows = 0;
  for (const auto& r : rows) {
    if (r.fold) has_fold = true;
    else if (r.node && r.node->kind == rpp::ChangeNode::Kind::File) ++file_rows;
  }
  CHECK(has_fold);
  CHECK(file_rows == 1);

  pol.unchanged_fold = -1;
  rows = rpp::flatten_diff(tree, pol);
  file_rows = 0;
  for (const auto& r : rows) {
    if (!r.fold && r.node && r.node->kind == rpp::ChangeNode::Kind::File) ++file_rows;
  }
  CHECK(file_rows == 20);

  pol.unchanged_fold = 0;
  rows = rpp::flatten_diff(tree, pol);
  file_rows = 0;
  for (const auto& r : rows) {
    if (!r.fold && r.node && r.node->kind == rpp::ChangeNode::Kind::File) ++file_rows;
  }
  CHECK(file_rows == 1);

  pol.unchanged_fold = 15;
  pol.detail = "summary";
  rows = rpp::flatten_diff(tree, pol);
  file_rows = 0;
  for (const auto& r : rows) {
    if (!r.fold && r.node && r.node->kind == rpp::ChangeNode::Kind::File) ++file_rows;
  }
  CHECK(file_rows == 0);
}

static void test_checks() {
  fs::path dir = make_tmp();
  auto art = make_artifacts(dir);
  // real artifact files so size checks are meaningful
  write_file(dir / "ccid_1.0.orig.tar.xz", "0123456789");
  write_file(dir / "ccid_1.0-1.dsc", "x");
  write_file(dir / "libccid_1.0-1_amd64.deb", "x");

  rpp::Policy pol;
  pol.buildinfo = "ignore";
  rpp::CheckInput ci;
  ci.art = &art;
  ci.policy = &pol;
  ci.target_archs = {"amd64"};
  ci.codename = "trixie";
  auto issues = rpp::run_checks(ci);
  CHECK(rpp::has_blocking(issues));  // missing md5 source file entry with wrong md5

  // Fix the dsc entry: md5 of "0123456789" is 781e5e245d69b566979b86e28d23f2c7
  art.dscs[0].files[0].md5 = "781e5e245d69b566979b86e28d23f2c7";
  art.dscs[0].files[0].size = 10;
  issues = rpp::run_checks(ci);
  CHECK(!rpp::has_blocking(issues));

  // Missing Priority is blocking
  art.dscs[0].fields.erase("Priority");
  issues = rpp::run_checks(ci);
  CHECK(rpp::has_blocking(issues));

  fs::remove_all(dir);
}

static void test_state_roundtrip() {
  fs::path dir = make_tmp();
  auto path = dir / "state.toml";
  rpp::UiState st;
  st.repo = "spark";
  st.codename = "trixie";
  st.component = "main";
  st.tab = 2;
  st.show_unchanged = true;
  std::string err;
  CHECK(rpp::save_state(st, path, &err));
  CHECK(fs::exists(path));
  auto load = rpp::load_state(path);
  CHECK(load.repo == "spark");
  CHECK(load.codename == "trixie");
  CHECK(load.component == "main");
  CHECK(load.tab == 2);
  CHECK(load.show_unchanged);
  // missing file is not an error
  auto none = rpp::load_state(dir / "absent.toml");
  CHECK(none.repo.empty());
  fs::remove_all(dir);
}

static void test_package_sets() {
  rpp::Artifacts a;
  a.dir = "/tmp/sets";
  rpp::DscInfo d1;
  d1.path = "/tmp/sets/foo_1.0-1.dsc";
  d1.fields["Source"] = "foo";
  d1.fields["Version"] = "1.0-1";
  rpp::DscInfo d2;
  d2.path = "/tmp/sets/bar_2.0-1.dsc";
  d2.fields["Source"] = "bar";
  d2.fields["Version"] = "2.0-1";
  a.dscs = {d1, d2};

  rpp::DebInfo b1;
  b1.path = "/tmp/sets/foo_1.0-1_amd64.deb";
  b1.package = "foo";
  b1.version = "1.0-1";
  b1.arch = "amd64";
  b1.source = "foo";
  rpp::DebInfo b2;
  b2.path = "/tmp/sets/foo_1.0-1_arm64.deb";
  b2.package = "foo";
  b2.version = "1.0-1";
  b2.arch = "arm64";
  b2.source = "foo";
  rpp::DebInfo b3;
  b3.path = "/tmp/sets/foo_2.0-1_amd64.deb";
  b3.package = "foo";
  b3.version = "2.0-1";
  b3.arch = "amd64";
  b3.source = "foo";
  a.debs = {b1, b2, b3};
  a.scene = rpp::Scene::Both;

  auto sets = rpp::group_artifacts(a);
  CHECK(sets.size() == 3);  // bar 2.0, foo 1.0, foo 2.0
  // Select only the first set (sorted by source, then version -> bar first).
  std::vector<bool> sel(sets.size(), false);
  sel[0] = true;
  auto only = rpp::select_artifacts(a, sets, sel);
  CHECK(only.dscs.size() == 1);
  CHECK(rpp::dsc_source_name(only.dscs[0]) == "bar");
  CHECK(only.debs.empty());
  CHECK(only.scene == rpp::Scene::SourceOnly);

  // Select foo 1.0: two binaries, no source.  Metadata is dropped because the
  // selection is a subset.
  a.buildinfos.push_back("/tmp/sets/foo_1.0-1_amd64.buildinfo");
  sel.assign(sets.size(), false);
  for (std::size_t i = 0; i < sets.size(); ++i) {
    if (sets[i].source == "foo" && sets[i].version == "1.0-1") sel[i] = true;
  }
  auto foo10 = rpp::select_artifacts(a, sets, sel);
  CHECK(foo10.debs.size() == 2);
  CHECK(foo10.dscs.size() == 1);
  CHECK(foo10.buildinfos.empty());
  CHECK(foo10.scene == rpp::Scene::Both);

  // Everything selected keeps the metadata.
  sel.assign(sets.size(), true);
  auto everything = rpp::select_artifacts(a, sets, sel);
  CHECK(everything.buildinfos.size() == 1);
  CHECK(everything.dscs.size() == 2);
  CHECK(everything.debs.size() == 3);
}

static void test_stage_snapshot() {
  fs::path dir = make_tmp();
  write_file(dir / "foo_1.0-1.dsc", "Source: foo\nVersion: 1.0-1\n");
  write_file(dir / "foo_1.0.orig.tar.xz", "tarball");
  write_file(dir / "foo_1.0-1_amd64.deb", "not really a deb");

  rpp::Artifacts a;
  a.dir = dir;
  rpp::DscInfo d;
  d.path = dir / "foo_1.0-1.dsc";
  d.fields["Source"] = "foo";
  d.fields["Version"] = "1.0-1";
  d.files.push_back(rpp::FileEntry{"foo_1.0.orig.tar.xz", 7, {}, {}});
  a.dscs = {d};
  rpp::DebInfo b;
  b.path = dir / "foo_1.0-1_amd64.deb";
  b.package = "foo";
  b.version = "1.0-1";
  b.arch = "amd64";
  a.debs = {b};
  a.scene = rpp::Scene::Both;

  auto st = rpp::stage_artifacts(a, dir / "stage");
  CHECK(st.ok);
  CHECK(fs::exists(st.dir / "foo_1.0-1.dsc"));
  CHECK(fs::exists(st.dir / "foo_1.0.orig.tar.xz"));
  CHECK(fs::exists(st.dir / "foo_1.0-1_amd64.deb"));
  CHECK(st.art.dir == st.dir);
  CHECK(st.art.dscs.size() == 1 && st.art.dscs[0].path.parent_path() == st.dir);
  CHECK(st.art.debs.size() == 1 && st.art.debs[0].path.parent_path() == st.dir);

  // Mutating the staging copy must not affect the source.
  write_file(st.dir / "foo_1.0-1.dsc", "changed");
  std::ifstream in(dir / "foo_1.0-1.dsc");
  std::string line;
  std::getline(in, line);
  CHECK(line == "Source: foo");

  rpp::cleanup_stage(st.dir);
  CHECK(!fs::exists(st.dir));
  fs::remove_all(dir);
}

static void test_version_compare() {
  using rpp::deb_version_compare;
  CHECK(deb_version_compare("1.0", "1.1") < 0);
  CHECK(deb_version_compare("1.1", "1.0") > 0);
  CHECK(deb_version_compare("1.0", "1.0") == 0);
  CHECK(deb_version_compare("1.0~rc1", "1.0") < 0);
  CHECK(deb_version_compare("1.0~~", "1.0~") < 0);
  CHECK(deb_version_compare("1:1.0", "2.0") > 0);
  CHECK(deb_version_compare("1.0-1", "1.0-2") < 0);
  CHECK(deb_version_compare("1.0-1~bpo1", "1.0-1") < 0);
  CHECK(deb_version_compare("1.8.10-1", "1.8.9-1") > 0);
  CHECK(deb_version_compare("2.0", "2.0-0") == 0);
}

static void test_i18n() {
  rpp::set_lang(rpp::Lang::ZhCN);
  CHECK(rpp::tr("scan.scene.both").find(".deb") != std::string::npos);
  rpp::set_lang(rpp::Lang::EnUS);
  CHECK(rpp::tr("scan.scene.both").find(".deb") != std::string::npos);
  bool ok = false;
  CHECK(rpp::parse_lang("zh-CN", &ok) == rpp::Lang::ZhCN && ok);
  CHECK(rpp::parse_lang("en-US", &ok) == rpp::Lang::EnUS && ok);
  rpp::parse_lang("xx", &ok);
  CHECK(!ok);
  rpp::set_lang(rpp::Lang::ZhCN);
  CHECK(rpp::human_size(1024) == "1.0 KiB");
  CHECK(rpp::strip_source_version("ccid (1.0-1)") == "ccid");
}

int main() {
  test_dsc_parse();
  test_reprepro_conf();
  test_config_roundtrip();
  test_minimal_repo_config();
  test_diff();
  test_folding();
  test_checks();
  test_state_roundtrip();
  test_package_sets();
  test_stage_snapshot();
  test_version_compare();
  test_i18n();
  if (g_fail) {
    std::cerr << g_fail << " test(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
