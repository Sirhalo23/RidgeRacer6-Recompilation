// Downloadable content (DLC): installing the player's own content packages.
//
// On the console, downloaded content sits on the hard drive as package files
// (long names without an extension, in Content\0000000000000000\4E4D07D3\
// 00000002). The game asks the system for the list of installed content when
// it starts, and opens each one like a small disc.
//
// The SDK does all of that already, for content that has been unpacked into
// its own folder layout under the user data folder:
//
//   <user data>/0000000000000000/4E4D07D3/00000002/<package name>/...
//   <user data>/0000000000000000/4E4D07D3/Headers/00000002/<package name>.header
//
// and it has a routine that unpacks a package into that layout
// (ContentManager::InstallContent). Nothing calls it. This file does:
//
//   rr6_recomp --rr6_install_content="<file or folder>|<file or folder>|..."
//
// loads the game's executable (the title ID comes from it), installs every
// package named, writes what happened to dlc-install-result.txt in the user
// data folder, and leaves without starting the game. The launcher and the
// Linux start script use it.
//
// Only packages that are downloadable content for this game are accepted:
// the package header must carry title ID 4E4D07D3 and the content type
// "marketplace content". A file named directly that is anything else (a
// save, a title update, another game's content) is refused with a reason. A
// folder is searched together with the folders inside it, and whatever in it
// is not content for this game is passed over without comment. The packages are the player's own files; none are
// part of this project.

#include "dlc_install.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/string/utf8.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

REXCVAR_DEFINE_STRING(rr6_install_content, "", "RR6",
                      "Install downloadable content and exit: one or more content package files, "
                      "or folders of them, separated by |. The packages are your own files from "
                      "an Xbox 360 hard drive.");

namespace rr6 {
namespace {

constexpr uint32_t kTitleId = 0x4E4D07D3;  // Ridge Racer 6 (USA)

// Tabs and line breaks would break the one-line-per-package result files.
std::string OneLine(std::string text) {
  for (char& c : text) {
    if (c == '\t' || c == '\r' || c == '\n') {
      c = ' ';
    }
  }
  return text;
}

std::string TrimmedDisplayName(const std::u16string& name) {
  std::u16string cut = name;
  const auto end = cut.find(u'\0');
  if (end != std::u16string::npos) {
    cut.resize(end);
  }
  return OneLine(rex::string::to_utf8(cut));
}

struct Counts {
  int installed = 0;
  int refused = 0;
  int failed = 0;
};

// One package file. Returns the line for the result file.
std::string InstallOne(rex::system::xam::ContentManager* manager, const std::filesystem::path& file,
                       bool from_folder_scan, Counts& counts) {
  const std::string shown = OneLine(rex::path_to_utf8(file.filename()));
  auto refuse = [&](const std::string& why) {
    if (from_folder_scan) {
      // A folder may hold all sorts of things (a whole console drive, say):
      // only what is content for this game is reported.
      return std::string();
    }
    ++counts.refused;
    REXLOG_INFO("[dlc] refused {}: {}", shown, why);
    return "refused\t" + why + "\t" + shown;
  };

  auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(file);
  if (!header) {
    return refuse("not an Xbox 360 content package");
  }
  const uint32_t title_id = header->metadata.execution_info.title_id;
  if (title_id != kTitleId) {
    char text[64];
    std::snprintf(text, sizeof(text), "for another game (title ID %08X)", title_id);
    return refuse(text);
  }
  const rex::system::XContentType type = header->metadata.content_type;
  if (type != rex::system::XContentType::kMarketplaceContent) {
    if (type == rex::system::XContentType::kSavedGame) {
      return refuse("a saved game, not downloadable content");
    }
    char text[80];
    std::snprintf(text, sizeof(text), "not downloadable content (content type %08X)",
                  uint32_t(type));
    return refuse(text);
  }
  const rex::filesystem::XContentVolumeType volume = header->metadata.volume_type;
  if (volume != rex::filesystem::XContentVolumeType::kStfs) {
    return refuse("a disc-style package, not downloadable content");
  }

  const std::string name =
      TrimmedDisplayName(header->metadata.display_name(rex::system::XLanguage::kEnglish));
  const auto result = manager->InstallContent(file);
  if (result != 0) {  // X_ERROR_SUCCESS
    ++counts.failed;
    char text[64];
    std::snprintf(text, sizeof(text), "could not be unpacked (error %08X)", uint32_t(result));
    REXLOG_ERROR("[dlc] {}: {}", shown, text);
    return std::string("failed\t") + text + "\t" + shown;
  }
  ++counts.installed;
  REXLOG_INFO("[dlc] installed {} ({})", shown, name);
  return "installed\t" + (name.empty() ? shown : name) + "\t" + shown;
}

std::vector<std::string> SplitList(const std::string& list) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= list.size()) {
    size_t end = list.find('|', start);
    if (end == std::string::npos) {
      end = list.size();
    }
    std::string part = list.substr(start, end - start);
    // A path may arrive wrapped in quotes or with spaces around it.
    while (!part.empty() && (part.front() == ' ' || part.front() == '"')) {
      part.erase(part.begin());
    }
    while (!part.empty() && (part.back() == ' ' || part.back() == '"')) {
      part.pop_back();
    }
    if (!part.empty()) {
      parts.push_back(part);
    }
    start = end + 1;
  }
  return parts;
}

