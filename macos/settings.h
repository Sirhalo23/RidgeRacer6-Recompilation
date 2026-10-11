#pragma once

#include <map>
#include <string>

namespace launcher {

// Edits only root assignments in the game's flat TOML settings. Unknown
// values, comments and tables are kept verbatim, including multiline values.
// Callers submit only settings the user actually changed.
std::string UpdateSettings(const std::string& document,
                          const std::map<std::string, std::string>& changes);
std::string Setting(const std::string& document, const std::string& key,
                    const std::string& fallback);
std::string Quote(const std::string& value);

// Shared between the UI and tests; aliases must not override a selected scale.
void RenderScale(std::map<std::string, std::string>& changes, int scale);
void DisplayVSync(std::map<std::string, std::string>& changes, bool enabled);

}  // namespace launcher
