#include "exec.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace rpp {
namespace {

RunOutput run_impl(const std::vector<std::string>& argv, const fs::path& cwd,
                   const std::function<void(std::string_view)>& sink) {
  RunOutput r;
  if (argv.empty() || argv.front().empty()) return r;

  int fds[2];
  if (pipe(fds) != 0) {
    r.out = std::strerror(errno);
    return r;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    r.out = std::strerror(errno);
    return r;
  }
  if (pid == 0) {
    close(fds[0]);
    if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fds[1], STDERR_FILENO) < 0) _exit(126);
    close(fds[1]);
    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) _exit(125);
    }
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }

  close(fds[1]);
  r.started = true;

  char buf[8192];
  for (;;) {
    ssize_t n = read(fds[0], buf, sizeof buf);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    r.out.append(buf, static_cast<std::size_t>(n));
    if (sink) sink(std::string_view(buf, static_cast<std::size_t>(n)));
  }
  close(fds[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) r.exit_code = 128 + WTERMSIG(status);
  return r;
}

std::string first_token(const std::string& s) {
  auto sp = s.find_first_of(" \t\r\n");
  return sp == std::string::npos ? s : s.substr(0, sp);
}

}  // namespace

RunOutput run_capture(const std::vector<std::string>& argv, const fs::path& cwd) {
  return run_impl(argv, cwd, {});
}

RunOutput run_stream(const std::vector<std::string>& argv, const fs::path& cwd,
                     const std::function<void(std::string_view)>& sink) {
  return run_impl(argv, cwd, sink);
}

std::string display_argv(const std::vector<std::string>& argv) {
  std::string out;
  for (const auto& a : argv) {
    if (!out.empty()) out += ' ';
    bool simple = !a.empty();
    for (char c : a) {
      if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '.' || c == '_' ||
            c == '-' || c == ':' || c == '=' || c == '~' || c == '+' || c == '@')) {
        simple = false;
        break;
      }
    }
    if (simple) {
      out += a;
    } else {
      out += '\'';
      for (char c : a) {
        if (c == '\'') out += "'\\''";
        else out += c;
      }
      out += '\'';
    }
  }
  return out;
}

bool has_command(const std::string& name) {
  auto r = run_capture({"sh", "-c", "command -v \"$1\" >/dev/null 2>&1", "sh", name});
  return r.exit_code == 0;
}

std::string sha256_file(const fs::path& p) {
  auto r = run_capture({"sha256sum", "--", p.string()});
  if (r.exit_code != 0) return {};
  return first_token(r.out);
}

std::string md5_file(const fs::path& p) {
  auto r = run_capture({"md5sum", "--", p.string()});
  if (r.exit_code != 0) return {};
  return first_token(r.out);
}

std::uint64_t file_size_of(const fs::path& p) {
  std::error_code ec;
  auto n = fs::file_size(p, ec);
  return ec ? 0 : static_cast<std::uint64_t>(n);
}

std::string copy_to_clipboard(const std::string& text) {
  for (const char* tool : {"wl-copy", "xclip", "xsel", "pbcopy"}) {
    if (!has_command(tool)) continue;
    std::vector<std::string> argv;
    if (std::string(tool) == "xclip") argv = {"xclip", "-selection", "clipboard"};
    else if (std::string(tool) == "xsel") argv = {"xsel", "--clipboard", "--input"};
    else argv = {tool};
    auto r = run_capture(argv, {});
    (void)r;
    // xclip/xsel need stdin; use a shell-free fallback with a temp file.
    // wl-copy also accepts stdin, which we cannot feed via run_capture yet.
    // Use sh with a quoted heredoc-free approach: printf '%s' "$1" | tool
    auto rr = run_capture({"sh", "-c", "printf '%s' \"$2\" | \"$1\"", "sh", tool, text});
    if (rr.exit_code == 0) return tool;
  }
  return {};
}

}  // namespace rpp
