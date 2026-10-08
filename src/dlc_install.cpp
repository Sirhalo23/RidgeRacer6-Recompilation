// Downloadable content (DLC): installing the player's own content packages.
//
// On the console, downloaded content sits on the hard drive as package files
// (long names without an extension, in Content\0000000000000000\4E4D07D3\
// 00000002). The game asks the system for the list of installed content when
// it starts, and opens each one like a small disc.
//
// The SDK serves content that has been unpacked into its own folder layout
// under the user data folder:
//
//   <user data>/0000000000000000/4E4D07D3/00000002/<package name>/...
//   <user data>/0000000000000000/4E4D07D3/Headers/00000002/<package name>.header
//
// and this file puts packages there:
//
//   rr6_recomp --rr6_install_content="<file or folder>|<file or folder>|..."
//              [--rr6_install_result=<file>]
//
// loads the game's executable (the title ID comes from it), installs every
// package named, writes what happened to the result file (by default
// dlc-install-result.txt in the user data folder), and leaves without starting
// the game. The launcher and the Linux start script use it.
//
// Only packages that are downloadable content for this game are accepted:
// the package header must carry title ID 4E4D07D3 and the content type
// "marketplace content". A file named directly that is anything else (a
// save, a title update, another game's content) is refused with a reason. A
// folder is searched together with the folders inside it, and whatever in it
// is not content for this game is passed over without comment. The packages
// are the player's own files; none are part of this project.
//
// The SDK has an installer of its own (ContentManager::InstallContent), and
// the first version of this file used it. It trusts the inside of a package:
// a file named "..\..\x" would be written outside the content folder, a broken
// folder link reads past the end of a list, and a failed or short write still
// counts as installed. Packages are files people pick themselves, possibly
// from somewhere other than their own console, so this file now reads the
// package itself (the same layout rules as the SDK's reader in
// stfs_container_device.cpp), checks every name and link before anything is
// written, unpacks into a staging folder with every read and write checked,
// and only then moves the result into place. An installed copy that is being
// replaced is kept until the new one is complete.

#include "dlc_install.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/devices/stfs_xbox.h>
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
REXCVAR_DEFINE_STRING(rr6_install_result, "", "RR6",
                      "With rr6_install_content: the file to write the result to. Default: "
                      "dlc-install-result.txt in the user data folder.");

namespace rr6 {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kTitleId = 0x4E4D07D3;  // Ridge Racer 6 (USA)
constexpr const char* kTitleFolder = "4E4D07D3";
constexpr const char* kContentTypeFolder = "00000002";  // marketplace content
constexpr size_t kMaxFileNameLength = 42;               // XCONTENT_DATA::file_name_raw

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

std::string Lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
  return text;
}

// A name that is safe as one folder or file name on Windows and Linux: no
// separators, no "." or "..", nothing Windows would change or treat as a
// device. Content packages use plain names, so anything else is refused.
bool NameIsSafe(const std::string& name) {
  if (name.empty() || name == "." || name == "..") {
    return false;
  }
  for (unsigned char c : name) {
    if (c < 0x20 || c >= 0x7F || std::strchr("\\/:*?\"<>|", c)) {
      return false;
    }
  }
  if (name.back() == '.' || name.back() == ' ') {
    return false;
  }
  const std::string base = Lower(name.substr(0, name.find('.')));
  static const char* const kDevices[] = {"con", "prn", "aux", "nul"};
  for (const char* device : kDevices) {
    if (base == device) {
      return false;
    }
  }
  if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) &&
      base[3] >= '0' && base[3] <= '9') {
    return false;
  }
  return true;
}

std::string Printable(const std::string& name) {
  std::string out;
  for (unsigned char c : name) {
    out += (c >= 0x20 && c < 0x7F && c != '\t') ? char(c) : '?';
  }
  return out;
}

// ---- reading a content package (STFS) ----------------------------------------
//
// Block addressing and hash-table lookups follow the SDK's reader
// (StfsContainerDevice::BlockToOffsetSTFS, BlockToHashBlockNumberSTFS and
// GetBlockHash), with every read checked against the end of the file.

