// rr6-extract: copies the game files out of a Ridge Racer 6 (USA) disc image.
//
// The command-line face of disc_image.cpp, for systems the launcher does not
// run on (Linux, Steam Deck). The start script of the Linux package calls it.
//
//   rr6-extract <disc image .iso> <folder for the game files> [--overwrite]
//
// Exit status: 0 = the game files are complete, 1 = they are not, 2 = wrong
// use, 130 = stopped with Ctrl+C (files copied so far are kept; running it
// again carries on).
//
// Build: g++ -std=c++17 -O2 -static -pthread rr6_extract.cpp disc_image.cpp -o rr6-extract
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "disc_image.h"

namespace {

// default.xex of the USA disc: the program this build was recompiled from.
const char kGameExecutableSha256[] = "39D3C0004EC62AEB0FE3E7E1889CF25D98FBD27987B6BC6B5FF30A56FFBA6C00";

disc::Progress g_progress;

void OnInterrupt(int) { g_progress.cancel = true; }

double Gigabytes(unsigned long long bytes) { return bytes / (1024.0 * 1024.0 * 1024.0); }

bool OnTerminal() {
#ifdef _WIN32
  return true;
#else
  return isatty(fileno(stdout)) != 0;
#endif
}

void ShowProgress(bool same_line) {
  const unsigned long long total = g_progress.total.load();
  if (total == 0) {
    return;
  }
  const unsigned long long done = g_progress.done.load();
  const std::string file = g_progress.File();
  if (same_line) {
    std::printf("\r  %5.1f%%  %.2f of %.2f GB  %-28.28s", 100.0 * done / total, Gigabytes(done),
                Gigabytes(total), file.c_str());
  } else {
    std::printf("  %5.1f%%  %.2f of %.2f GB  %s\n", 100.0 * done / total, Gigabytes(done),
                Gigabytes(total), file.c_str());
  }
  std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  std::string image, out;
  bool overwrite = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--overwrite") {
      overwrite = true;
    } else if (image.empty()) {
      image = arg;
    } else if (out.empty()) {
      out = arg;
    } else {
      image.clear();
      break;
    }
  }
  if (image.empty() || out.empty()) {
    std::fprintf(stderr,
                 "Copies the game files out of a Ridge Racer 6 (USA) disc image.\n\n"
                 "  rr6-extract <disc image .iso> <folder for the game files> [--overwrite]\n\n"
                 "Files already there with the right size are kept unless --overwrite is given.\n");
    return 2;
  }

  disc::Options options;
  options.image = image;
  options.out = out;
  options.expected_sha256 = kGameExecutableSha256;
  options.overwrite = overwrite;

  std::signal(SIGINT, OnInterrupt);
  std::signal(SIGTERM, OnInterrupt);

  std::printf("Checking the disc image and copying the game files (about 6 GB)...\n");
  std::fflush(stdout);

  disc::Result result = disc::Result::kOk;
  std::string detail;
  std::atomic<bool> finished{false};
  std::thread worker([&] {
    result = disc::Extract(options, &g_progress, &detail);
    finished = true;
  });
  const bool same_line = OnTerminal();
  int ticks = 0;
  while (!finished.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // On a terminal the line is redrawn five times a second; in a file or a
    // pipe there is a new line every five seconds.
    if (same_line || ++ticks % 25 == 0) {
      ShowProgress(same_line);
    }
  }
  worker.join();
  if (g_progress.total.load() != 0 && result == disc::Result::kOk) {
    ShowProgress(same_line);
  }
  if (same_line && g_progress.total.load() != 0) {
    std::printf("\n");
  }

  switch (result) {
    case disc::Result::kOk:
      std::printf("The game files are ready.\n");
      return 0;
    case disc::Result::kCancelled:
      std::printf("Stopped. Run this again with the same disc image to carry on from where it stopped.\n");
      return 130;
    case disc::Result::kCannotOpen:
      std::fprintf(stderr, "The file could not be opened:\n  %s\nIs the path right, and is the file "
                           "still being downloaded or used by another program?\n", image.c_str());
      break;
    case disc::Result::kNotGameDisc:
      std::fprintf(stderr, "This file is not an Xbox 360 game disc image: no game was found in it.\n");
      break;
    case disc::Result::kUnsafeName:
      std::fprintf(stderr, "The disc image contains a file name that cannot be used here, so nothing "
                           "was copied.\n");
      break;
    case disc::Result::kWrongVersion:
      std::fprintf(stderr, "This is a different game, or a different version of Ridge Racer 6, than "
                           "this build was made for.\nIt only works with the USA disc (title ID "
                           "4E4D07D3). Nothing was copied.\n");
      break;
    case disc::Result::kNoSpace: {
      unsigned long long needed = 0, available = 0;
      std::sscanf(detail.c_str(), "%llu %llu", &needed, &available);
      std::fprintf(stderr, "There is not enough free disk space for the game files: %.1f GB are "
                           "needed, and the drive this folder is on has %.1f GB free.\nFree some "
                           "space, or move this folder to another drive, and try again.\n",
                   Gigabytes(needed), Gigabytes(available));
      break;
    }
    case disc::Result::kReadError:
      std::fprintf(stderr, "The disc image could not be read to the end. It may be damaged or "
                           "incomplete.\nFiles copied so far are kept; a good image carries on from "
                           "there.\n");
      break;
    case disc::Result::kWriteError:
      std::fprintf(stderr, "A game file could not be written:\n  %s\nIs this folder read-only, or is "
                           "the drive full?\n", detail.c_str());
      break;
  }
  std::fprintf(stderr, "The game files were not copied.\n");
  return 1;
}
