#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "model.hpp"

namespace rpp {

struct RunOutput {
  int exit_code = -1;
  std::string out;
  bool started = false;
};

RunOutput run_capture(const std::vector<std::string>& argv, const fs::path& cwd = {});

// Streams merged stdout/stderr to sink (may be empty) and appends to captured.
RunOutput run_stream(const std::vector<std::string>& argv, const fs::path& cwd,
                     const std::function<void(std::string_view)>& sink);

std::string display_argv(const std::vector<std::string>& argv);
bool has_command(const std::string& name);

std::string md5_file(const fs::path& p);
std::string sha256_file(const fs::path& p);
std::uint64_t file_size_of(const fs::path& p);

std::string copy_to_clipboard(const std::string& text);

}  // namespace rpp
