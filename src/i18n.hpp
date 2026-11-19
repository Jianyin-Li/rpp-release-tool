#pragma once

#include <string>
#include <string_view>

namespace rpp {

enum class Lang { ZhCN, EnUS };

Lang detect_lang();
Lang parse_lang(const std::string& s, bool* ok = nullptr);
std::string lang_code(Lang l);
std::string lang_label(Lang l);

void set_lang(Lang l);
Lang current_lang();

std::string tr(std::string_view key);
const char* trc(std::string_view key);

// trf("key {}", value) -> translated text with std::format placeholders.
template <typename... Args>
std::string trf(std::string_view key, const Args&... args);

}  // namespace rpp

#include <format>

namespace rpp {
template <typename... Args>
std::string trf(std::string_view key, const Args&... args) {
  return std::vformat(tr(key), std::make_format_args(args...));
}
}  // namespace rpp