constexpr uint32_t kBlockSize = 0x1000;
constexpr uint32_t kBlocksPerHashLevel[3] = {170, 28900, 4913000};
constexpr uint32_t kEndOfChain = 0xFFFFFF;
constexpr size_t kMaxItems = 20000;       // real packages hold a handful of files
constexpr size_t kMaxPathLength = 200;    // inside the package
constexpr uint32_t kMaxRun = 4u << 20;    // longest single read while unpacking

struct Run {
  uint64_t offset;
  uint32_t size;
};

struct Item {
  std::string path;  // with '/' between folders
  bool directory = false;
  uint64_t length = 0;
  std::vector<Run> runs;  // where the file's bytes are, in order
};

class Package {
 public:
  Package(FILE* file, uint64_t file_size, const rex::filesystem::StfsHeader& header)
      : file_(file), file_size_(file_size), header_(header) {
    const auto& descriptor = header_.metadata.volume_descriptor.stfs;
    blocks_per_hash_table_ = descriptor.flags.bits.read_only_format ? 1 : 2;
    block_step_[0] = kBlocksPerHashLevel[0] + blocks_per_hash_table_;
    block_step_[1] = kBlocksPerHashLevel[1] + ((kBlocksPerHashLevel[0] + 1) * blocks_per_hash_table_);
    const uint64_t header_size = uint32_t(header_.header.header_size);
    data_start_ = (header_size + kBlockSize - 1) / kBlockSize * kBlockSize;
  }

  // Lists the package's folders and files. Returns an empty string, or the
  // reason the package is refused.
  std::string List(std::vector<Item>* items) {
    const auto& descriptor = header_.metadata.volume_descriptor.stfs;
    if (uint32_t(header_.metadata.data_file_count) > 1 || !descriptor.is_valid()) {
      return "not a content package this game can use";
    }
    const uint32_t total_blocks = descriptor.total_block_count;
    uint32_t table_block = descriptor.file_table_block_number();
    std::set<std::pair<uint32_t, std::string>> names;
    const std::string damaged = "damaged: ";

    for (uint32_t n = 0; n < descriptor.file_table_block_count; ++n) {
      if (table_block >= total_blocks) {
        return damaged + "its list of files is missing";
      }
      rex::filesystem::StfsDirectoryBlock directory;
      if (!ReadAt(BlockToOffset(table_block), &directory, sizeof(directory))) {
        return damaged + "its list of files cannot be read";
      }
      for (const auto& entry : directory.entries) {
        if (entry.name[0] == 0) {
          break;
        }
        if (items->size() >= kMaxItems) {
          return "it holds far more files than any content package";
        }
        const std::string name(entry.name, entry.flags.name_length & 0x3F);
        if (!NameIsSafe(name)) {
          return "it holds a file with a name that is not allowed (\"" + Printable(name) + "\")";
        }
        const uint32_t parent = entry.directory_index;
        std::string path = name;
        if (parent != 0xFFFF) {
          // Folders come before what is in them.
          if (parent >= items->size() || !(*items)[parent].directory) {
            return damaged + "a file is listed in a folder that does not exist";
          }
          path = (*items)[parent].path + "/" + name;
        }
        if (!names.insert({parent, Lower(name)}).second) {
          return "it lists the name \"" + Printable(name) + "\" twice in one folder";
        }
        if (path.size() > kMaxPathLength) {
          return "it holds a file with an unusually long path";
        }

        Item item;
        item.path = path;
        item.directory = entry.flags.directory;
        if (!item.directory) {
          item.length = uint32_t(entry.length);
          uint64_t remaining = item.length;
          uint32_t block = entry.start_block_number();
          while (remaining > 0) {
            if (block == kEndOfChain || block >= total_blocks) {
              return damaged + "the file " + Printable(path) + " is incomplete";
            }
            const uint32_t size = uint32_t(std::min<uint64_t>(kBlockSize, remaining));
            const uint64_t offset = BlockToOffset(block);
            if (offset + size > file_size_) {
              return damaged + "the file " + Printable(path) + " is cut short";
            }
            if (!item.runs.empty() && item.runs.back().offset + item.runs.back().size == offset &&
                item.runs.back().size + size <= kMaxRun) {
              item.runs.back().size += size;
            } else {
              item.runs.push_back({offset, size});
            }
            remaining -= size;
            const auto* hash = BlockHash(block);
            if (!hash) {
              return damaged + "its block tables cannot be read";
            }
            block = hash->level0_next_block();
          }
        }
        items->push_back(std::move(item));
      }

      const auto* hash = BlockHash(table_block);
      if (!hash) {
        return damaged + "its block tables cannot be read";
      }
      table_block = hash->level0_next_block();
      if (table_block == kEndOfChain) {
        break;
      }
    }
    return std::string();
  }

