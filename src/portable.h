// Portable mode (a "user-data" folder next to the game); see portable.cpp.

#pragma once

#include <rex/rex_app.h>

namespace rr6 {

// In OnConfigurePaths: if a user-data folder exists next to the game program
// or one level up, keep the user data and caches there.
void UsePortableFolder(rex::PathConfig& paths);

// Once logging runs: says in the log which folder is used, if portable.
void LogPortableFolder();

}  // namespace rr6
