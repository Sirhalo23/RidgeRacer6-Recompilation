// Portable mode: keep saves, achievements, installed DLC and caches next to
// the game instead of in the user's Documents folder (issue #31).
//
// The switch is a folder: if one called "user-data" exists next to the game
// program (bin\) or one level up, beside RR6 Launcher.exe (the package's top
// folder), the game uses it as its user data folder. Creating the folder turns
// portable mode on for every way of starting the game (the launcher, "Play
// without the launcher.bat", the Linux script); deleting or renaming it turns
// it off. Existing saves stay where they were: the player moves them across
// if they want them (README).
//
// A --user_data_root given on the command line still wins (the macOS app and
// anyone who set it on purpose). The SDK picks the folder before it reads the
// settings file, so this has to happen in OnConfigurePaths.

#include "portable.h"

#include <string>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

REXCVAR_DECLARE(std::string, user_data_root);  // defined by the SDK (runtime.cpp)
REXCVAR_DECLARE(std::string, cache_root);      // defined by the SDK

namespace rr6 {
namespace {

std::filesystem::path g_portable_folder;  // empty: not portable

}  // namespace

void UsePortableFolder(rex::PathConfig& paths) {
  if (!REXCVAR_GET(user_data_root).empty()) {
    return;
  }
  const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
  const std::filesystem::path candidates[] = {exe_dir / "user-data",
                                              exe_dir.parent_path() / "user-data"};
  for (const std::filesystem::path& folder : candidates) {
    std::error_code error;
    if (std::filesystem::is_directory(folder, error)) {
      g_portable_folder = std::filesystem::absolute(folder, error);
      if (error) {
        g_portable_folder = folder;
      }
      paths.user_data_root = g_portable_folder;
      if (REXCVAR_GET(cache_root).empty()) {
        paths.cache_root = g_portable_folder / "cache";
      }
      return;
    }
  }
}

void LogPortableFolder() {
  if (!g_portable_folder.empty()) {
    REXLOG_INFO("[portable] saves, achievements, DLC and caches are kept in {}",
                g_portable_folder.string());
  }
}

}  // namespace rr6
