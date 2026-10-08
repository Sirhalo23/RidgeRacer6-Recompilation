// Copies the game files out of an Xbox 360 disc image (.iso).
//
// The same job as tools\prepare-game.ps1 in the tester package, built into the
// launcher so that it can show progress and needs no PowerShell. Nothing here
// is specific to Windows apart from how files are opened.
//
// Layout of the image: the game partition (file system "XDVDFS") starts at a
// fixed offset that depends on the disc format, or at 0 in an image that holds
// only that partition. 0x10000 bytes into the partition is a header: the text
// MICROSOFT*XBOX*MEDIA, then the sector and the size of the root directory.
// A directory is a table of entries forming a binary tree: left and right
// child (16-bit offsets in units of 4 bytes), first sector, size, attributes,
// name length, name. Sectors are 2048 bytes, counted from the partition start.

#include "disc_image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <system_error>
#include <vector>

namespace disc {

const char* const kCopyingMarker = ".rr6-copying";
const char* const kReadyMarker = ".rr6-ready";

namespace {

namespace fs = std::filesystem;

const uint64_t kSector = 2048;
const uint64_t kPartitionOffsets[] = {0xFD90000ull, 0x2080000ull, 0x18300000ull, 0ull};
const char kMagic[] = "MICROSOFT*XBOX*MEDIA";  // 20 characters
const uint64_t kSpareSpace = 200ull * 1024 * 1024;
const size_t kBlock = 4u * 1024 * 1024;

// ---- files larger than 4 GB, with names in any language ---------------------

FILE* OpenFile(const fs::path& path, bool write) {
#ifdef _WIN32
  return _wfopen(path.c_str(), write ? L"wb" : L"rb");
#else
  return fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

bool SeekTo(FILE* f, uint64_t position) {
#ifdef _WIN32
  return _fseeki64(f, (long long)position, SEEK_SET) == 0;
#else
  return fseeko(f, (off_t)position, SEEK_SET) == 0;
#endif
}

bool ReadAt(FILE* f, uint64_t position, void* out, size_t size) {
  return SeekTo(f, position) && fread(out, 1, size, f) == size;
}

uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ---- SHA-256 (FIPS 180-4) ---------------------------------------------------

class Sha256 {
 public:
  void Update(const uint8_t* data, size_t size) {
    length_ += size;
    while (size > 0) {
      size_t take = std::min(size, sizeof(block_) - fill_);
      memcpy(block_ + fill_, data, take);
      fill_ += take;
      data += take;
      size -= take;
      if (fill_ == sizeof(block_)) {
        Compress();
        fill_ = 0;
      }
    }
  }

  std::string HexDigest() {
    uint64_t bits = length_ * 8;
    const uint8_t one = 0x80, zero = 0;
    Update(&one, 1);
    while (fill_ != 56) Update(&zero, 1);
    uint8_t size_bytes[8];
    for (int i = 0; i < 8; ++i) size_bytes[i] = (uint8_t)(bits >> (56 - 8 * i));
    Update(size_bytes, 8);
    static const char* digits = "0123456789ABCDEF";
    std::string hex;
    for (uint32_t word : state_) {
      for (int shift = 28; shift >= 0; shift -= 4) hex += digits[(word >> shift) & 15];
    }
    return hex;
  }

 private:
  static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void Compress() {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = ((uint32_t)block_[i * 4] << 24) | ((uint32_t)block_[i * 4 + 1] << 16) |
             ((uint32_t)block_[i * 4 + 2] << 8) | (uint32_t)block_[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block_[64];
  size_t fill_ = 0;
  uint64_t length_ = 0;
};

// ---- the directory tree -----------------------------------------------------

struct Entry {
  std::string path;  // with '/' between folders
  uint32_t sector;
  uint32_t size;
};

std::string Lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return text;
}

bool EndsWith(const std::string& text, const char* end) {
  const size_t n = strlen(end);
  return text.size() >= n && text.compare(text.size() - n, n, end) == 0;
}

// A name that can be written as it is on Windows and Linux, and that cannot
// be mistaken for one of the names this copy uses itself (a file's ".part"
// while it is being written, and the two markers).
bool SafeName(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  for (unsigned char c : name) {
    if (c < 0x20 || c >= 0x7F || strchr("\\/:*?\"<>|", c)) return false;
  }
  if (name.back() == '.' || name.back() == ' ') return false;
  const std::string lower = Lower(name);
  if (EndsWith(lower, ".part") || lower == kCopyingMarker || lower == kReadyMarker) return false;
  const std::string base = lower.substr(0, lower.find('.'));
  if (base == "con" || base == "prn" || base == "aux" || base == "nul") return false;
  if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) &&
      base[3] >= '0' && base[3] <= '9') {
    return false;
  }
  return true;
}

struct Image {
  FILE* file = nullptr;
  uint64_t base = 0;  // start of the game partition
  std::vector<Entry> files;
  std::vector<std::string> folders;
  bool unsafe_names = false;
  ~Image() {
    if (file) fclose(file);
  }
};

// Reads the whole directory tree. A broken tree is refused rather than read in
// part: a copy of only the files that could be reached would look complete.
Result ReadTree(Image* image, Progress* progress) {
  uint8_t header[28];
  bool found = false;
  for (uint64_t offset : kPartitionOffsets) {
    if (ReadAt(image->file, offset + 0x10000, header, sizeof(header)) && !memcmp(header, kMagic, 20)) {
      image->base = offset;
      found = true;
      break;
    }
    clearerr(image->file);
  }
  if (!found) return Result::kNotGameDisc;

  struct Folder {
    uint32_t sector, size;
    std::string path;
    int depth;
  };
  std::vector<Folder> pending = {{U32(header + 20), U32(header + 24), "", 0}};
  std::set<uint32_t> folder_sectors;  // each folder's table is read once
  std::set<std::string> paths;        // as Windows compares them: without regard to case
  int folders_read = 0;
  while (!pending.empty()) {
    if (progress->cancel) return Result::kCancelled;
    Folder folder = pending.back();
    pending.pop_back();
    if (folder.size == 0) continue;
    // Real discs have a handful of small tables; anything else is not a disc.
    if (folder.size > 16u * 1024 * 1024 || ++folders_read > 10000) return Result::kNotGameDisc;
    // A folder whose table was read already is a folder inside itself.
    if (!folder_sectors.insert(folder.sector).second || folder.depth > 32) return Result::kDamaged;
    std::vector<uint8_t> table(folder.size);
    if (!ReadAt(image->file, image->base + (uint64_t)folder.sector * kSector, table.data(), table.size())) {
      return Result::kReadError;
    }
    std::vector<size_t> nodes = {0};
    std::set<size_t> seen;
    while (!nodes.empty()) {
      size_t o = nodes.back();
      nodes.pop_back();
      // Every link has to lead to an entry of this table that was not reached
      // before; anything else means the list of files is broken.
      if (o + 14 > table.size() || !seen.insert(o).second) return Result::kDamaged;
      const uint8_t* e = table.data() + o;
      const uint16_t left = U16(e), right = U16(e + 2);
      const uint32_t sector = U32(e + 4), size = U32(e + 8);
      const uint8_t attributes = e[12], name_length = e[13];
      if (left == 0xFFFF && right == 0xFFFF && sector == 0xFFFFFFFFu) continue;  // unused space
      if (o + 14 + name_length > table.size()) return Result::kNotGameDisc;
      std::string name((const char*)e + 14, name_length);
      if (!SafeName(name)) image->unsafe_names = true;  // reported once the game is known to be the right one
      std::string path = folder.path.empty() ? name : folder.path + "/" + name;
      if (path.size() > 1000) return Result::kDamaged;
      // Two names that differ only in case would end up as one file on Windows.
      if (!paths.insert(Lower(path)).second) image->unsafe_names = true;
      if (attributes & 0x10) {
        image->folders.push_back(path);
        pending.push_back({sector, size, path, folder.depth + 1});
      } else {
        image->files.push_back({path, sector, size});
      }
      if (image->files.size() + image->folders.size() > 200000) return Result::kNotGameDisc;
      if (left != 0 && left != 0xFFFF) nodes.push_back((size_t)left * 4);
      if (right != 0 && right != 0xFFFF) nodes.push_back((size_t)right * 4);
    }
  }
  return Result::kOk;
}

bool SameName(const std::string& a, const char* b) {
  if (a.size() != strlen(b)) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

void WriteMarker(const fs::path& path) {
  if (FILE* f = OpenFile(path, true)) {
    fputs("ok\n", f);
    fclose(f);
  }
}

}  // namespace

Result Extract(const Options& options, Progress* progress, std::string* detail) {
  detail->clear();
  Image image;
  image.file = OpenFile(options.image, false);
  if (!image.file) return Result::kCannotOpen;
  Result tree = ReadTree(&image, progress);
  if (tree != Result::kOk) return tree;

  // In the order they lie on the disc.
  std::sort(image.files.begin(), image.files.end(),
            [](const Entry& a, const Entry& b) { return a.sector < b.sector; });
  const Entry* executable = nullptr;
  uint64_t total = 0;
  for (const Entry& e : image.files) {
    total += e.size;
    if (SameName(e.path, "default.xex")) executable = &e;
  }
  if (!executable) return Result::kNotGameDisc;
  progress->total = total;

  std::vector<uint8_t> block(kBlock);

  // The right game? Checked on the image itself, before anything is copied.
  if (options.expected_sha256) {
    Sha256 hash;
    uint64_t remaining = executable->size;
    if (!SeekTo(image.file, image.base + (uint64_t)executable->sector * kSector)) return Result::kReadError;
    while (remaining > 0) {
      if (progress->cancel) return Result::kCancelled;
      size_t want = (size_t)std::min<uint64_t>(block.size(), remaining);
      if (fread(block.data(), 1, want, image.file) != want) return Result::kReadError;
      hash.Update(block.data(), want);
      remaining -= want;
    }
    std::string found = hash.HexDigest();
    if (found != options.expected_sha256) {
      *detail = found;
      return Result::kWrongVersion;
    }
  }
  if (image.unsafe_names) return Result::kUnsafeName;
  if (progress->cancel) return Result::kCancelled;

  std::error_code ec;
  fs::create_directories(options.out, ec);
  if (!fs::is_directory(options.out, ec)) {
    *detail = options.out.u8string();
    return Result::kWriteError;
  }

  // Work out what is still to copy, then whether it fits.
  std::vector<bool> keep(image.files.size(), false);
  uint64_t to_copy = 0;
  for (size_t i = 0; i < image.files.size(); ++i) {
    const Entry& e = image.files[i];
    if (!options.overwrite && &e != executable) {
      const fs::path target = options.out / fs::u8path(e.path);
      const uintmax_t existing = fs::file_size(target, ec);
      if (!ec && existing == e.size) keep[i] = true;
    }
    if (!keep[i]) to_copy += e.size;
  }
  const fs::space_info space = fs::space(options.out, ec);
  if (!ec && space.available != (uintmax_t)-1 && space.available < to_copy + kSpareSpace) {
    *detail = std::to_string(to_copy + kSpareSpace) + " " + std::to_string(space.available);
    return Result::kNoSpace;
  }

  WriteMarker(options.out / kCopyingMarker);
  fs::remove(options.out / kReadyMarker, ec);
  for (const std::string& folder : image.folders) {
    fs::create_directories(options.out / fs::u8path(folder), ec);
  }

  uint64_t done = 0;
  for (size_t i = 0; i < image.files.size(); ++i) {
    const Entry& e = image.files[i];
    if (keep[i]) {
      done += e.size;
      progress->done = done;
      continue;
    }
    progress->SetFile(e.path);
    const fs::path target = options.out / fs::u8path(e.path);
    fs::path partial = target;
    partial += ".part";
    if (!SeekTo(image.file, image.base + (uint64_t)e.sector * kSector)) return Result::kReadError;
    FILE* out = OpenFile(partial, true);
    if (!out) {
      *detail = target.u8string();
      return Result::kWriteError;
    }
    uint64_t remaining = e.size;
    Result result = Result::kOk;
    while (remaining > 0 && result == Result::kOk) {
      if (progress->cancel) {
        result = Result::kCancelled;
        break;
      }
      size_t want = (size_t)std::min<uint64_t>(block.size(), remaining);
      if (fread(block.data(), 1, want, image.file) != want) {
        result = Result::kReadError;
      } else if (fwrite(block.data(), 1, want, out) != want) {
        result = Result::kWriteError;
      } else {
        remaining -= want;
        done += want;
        progress->done = done;
      }
    }
    if (fclose(out) != 0 && result == Result::kOk) result = Result::kWriteError;
    if (result == Result::kOk) {
      fs::remove(target, ec);
      fs::rename(partial, target, ec);
      if (ec) result = Result::kWriteError;
    }
    if (result != Result::kOk) {
      fs::remove(partial, ec);
      if (result == Result::kWriteError) *detail = target.u8string();
      return result;
    }
  }

  WriteMarker(options.out / kReadyMarker);
  fs::remove(options.out / kCopyingMarker, ec);
  progress->SetFile("");
  return Result::kOk;
}

}  // namespace disc
