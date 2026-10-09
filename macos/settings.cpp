#include "settings.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace launcher {
namespace {
struct Assignment {
  std::string key;
  size_t start, value, end;
};
struct Document {
  std::vector<Assignment> assignments;
  size_t table;
};
std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

// Scan a whole value, rather than interpreting assignment-like text inside
// arrays, inline tables or multiline strings as launcher settings.
size_t ValueEnd(const std::string& text, size_t pos) {
  char quote = 0;
  bool triple = false;
  int depth = 0;
  for (size_t i = pos; i < text.size(); ++i) {
    const char c = text[i];
    if (quote) {
      if (quote == '"' && c == '\\') { if (i + 1 < text.size()) ++i; continue; }
      if (c == quote) {
        if (!triple) quote = 0;
        else if (text.compare(i, 3, std::string(3, quote)) == 0) { quote = 0; i += 2; }
      }
    } else if (c == '#' && depth == 0) {
      return i;
    } else if (c == '#') {
      const size_t newline = text.find('\n', i);
      if (newline == std::string::npos) return text.size();
      i = newline;
    } else if (c == '"' || c == '\'') {
      quote = c;
      triple = text.compare(i, 3, std::string(3, c)) == 0;
      if (triple) i += 2;
    } else if (c == '[' || c == '{') {
      ++depth;
    } else if (c == ']' || c == '}') {
      --depth;
    } else if (c == '\n' && depth == 0) {
      return i;
    }
  }
  return text.size();
}
Document Parse(const std::string& text) {
  Document result{{}, text.size()};
  size_t pos = 0;
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;
  while (pos < text.size()) {
    const size_t start = pos;
    const size_t newline = text.find('\n', pos);
    const size_t lineEnd = newline == std::string::npos ? text.size() : newline;
    pos = lineEnd + (newline != std::string::npos);
    const size_t first = text.find_first_not_of(" \t\r", start);
    if (first == std::string::npos || first >= lineEnd || text[first] == '#') continue;
    if (text[first] == '[') { result.table = start; break; }
    const size_t equal = text.find('=', first);
    if (equal == std::string::npos || equal >= lineEnd) continue;
    std::string key = Trim(text.substr(first, equal - first));
    if (key.size() >= 2 && (key.front() == '"' || key.front() == '\'') && key.back() == key.front())
      key = key.substr(1, key.size() - 2);
    // The game's cvar names are simple bare keys. Leave other TOML forms alone.
    if (key.empty() || !std::all_of(key.begin(), key.end(), [](unsigned char c) {
          return std::isalnum(c) || c == '_';
        })) continue;
    size_t value = equal + 1;
    while (value < text.size() && (text[value] == ' ' || text[value] == '\t')) ++value;
    const size_t end = ValueEnd(text, value);
    size_t trimmed = end;
    while (trimmed > value && std::isspace(static_cast<unsigned char>(text[trimmed - 1]))) --trimmed;
    result.assignments.push_back({key, start, value, trimmed});
    const size_t after = text.find('\n', end);
    pos = after == std::string::npos ? text.size() : after + 1;
  }
  return result;
}
}  // namespace

std::string Setting(const std::string& document, const std::string& key,
                    const std::string& fallback) {
  for (const auto& entry : Parse(document).assignments) {
    if (entry.key != key) continue;
    std::string value = document.substr(entry.value, entry.end - entry.value);
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
      return value.substr(1, value.size() - 2);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      std::string decoded;
      for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size()) {
          const char c = value[++i];
          if (c == 'n') decoded += '\n';
          else if (c == 'r') decoded += '\r';
          else if (c == 't') decoded += '\t';
          else if (c == '"' || c == '\\') decoded += c;
          else return fallback;  // Never rewrite an unfamiliar string escape.
        } else decoded += value[i];
      }
      return decoded;
    }
    return value;
  }
  return fallback;
}

std::string UpdateSettings(const std::string& document,
                          const std::map<std::string, std::string>& changes) {
  const auto parsed = Parse(document);
  auto remaining = changes;
  struct Edit { size_t start, size; std::string value; };
  std::vector<Edit> edits;
  for (const auto& entry : parsed.assignments) {
    const auto found = changes.find(entry.key);
    if (found == changes.end()) continue;
    if (found->second.empty()) {
      // An empty replacement removes a deprecated alias, not a quoted "".
      const size_t newline = document.find('\n', entry.end);
      const size_t end = newline == std::string::npos ? document.size() : newline + 1;
      edits.push_back({entry.start, end - entry.start, ""});
    } else edits.push_back({entry.value, entry.end - entry.value, found->second});
    remaining.erase(entry.key);
  }
  std::string added;
  for (const auto& [key, value] : remaining)
    if (!value.empty()) added += key + " = " + value + "\n";
  if (!added.empty()) {
    if (parsed.table > 0 && document[parsed.table - 1] != '\n') added.insert(0, "\n");
    edits.push_back({parsed.table, 0, added});
  }
  std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start > b.start; });
  std::string result = document;
  for (const auto& edit : edits) result.replace(edit.start, edit.size, edit.value);
  return result;
}

std::string Quote(const std::string& value) {
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { result += '\\'; result += c; }
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else if (c < 0x20 || c == 0x7F) {
      const char* hex = "0123456789ABCDEF";
      result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
    }
    else result += c;
  }
  return result + '"';
}

void RenderScale(std::map<std::string, std::string>& changes, int scale) {
  changes["resolution_scale"] = "";
  changes["draw_resolution_scale_x"] = std::to_string(scale);
  changes["draw_resolution_scale_y"] = std::to_string(scale);
}
void DisplayVSync(std::map<std::string, std::string>& changes, bool enabled) {
  for (const auto* key : {"vulkan_allow_present_mode_immediate", "vulkan_allow_present_mode_mailbox",
                          "vulkan_allow_present_mode_fifo_relaxed"})
    changes[key] = enabled ? "false" : "true";
}
}  // namespace launcher