  bool ReadAt(uint64_t offset, void* out, size_t size) {
    if (offset > file_size_ || size > file_size_ - offset) {
      return false;
    }
    if (!rex::filesystem::Seek(file_, int64_t(offset), SEEK_SET)) {
      return false;
    }
    return std::fread(out, 1, size, file_) == size;
  }

 private:
  uint64_t BlockToOffset(uint64_t block_index) const {
    uint64_t base = kBlocksPerHashLevel[0];
    uint64_t block = block_index;
    for (uint32_t i = 0; i < 3; i++) {
      block += ((block_index + base) / base) * blocks_per_hash_table_;
      if (block_index < base) {
        break;
      }
      base *= kBlocksPerHashLevel[0];
    }
    return data_start_ + (block << 12);
  }

  uint32_t HashBlockNumber(uint32_t block_index, uint32_t hash_level) const {
    uint32_t block = 0;
    if (hash_level == 0) {
      if (block_index < kBlocksPerHashLevel[0]) {
        return 0;
      }
      block = (block_index / kBlocksPerHashLevel[0]) * block_step_[0];
      block += ((block_index / kBlocksPerHashLevel[1]) + 1) * blocks_per_hash_table_;
      if (block_index < kBlocksPerHashLevel[1]) {
        return block;
      }
      return block + blocks_per_hash_table_;
    }
    if (hash_level == 1) {
      if (block_index < kBlocksPerHashLevel[1]) {
        return block_step_[0];
      }
      block = (block_index / kBlocksPerHashLevel[1]) * block_step_[1];
      return block + blocks_per_hash_table_;
    }
    return block_step_[1];
  }

  uint64_t HashBlockOffset(uint32_t block_index, uint32_t hash_level) const {
    return data_start_ + (uint64_t(HashBlockNumber(block_index, hash_level)) << 12);
  }

  const rex::filesystem::StfsHashTable* Table(uint64_t offset, uint64_t read_from) {
    auto found = cache_.find(offset);
    if (found != cache_.end()) {
      return &found->second;
    }
    rex::filesystem::StfsHashTable table;
    if (!ReadAt(read_from, &table, sizeof(table))) {
      return nullptr;
    }
    return &cache_.emplace(offset, table).first->second;
  }

  const rex::filesystem::StfsHashEntry* BlockHash(uint32_t block_index) {
    const auto& descriptor = header_.metadata.volume_descriptor.stfs;
    uint32_t secondary = descriptor.flags.bits.root_active_index ? kBlockSize : 0;
    const uint64_t offset_lv0 = HashBlockOffset(block_index, 0);
    if (!cache_.count(offset_lv0)) {
      if (descriptor.flags.bits.read_only_format) {
        secondary = 0;
      } else if (uint32_t(descriptor.total_block_count) > kBlocksPerHashLevel[0]) {
        const uint64_t offset_lv1 = HashBlockOffset(block_index, 1);
        if (!cache_.count(offset_lv1)) {
          if (uint32_t(descriptor.total_block_count) > kBlocksPerHashLevel[1]) {
            const uint64_t offset_lv2 = HashBlockOffset(block_index, 2);
            const auto* lv2 = Table(offset_lv2, offset_lv2 + secondary);
            if (!lv2) {
              return nullptr;
            }
            const auto record = (block_index / kBlocksPerHashLevel[1]) % kBlocksPerHashLevel[0];
            secondary = lv2->entries[record].levelN_active_index() ? kBlockSize : 0;
          }
          if (!Table(offset_lv1, offset_lv1 + secondary)) {
            return nullptr;
          }
        }
        const auto record = (block_index / kBlocksPerHashLevel[0]) % kBlocksPerHashLevel[0];
        secondary = cache_[offset_lv1].entries[record].levelN_active_index() ? kBlockSize : 0;
      }
    }
    const auto* lv0 = Table(offset_lv0, offset_lv0 + secondary);
    if (!lv0) {
      return nullptr;
    }
    return &lv0->entries[block_index % kBlocksPerHashLevel[0]];
  }

