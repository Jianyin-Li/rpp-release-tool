#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rpp {

namespace fs = std::filesystem;

enum class Severity { Ok, Warn, Block };

struct Issue {
  Severity sev = Severity::Ok;
  std::string code;
  std::string msg;
  std::string detail;
  fs::path file;
};

enum class Scene { Empty, BinaryOnly, SourceOnly, Both };

struct FileEntry {
  std::string name;
  std::uint64_t size = 0;
  std::string md5;
  std::string sha256;
};

struct DscInfo {
  fs::path path;
  std::map<std::string, std::string> fields;
  std::vector<FileEntry> files;  // merged from Files + Checksums-Sha256
  bool has_files_block = false;
  bool has_sha256_block = false;

  std::string field(const std::string& k) const {
    auto it = fields.find(k);
    return it == fields.end() ? std::string{} : it->second;
  }
  bool has(const std::string& k) const { return fields.count(k) != 0; }
};

struct DebInfo {
  fs::path path;
  std::string package;
  std::string version;
  std::string arch;
  std::string source;   // source package name, version suffix stripped
  std::string section;
  std::string priority;
  std::string depends;
};

struct Artifacts {
  fs::path dir;
  std::vector<DscInfo> dscs;
  std::vector<DebInfo> debs;
  std::vector<fs::path> changes;
  std::vector<fs::path> buildinfos;
  std::vector<fs::path> detached_sigs;
  Scene scene = Scene::Empty;
};

struct Repo {
  std::string name;
  fs::path base;
  fs::path conf = "conf/distributions";
  std::string codename;
  std::string component;
  std::vector<std::string> architectures;
  fs::path incoming;
};

struct Distribution {
  std::string codename;
  std::string suite;
  std::vector<std::string> components;
  std::vector<std::string> architectures;
  std::string sign_with;
  std::string origin;
  std::string label;
};

struct Policy {
  bool require_both = true;            // prompt when only .deb/.dsc is present
  std::string missing_arch = "warn";   // error | warn | ignore
  std::string buildinfo = "warn";      // error | warn | ignore
  std::string sha256 = "error";        // error | warn | ignore
  bool allow_replace = false;
};

struct UiConfig {
  std::string detail = "normal";  // summary | normal | full
  int unchanged_fold = 15;        // -1 never fold, 0 never show
  std::string language = "auto";  // auto | zh-CN | en-US
  bool show_unchanged = true;
};

struct Config {
  int version = 1;
  std::string default_repo;
  std::vector<Repo> repos;
  fs::path artifacts;
  Policy policy;
  UiConfig ui;
};

enum class Change { Unchanged, Added, Modified, Deleted };

struct ChangeNode {
  enum class Kind { Codename, Component, Arch, Package, File, Index };
  Kind kind = Kind::File;
  std::string name;
  std::string meta;
  Change change = Change::Unchanged;
  std::vector<ChangeNode> children;
};

struct DiffTree {
  std::vector<ChangeNode> roots;  // codename nodes
  int added = 0;
  int modified = 0;
  int unchanged = 0;
  bool has_changes() const { return added + modified > 0; }
};

struct Command {
  std::vector<std::string> argv;
  std::string display;
  bool destructive = false;
};

struct CommandResult {
  std::vector<std::string> argv;
  int exit_code = -1;
  std::string output;
  bool skipped = false;
};

}  // namespace rpp