void WriteLines(const std::filesystem::path& file, const std::vector<std::string>& lines) {
  // Written under another name first, so that a reader never sees half a file.
  std::filesystem::path partial = file;
  partial += ".part";
  {
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    for (const auto& line : lines) {
      out << line << "\n";
    }
  }
  std::error_code ec;
  std::filesystem::rename(partial, file, ec);
  if (ec) {
    std::filesystem::remove(file, ec);
    std::filesystem::rename(partial, file, ec);
  }
}

}  // namespace

bool ContentInstallRequested() {
  return !REXCVAR_GET(rr6_install_content).empty();
}

void InstallRequestedContent(rex::Runtime* runtime) {
  std::vector<std::string> lines;
  lines.push_back("RR6-DLC-INSTALL 1");
  Counts counts;

  auto* kernel_state = runtime ? runtime->kernel_state() : nullptr;
  auto* manager = kernel_state ? kernel_state->content_manager() : nullptr;
  if (!manager || runtime->user_data_root().empty()) {
    REXLOG_ERROR("[dlc] the content manager is not available; nothing installed");
    return;
  }
  const std::filesystem::path result_file = runtime->user_data_root() / "dlc-install-result.txt";

  if (kernel_state->title_id() != kTitleId) {
    // InstallContent files the content under the running title's ID.
    lines.push_back("failed\tthe game's executable is not the expected one\t-");
    ++counts.failed;
  } else {
    for (const auto& item : SplitList(REXCVAR_GET(rr6_install_content))) {
      const std::filesystem::path path = rex::to_path(item);
      std::error_code ec;
      if (std::filesystem::is_directory(path, ec)) {
        // The folder and the folders inside it, so that pointing at "Content"
        // or at a copy of 4E4D07D3 works as well as pointing at 00000002. Not
        // without limits: somebody may pick a whole drive.
        constexpr int kMaxDepth = 6;
        constexpr int kMaxFiles = 20000;
        int found = 0;
        int looked_at = 0;
        std::filesystem::recursive_directory_iterator it(
            path, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
          std::error_code entry_ec;
          if (it->is_directory(entry_ec)) {
            if (it.depth() >= kMaxDepth || it->is_symlink(entry_ec)) {
              it.disable_recursion_pending();
            }
            continue;
          }
          if (!it->is_regular_file(entry_ec)) {
            continue;
          }
          if (++looked_at > kMaxFiles) {
            break;
          }
          std::string line = InstallOne(manager, it->path(), true, counts);
          if (!line.empty()) {
            lines.push_back(std::move(line));
            ++found;
          }
        }
        if (found == 0) {
          ++counts.refused;
          lines.push_back(
              "refused\tno Ridge Racer 6 content in this folder or the folders inside it\t" +
              OneLine(rex::path_to_utf8(path.filename())));
        }
      } else if (std::filesystem::is_regular_file(path, ec)) {
        lines.push_back(InstallOne(manager, path, false, counts));
      } else {
        ++counts.failed;
        lines.push_back("failed\tfile not found\t" + OneLine(rex::path_to_utf8(path.filename())));
      }
    }
  }

  char done[96];
  std::snprintf(done, sizeof(done), "done\t%d\t%d\t%d", counts.installed, counts.refused,
                counts.failed);
  lines.push_back(done);
  WriteLines(result_file, lines);
  REXLOG_INFO("[dlc] finished: {} installed, {} refused, {} failed", counts.installed,
              counts.refused, counts.failed);
  WriteInstalledContentList(runtime);
}

void WriteInstalledContentList(rex::Runtime* runtime) {
  auto* kernel_state = runtime ? runtime->kernel_state() : nullptr;
  auto* manager = kernel_state ? kernel_state->content_manager() : nullptr;
  if (!manager || runtime->user_data_root().empty()) {
    return;
  }
  const auto installed = manager->ListContent(
      static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD), 0,
      rex::system::XContentType::kMarketplaceContent, kTitleId);
  std::vector<std::string> lines;
  for (const auto& item : installed) {
    const std::string folder = OneLine(item.file_name());
    std::string name = TrimmedDisplayName(item.display_name());
    lines.push_back(folder + "\t" + (name.empty() ? folder : name));
  }
  std::error_code ec;
  std::filesystem::create_directories(runtime->user_data_root(), ec);
  WriteLines(runtime->user_data_root() / "dlc-installed.txt", lines);
  REXLOG_INFO("[dlc] {} content package(s) installed", installed.size());
  for (const auto& line : lines) {
    REXLOG_INFO("[dlc]   {}", line);
  }
}

}  // namespace rr6