  FILE* file_;
  uint64_t file_size_;
  const rex::filesystem::StfsHeader& header_;
  uint64_t data_start_ = 0;
  uint32_t blocks_per_hash_table_ = 1;
  uint32_t block_step_[2] = {0, 0};
  std::map<uint64_t, rex::filesystem::StfsHashTable> cache_;
};

struct UnpackResult {
  std::string problem;            // empty when everything was written
  bool package_at_fault = false;  // the package is broken, rather than the disk
};

UnpackResult Damaged(std::string problem) { return {std::move(problem), true}; }
UnpackResult WriteFailed(std::string problem) { return {std::move(problem), false}; }

// Writes the package's files under `to`.
UnpackResult Unpack(Package& package, const std::vector<Item>& items, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to, ec);
  if (ec) {
    return WriteFailed("could not be unpacked: a folder could not be created (" + ec.message() + ")");
  }
  const fs::path root = to.lexically_normal();
  std::vector<uint8_t> buffer(kMaxRun);
  for (const Item& item : items) {
    const fs::path dest = (to / rex::to_path(item.path)).lexically_normal();
    // The names were checked one by one, so this cannot leave `to`. Checked
    // again here because getting it wrong would write outside the folder.
    const fs::path relative = dest.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") {
      return Damaged("it holds a file with a path that is not allowed");
    }
    if (item.directory) {
      fs::create_directories(dest, ec);
      if (ec) {
        return WriteFailed("could not be unpacked: a folder could not be created (" + ec.message() + ")");
      }
      continue;
    }
    fs::create_directories(dest.parent_path(), ec);
    FILE* out = rex::filesystem::OpenFile(dest, "wb");
    if (!out) {
      return WriteFailed("could not be unpacked: " + Printable(item.path) + " could not be created");
    }
    bool written = true;
    for (const Run& run : item.runs) {
      if (!package.ReadAt(run.offset, buffer.data(), run.size)) {
        std::fclose(out);
        return Damaged("damaged: " + Printable(item.path) + " could not be read");
      }
      if (std::fwrite(buffer.data(), 1, run.size, out) != run.size) {
        written = false;
        break;
      }
    }
    if (std::fflush(out) != 0) {
      written = false;
    }
    if (std::fclose(out) != 0) {
      written = false;
    }
    if (!written) {
      return WriteFailed("could not be unpacked: writing " + Printable(item.path) +
                         " failed (is the disk full?)");
    }
  }
  return {};
}

bool ReadWholeFile(const fs::path& path, std::string* out) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    return false;
  }
  FILE* in = rex::filesystem::OpenFile(path, "rb");
  if (!in) {
    return false;
  }
  out->clear();
  char buffer[4096];
  size_t n;
  while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0) {
    out->append(buffer, n);
  }
  const bool ok = !std::ferror(in);
  std::fclose(in);
  return ok;
}

bool WriteWholeFile(const fs::path& path, const std::string& data) {
  FILE* out = rex::filesystem::OpenFile(path, "wb");
  if (!out) {
    return false;
  }
  bool ok = std::fwrite(data.data(), 1, data.size(), out) == data.size();
  ok = std::fflush(out) == 0 && ok;
  ok = std::fclose(out) == 0 && ok;
  return ok;
}

struct Counts {
  int installed = 0;
  int refused = 0;
  int failed = 0;
};

struct Installer {
  rex::system::xam::ContentManager* manager = nullptr;
  fs::path user_data;
  std::set<std::string> added;  // package file names added in this run
  Counts counts;
};

