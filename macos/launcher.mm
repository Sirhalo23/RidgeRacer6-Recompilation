// The release bundle's Finder entry point. The game and its runtime libraries
// remain private inside the app; user data never goes into the signed bundle.
#import <Cocoa/Cocoa.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "../launcher/disc_image.h"

namespace fs = std::filesystem;

static void Error(NSString* message, bool headless) {
  fprintf(stderr, "%s\n", message.UTF8String);
  if (!headless) {
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"Ridge Racer 6 could not start";
    alert.informativeText = message;
    [alert runModal];
  }
}

@interface CopyControl : NSObject
@property(nonatomic, assign) disc::Progress* progress;
- (void)cancel:(id)sender;
@end
@implementation CopyControl
- (void)cancel:(id)sender {
  (void)sender;
  self.progress->cancel = true;
}
@end

static bool Prepare(const fs::path& iso, const fs::path& game, bool headless) {
  disc::Progress progress;
  disc::Options options;
  options.image = iso;
  options.out = game;
  options.expected_sha256 = "39D3C0004EC62AEB0FE3E7E1889CF25D98FBD27987B6BC6B5FF30A56FFBA6C00";
  NSWindow* window = nil;
  NSProgressIndicator* bar = nil;
  NSTextField* label = nil;
  CopyControl* control = [CopyControl new];
  control.progress = &progress;
  if (!headless) {
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 460, 150)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Preparing Ridge Racer 6";
    label = [NSTextField labelWithString:@"Checking your USA disc image…"];
    label.frame = NSMakeRect(20, 100, 420, 28);
    [window.contentView addSubview:label];
    bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 70, 420, 16)];
    bar.indeterminate = NO;
    bar.maxValue = 100;
    [window.contentView addSubview:bar];
    NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:control action:@selector(cancel:)];
    cancel.frame = NSMakeRect(340, 20, 100, 32);
    [window.contentView addSubview:cancel];
    [window center];
    [window makeKeyAndOrderFront:nil];
  }
  std::atomic<bool> finished{false};
  disc::Result result = disc::Result::kOk;
  std::string detail;
  std::thread worker([&] {
    result = disc::Extract(options, &progress, &detail);
    finished = true;
  });
  while (!finished) {
    if (headless) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      const uint64_t total = progress.total.load();
      if (total) {
        const double percent = 100.0 * progress.done.load() / total;
        bar.doubleValue = percent;
        label.stringValue = [NSString stringWithFormat:@"Copying game files: %.0f%% (about 6 GB)", percent];
      }
      NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
          untilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]
          inMode:NSDefaultRunLoopMode dequeue:YES];
      if (event) [NSApp sendEvent:event];
      [NSApp updateWindows];
    }
  }
  worker.join();
  [window orderOut:nil];
  if (result == disc::Result::kOk) return true;
  if (result == disc::Result::kCancelled) return false;
  NSString* reason = @"The image could not be copied. Check that it is complete and that the disk has at least 6 GB free. Partial files are kept for the next attempt.";
  if (result == disc::Result::kWrongVersion)
    reason = @"This build needs the Ridge Racer 6 USA disc image. The executable on this image does not match.";
  else if (result == disc::Result::kNotGameDisc || result == disc::Result::kDamaged || result == disc::Result::kUnsafeName)
    reason = @"This is not a supported, intact Xbox 360 game disc image.";
  else if (result == disc::Result::kNoSpace)
    reason = @"There is not enough free disk space to copy the game files (about 6 GB).";
  Error(reason, headless);
  return false;
}

