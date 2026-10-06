// Copies the game files out of an Xbox 360 disc image (.iso). See disc_image.cpp.
#ifndef RR6_DISC_IMAGE_H
#define RR6_DISC_IMAGE_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace disc {

enum class Result {
  kOk,
  kCancelled,
  kCannotOpen,     // the image file could not be opened
  kNotGameDisc,    // no game partition, or no default.xex in it
  kUnsafeName,     // a file name in the image that must not be written to disk
  kWrongVersion,   // default.xex is not the one this build was made from; detail: its SHA-256
  kNoSpace,        // detail: "<needed bytes> <free bytes>"
  kReadError,      // the image ends early or cannot be read
  kWriteError,     // detail: the file that could not be written
};

// Shared between the copying thread and whoever shows its progress.
class Progress {
 public:
  std::atomic<uint64_t> done{0};    // bytes dealt with so far (copied or already there)
  std::atomic<uint64_t> total{0};   // bytes in all files; 0 until the image has been read
  std::atomic<bool> cancel{false};  // set to stop the copy at the next block

  void SetFile(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = name;
  }
  std::string File() {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_;
  }

 private:
  std::mutex mutex_;
  std::string file_;  // the file being copied
};

struct Options {
  std::filesystem::path image;            // the .iso
  std::filesystem::path out;              // folder for the game files (created if missing)
  const char* expected_sha256 = nullptr;  // of default.xex, upper-case hex; nullptr = accept any
  bool overwrite = false;                 // false: files already there with the right size are kept
};

// Markers in the output folder: the first exists while a copy is unfinished
// (also after a failed or cancelled one), the second once everything is there.
extern const char* const kCopyingMarker;  // ".rr6-copying"
extern const char* const kReadyMarker;    // ".rr6-ready"

// Blocks until done. `detail` receives extra information for some results.
Result Extract(const Options& options, Progress* progress, std::string* detail);

}  // namespace disc

#endif
