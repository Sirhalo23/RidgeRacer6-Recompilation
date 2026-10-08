// Downloadable content (DLC): installing the player's own content packages.
// See dlc_install.cpp.

#pragma once

namespace rex {
class Runtime;
}

namespace rr6 {

// True when the program was started with --rr6_install_content=...: it is
// then meant to install content packages and leave without starting the game.
bool ContentInstallRequested();

// Installs every package named by --rr6_install_content and writes what
// happened to "dlc-install-result.txt" in the user data folder. Needs the
// runtime with the game's executable loaded (the title ID comes from it).
void InstallRequestedContent(rex::Runtime* runtime);

// Adds new and changed content files found in the DLC folder (next to the
// program's bin folder, or inside the game files folder; rr6_dlc_folder names
// another). Called at every start, before the game runs.
void AddContentFromDlcFolder(rex::Runtime* runtime);

// Writes "dlc-installed.txt" in the user data folder: one line for each
// installed content package (folder name, a tab, the name it shows). The
// launcher reads it. Also logs the list.
void WriteInstalledContentList(rex::Runtime* runtime);

}  // namespace rr6