int main(int argc, char** argv) {
  @autoreleasepool {
    bool prepareOnly = false;
    fs::path iso, data;
    std::vector<std::string> extra;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--prepare-only") prepareOnly = true;
      else if (arg == "--data-root" && i + 1 < argc) data = fs::absolute(argv[++i]);
      else if (!arg.empty() && arg.front() != '-' && iso.empty()) iso = fs::absolute(arg);
      else extra.push_back(arg);
    }
    if (prepareOnly && iso.empty()) {
      fprintf(stderr, "Usage: rr6-launcher --prepare-only image.iso [--data-root folder]\n");
      return 2;
    }
    if (!prepareOnly) {
      [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      [NSApp activateIgnoringOtherApps:YES];
    }
    NSBundle* bundle = [NSBundle mainBundle];
    if (data.empty()) {
      NSURL* support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
          inDomains:NSUserDomainMask].firstObject;
      data = fs::path(support.fileSystemRepresentation) / "Ridge Racer 6";
    }
    const fs::path game = data / "game";
    const bool ready = fs::is_regular_file(game / disc::kReadyMarker) &&
        !fs::exists(game / disc::kCopyingMarker) && fs::is_regular_file(game / "default.xex");
    // An explicit ISO always runs the extractor's version check. Otherwise a
    // complete cache launches directly, even if the ISO or app has moved.
    if (!ready || !iso.empty()) {
      if (iso.empty()) {
        fs::path beside = fs::path(bundle.bundleURL.fileSystemRepresentation).parent_path();
        std::vector<fs::path> candidates;
        std::error_code ec;
        for (fs::directory_iterator it(beside, ec), end; !ec && it != end; it.increment(ec)) {
          std::string ext = it->path().extension().string();
          for (char& c : ext) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
          if (it->is_regular_file(ec) && ext == ".iso") candidates.push_back(it->path());
        }
        if (candidates.size() == 1) iso = candidates.front();
        else {
          NSOpenPanel* panel = [NSOpenPanel openPanel];
          panel.title = @"Choose your Ridge Racer 6 USA disc image";
          panel.canChooseDirectories = NO;
          panel.allowsMultipleSelection = NO;
          if ([panel runModal] != NSModalResponseOK) return 0;
          iso = panel.URL.fileSystemRepresentation;
        }
      }
      if (!Prepare(iso, game, prepareOnly)) return 1;
    }
    if (prepareOnly) {
      printf("Game files ready: %s\n", game.c_str());
      return 0;
    }
    std::error_code ec;
    fs::create_directories(data / "logs", ec);
    if (ec) { Error(@"The user-data folder could not be created.", false); return 1; }
    fs::create_directories(data / "DLC", ec);
    if (ec) { Error(@"The DLC folder could not be created.", false); return 1; }
    fs::path bin = fs::path(bundle.bundleURL.fileSystemRepresentation) / "Contents/MacOS";
    const fs::path resources = bin.parent_path() / "Resources";
    const fs::path config = data / "rr6_recomp.toml";
    if (!fs::exists(config)) {
      fs::copy_file(resources / "rr6_recomp.toml", config, fs::copy_options::none, ec);
      if (ec) { Error(@"Default settings could not be copied.", false); return 1; }
    }
    // The application hook reads this only to keep settings outside the bundle.
    setenv("RR6_MACOS_RELEASE_USER_ROOT", data.c_str(), 1);
    std::vector<std::string> args = {(bin / "rr6_recomp").string(),
        "--game_data_root=" + game.string(), "--user_data_root=" + (data / "userdata").string(),
        "--gpu_plugin=xenos", "--hid_mappings_file=" + (resources / "gamecontrollerdb.txt").string(),
        "--rr6_dlc_folder=" + (data / "DLC").string(),
        "--log_file=" + (data / "logs/run.log").string()};
    // CLI11 rejects duplicate options. Explicit launch flags replace the
    // launcher's defaults, while unrelated game flags are simply forwarded.
    for (const auto& arg : extra) {
      if (arg.rfind("--", 0) == 0) {
        const std::string key = arg.substr(0, arg.find('='));
        for (auto it = args.begin() + 1; it != args.end();) {
          if (it->substr(0, it->find('=')) == key) it = args.erase(it);
          else ++it;
        }
      }
    }
    args.insert(args.end(), extra.begin(), extra.end());
    std::vector<char*> pointers;
    for (auto& arg : args) pointers.push_back(arg.data());
    pointers.push_back(nullptr);
    execv(pointers[0], pointers.data());
    Error(@"The bundled game executable could not start.", false);
    return 1;
  }
}