// One package file. Returns the line for the result file, or an empty string
// for a file in a searched folder that is not content for this game.
std::string InstallOneUnguarded(Installer& installer, const fs::path& file, bool from_folder_scan) {
  Counts& counts = installer.counts;
  const std::string file_name = rex::path_to_utf8(file.filename());
  const std::string shown = OneLine(file_name);
  auto refuse = [&](const std::string& why) {
    if (from_folder_scan) {
      // A folder may hold all sorts of things (a whole console drive, say):
      // only what is content for this game is reported.
      return std::string();
    }
    ++counts.refused;
    REXLOG_INFO("[dlc] refused {}: {}", shown, why);
    return "refused\t" + OneLine(why) + "\t" + shown;
  };
  auto fail = [&](const std::string& why) {
    ++counts.failed;
    REXLOG_ERROR("[dlc] {}: {}", shown, why);
    return "failed\t" + OneLine(why) + "\t" + shown;
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

  // From here on the file is meant to be content for this game, so problems
  // are reported even in a folder search.
  auto refuse_content = [&](const std::string& why) {
    ++counts.refused;
    REXLOG_INFO("[dlc] refused {}: {}", shown, why);
    return "refused\t" + OneLine(why) + "\t" + shown;
  };
  if (file_name.size() > kMaxFileNameLength || !NameIsSafe(file_name)) {
    return refuse_content(
        "its file name is not one a content package can have (keep the name it had on the console)");
  }
  if (!installer.added.insert(Lower(file_name)).second) {
    return refuse_content("a file with the same name was already added from another folder");
  }

  std::error_code ec;
  const uint64_t file_size = fs::file_size(file, ec);
  if (ec) {
    return fail("could not be read");
  }
  FILE* in = rex::filesystem::OpenFile(file, "rb");
  if (!in) {
    return fail("could not be opened");
  }
  struct Closer {
    FILE* f;
    ~Closer() { std::fclose(f); }
  } closer{in};

  Package package(in, file_size, *header);
  std::vector<Item> items;
  const std::string problem = package.List(&items);
  if (!problem.empty()) {
    return refuse_content(problem);
  }

  // Unpack next to the content folder (same drive, so the move is a rename),
  // but outside it: the SDK lists every folder in the content folder.
  const fs::path staging_root = installer.user_data / "dlc-staging";
  const fs::path staging = staging_root / rex::to_path(file_name);
  const fs::path previous = staging_root / rex::to_path(file_name + ".previous");
  fs::remove_all(staging, ec);
  fs::remove_all(previous, ec);
  const UnpackResult unpacked = Unpack(package, items, staging);
  if (!unpacked.problem.empty()) {
    fs::remove_all(staging, ec);
    return unpacked.package_at_fault ? refuse_content(unpacked.problem) : fail(unpacked.problem);
  }

  // Where the SDK looks (ContentManager::ResolvePackagePath and
  // ResolvePackageHeaderPath, for marketplace content of this title).
  const fs::path title_root = installer.user_data / "0000000000000000" / kTitleFolder;
  const fs::path content_dir = title_root / kContentTypeFolder;
  const fs::path installed = content_dir / rex::to_path(file_name);
  const fs::path header_file =
      title_root / "Headers" / kContentTypeFolder / rex::to_path(file_name + ".header");

  fs::create_directories(content_dir, ec);
  const bool replacing = fs::exists(installed, ec);
  std::string old_header;
  const bool had_header = ReadWholeFile(header_file, &old_header);
  if (replacing) {
    fs::rename(installed, previous, ec);
    if (ec) {
      fs::remove_all(staging, ec);
      return fail("the copy added earlier could not be replaced (is the game running?)");
    }
  }
  auto restore = [&]() {
    std::error_code ignored;
    fs::remove_all(installed, ignored);
    if (replacing) {
      fs::rename(previous, installed, ignored);
    }
    if (had_header) {
      WriteWholeFile(header_file, old_header);
    } else {
      fs::remove(header_file, ignored);
    }
  };
  fs::rename(staging, installed, ec);
  if (ec) {
    fs::remove_all(staging, ec);
    restore();
    return fail("could not be moved into the content folder");
  }

  rex::system::xam::XCONTENT_AGGREGATE_DATA data{};  // padding written as zeros
  data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
  data.content_type = rex::system::XContentType::kMarketplaceContent;
  data.title_id = kTitleId;
  data.xuid = 0;
  data.set_file_name(file_name);
  const std::u16string display = header->metadata.display_name(rex::system::XLanguage::kEnglish);
  const std::string name = TrimmedDisplayName(display);
  data.set_display_name(name.empty() ? rex::path_to_utf16(file.filename()) : display);
  uint32_t license_mask = 0;
  for (const auto& license : header->header.licenses) {
    if (uint32_t(license.license_flags)) {
      license_mask |= uint32_t(license.license_bits);
    }
  }
  const auto result = installer.manager->WriteContentHeaderFile(0, data, license_mask);
  const uint64_t expected = sizeof(data) + (license_mask ? sizeof(license_mask) : 0);
  if (result != 0 || fs::file_size(header_file, ec) != expected || ec) {
    restore();
    return fail("its description file could not be written (is the disk full?)");
  }
  fs::remove_all(previous, ec);

  ++counts.installed;
  REXLOG_INFO("[dlc] installed {} ({}), {} file(s)", shown, name, items.size());
  return "installed\t" + (name.empty() ? shown : name) + "\t" + shown;
}

// The same, with anything unexpected (a filesystem error thrown from deep
// inside the standard library, say) reported for this package alone instead
// of ending the whole run.
std::string InstallOne(Installer& installer, const fs::path& file, bool from_folder_scan) {
  try {
    return InstallOneUnguarded(installer, file, from_folder_scan);
  } catch (const std::exception& e) {
    ++installer.counts.failed;
    REXLOG_ERROR("[dlc] {}: {}", rex::path_to_utf8(file), e.what());
    std::error_code ec;
    fs::remove_all(installer.user_data / "dlc-staging" / file.filename(), ec);
    return "failed\t" + OneLine(std::string("unexpected error: ") + e.what()) + "\t" +
           OneLine(rex::path_to_utf8(file.filename()));
  }
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

// The files in a folder and the folders inside it, so that pointing at
// "Content" or at a copy of 4E4D07D3 works as well as pointing at 00000002.
// Not without limits: somebody may pick a whole drive. What could not be
// searched is reported rather than passed over.
struct FolderSearch {
  std::vector<fs::path> files;
  bool too_many = false;
  bool too_deep = false;
  int unreadable = 0;
};

FolderSearch SearchFolder(const fs::path& top) {
  constexpr int kMaxDepth = 6;
  constexpr size_t kMaxEntries = 50000;
  FolderSearch search;
  size_t seen = 0;
  std::vector<std::pair<fs::path, int>> folders = {{top, 0}};
  while (!folders.empty() && !search.too_many) {
    auto [folder, depth] = folders.back();
    folders.pop_back();
    std::error_code ec;
    fs::directory_iterator it(folder, ec);
    if (ec) {
      ++search.unreadable;
      continue;
    }
    std::vector<fs::path> here;
    for (; it != fs::directory_iterator(); it.increment(ec)) {
      if (ec) {
        break;
      }
      if (++seen > kMaxEntries) {
        search.too_many = true;
        break;
      }
      std::error_code entry_ec;
      const bool link = it->is_symlink(entry_ec);
      if (it->is_directory(entry_ec)) {
        if (link) {
          continue;  // not followed: it may lead anywhere, even back up
        }
        if (depth + 1 > kMaxDepth) {
          search.too_deep = true;
          continue;
        }
        folders.push_back({it->path(), depth + 1});
      } else if (it->is_regular_file(entry_ec)) {
        here.push_back(it->path());
      }
    }
    if (ec) {
      ++search.unreadable;
    }
    std::sort(here.begin(), here.end());
    search.files.insert(search.files.end(), here.begin(), here.end());
  }
  return search;
}

// Writes the file under another name first, so that a reader never sees half
// a file, and keeps the previous file if anything fails.
bool WriteLines(const fs::path& file, const std::vector<std::string>& lines) {
  fs::path partial = file;
  partial += ".part";
  std::string text;
  for (const auto& line : lines) {
    text += line;
    text += "\n";
  }
  std::error_code ec;
  if (!WriteWholeFile(partial, text)) {
    fs::remove(partial, ec);
    REXLOG_ERROR("[dlc] could not write {}", rex::path_to_utf8(file));
    return false;
  }
  fs::rename(partial, file, ec);  // replaces the old file, also on Windows
  if (ec) {
    fs::remove(partial, ec);
    REXLOG_ERROR("[dlc] could not replace {}", rex::path_to_utf8(file));
    return false;
  }
  return true;
}

}  // namespace

bool ContentInstallRequested() {
  return !REXCVAR_GET(rr6_install_content).empty();
}

void InstallRequestedContent(rex::Runtime* runtime) {
  std::vector<std::string> lines;
  lines.push_back("RR6-DLC-INSTALL 1");
  Installer installer;
  Counts& counts = installer.counts;

  auto* kernel_state = runtime ? runtime->kernel_state() : nullptr;
  installer.manager = kernel_state ? kernel_state->content_manager() : nullptr;
  if (runtime) {
    installer.user_data = runtime->user_data_root();
  }
  fs::path result_file = rex::to_path(REXCVAR_GET(rr6_install_result));
  if (result_file.empty() && !installer.user_data.empty()) {
    result_file = installer.user_data / "dlc-install-result.txt";
  }

  if (!installer.manager || installer.user_data.empty()) {
    REXLOG_ERROR("[dlc] the content manager is not available; nothing installed");
    lines.push_back("failed\tthe game's content support did not start\t-");
    ++counts.failed;
  } else if (kernel_state->title_id() != kTitleId) {
    lines.push_back("failed\tthe game's executable is not the expected one\t-");
    ++counts.failed;
  } else {
    for (const auto& item : SplitList(REXCVAR_GET(rr6_install_content))) {
      const fs::path path = rex::to_path(item);
      const std::string shown = OneLine(rex::path_to_utf8(path.filename()));
      std::error_code ec;
      if (fs::is_directory(path, ec)) {
        const FolderSearch search = SearchFolder(path);
        int found = 0;
        for (const fs::path& file : search.files) {
          std::string line = InstallOne(installer, file, true);
          if (!line.empty()) {
            lines.push_back(std::move(line));
            ++found;
          }
        }
        std::string unfinished;
        if (search.too_many) {
          unfinished = "the search stopped after 50,000 files and folders";
        } else if (search.unreadable > 0) {
          unfinished = std::to_string(search.unreadable) + " folder(s) inside it could not be opened";
        } else if (search.too_deep) {
          unfinished = "folders more than 6 levels down were not searched";
        }
        if (!unfinished.empty()) {
          ++counts.failed;
          lines.push_back("failed\tnot all of this folder was searched: " + unfinished +
                          "; choose a folder closer to the content\t" + shown);
        } else if (found == 0) {
          ++counts.refused;
          lines.push_back(
              "refused\tno Ridge Racer 6 content in this folder or the folders inside it\t" +
              shown);
        }
      } else if (fs::is_regular_file(path, ec)) {
        lines.push_back(InstallOne(installer, path, false));
      } else {
        ++counts.failed;
        lines.push_back("failed\tfile not found\t" + shown);
      }
    }
    std::error_code ec;
    fs::remove_all(installer.user_data / "dlc-staging", ec);
  }

  char done[96];
  std::snprintf(done, sizeof(done), "done\t%d\t%d\t%d", counts.installed, counts.refused,
                counts.failed);
  lines.push_back(done);
  if (!result_file.empty()) {
    WriteLines(result_file, lines);
  }
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
  std::vector<rex::system::xam::XCONTENT_AGGREGATE_DATA> installed;
  try {
    installed = manager->ListContent(static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD),
                                     0, rex::system::XContentType::kMarketplaceContent, kTitleId);
  } catch (const std::exception& e) {
    // Only for the launcher's list; the game itself asks again later.
    REXLOG_ERROR("[dlc] the installed content could not be listed: {}", e.what());
    return;
  }
  std::vector<std::string> lines;
  for (const auto& item : installed) {
    const std::string folder = OneLine(item.file_name());
    std::string name = TrimmedDisplayName(item.display_name());
    lines.push_back(folder + "\t" + (name.empty() ? folder : name));
  }
  std::error_code ec;
  fs::create_directories(runtime->user_data_root(), ec);
  WriteLines(runtime->user_data_root() / "dlc-installed.txt", lines);
  REXLOG_INFO("[dlc] {} content package(s) installed", installed.size());
  for (const auto& line : lines) {
    REXLOG_INFO("[dlc]   {}", line);
  }
}

}  // namespace rr6
