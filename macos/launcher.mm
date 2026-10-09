// The release bundle's Finder entry point. The game and its runtime libraries
// remain private inside the app; user data never goes into the signed bundle.
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "../launcher/disc_image.h"
#include "settings.h"

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

static NSString* Text(const std::string& text) {
  return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}
static bool Ready(const fs::path& data) {
  std::error_code ec;
  const fs::path game = data / "game";
  return fs::is_regular_file(game / disc::kReadyMarker, ec) &&
      !fs::exists(game / disc::kCopyingMarker, ec) && fs::is_regular_file(game / "default.xex", ec);
}
static fs::path BesideApp(NSBundle* bundle) {
  const fs::path beside = fs::path(bundle.bundleURL.fileSystemRepresentation).parent_path();
  std::vector<fs::path> images;
  std::error_code ec;
  for (fs::directory_iterator it(beside, ec), end; !ec && it != end; it.increment(ec)) {
    std::string ext = it->path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (it->is_regular_file(ec) && ext == ".iso") images.push_back(it->path());
  }
  return images.size() == 1 ? images.front() : fs::path{};
}
static fs::path ChooseImage() {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.title = @"Choose your Ridge Racer 6 USA disc image";
  panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"iso"]];
  panel.canChooseDirectories = NO;
  panel.allowsMultipleSelection = NO;
  return [panel runModal] == NSModalResponseOK ? fs::path(panel.URL.fileSystemRepresentation) : fs::path{};
}
static std::vector<std::string> GameArguments(const fs::path& data, NSBundle* bundle,
                                             const std::vector<std::string>& extra) {
  const fs::path contents = fs::path(bundle.bundleURL.fileSystemRepresentation) / "Contents";
  std::vector<std::string> args = {
      "--game_data_root=" + (data / "game").string(),
      "--user_data_root=" + (data / "userdata").string(), "--gpu_plugin=xenos",
      "--hid_mappings_file=" + (contents / "Resources/gamecontrollerdb.txt").string(),
      "--rr6_dlc_folder=" + (data / "DLC").string(),
      "--log_file=" + (data / "logs/run.log").string()};
  for (const auto& arg : extra) {
    if (arg.rfind("--", 0) != 0) continue;
    const std::string key = arg.substr(0, arg.find('='));
    args.erase(std::remove_if(args.begin(), args.end(), [&](const auto& value) {
      return value.substr(0, value.find('=')) == key;
    }), args.end());
  }
  args.insert(args.end(), extra.begin(), extra.end());
  return args;
}

struct Binding { const char* action; const char* xbox; const char* ps; const char* key; const char* fallback; };
static const Binding kBindings[] = {
    {"Steer left", "Left stick ←", "Left stick ←", "keybind_lstick_left", "Left,A"},
    {"Steer right", "Left stick →", "Left stick →", "keybind_lstick_right", "Right,D"},
    {"Navigate up", "Left stick ↑", "Left stick ↑", "keybind_lstick_up", "Up,W"},
    {"Navigate down", "Left stick ↓", "Left stick ↓", "keybind_lstick_down", "Down,S"},
    {"Accelerate", "RT", "R2", "keybind_right_trigger", "Up,W"},
    {"Brake", "LT", "L2", "keybind_left_trigger", "Down,S"},
    {"Confirm", "A", "Cross ✕", "keybind_a", "Space"},
    {"Back", "B", "Circle ○", "keybind_b", "Backspace,B"},
    {"X button", "X", "Square □", "keybind_x", "X"},
    {"Y button", "Y", "Triangle △", "keybind_y", "Y"},
    {"Right shoulder", "RB", "R1", "keybind_right_shoulder", "E"},
    {"Left shoulder", "LB", "L1", "keybind_left_shoulder", "Q"},
    {"Start / pause", "Menu", "Options", "keybind_start", "Return,P"},
    {"Select / back", "View", "Share", "keybind_back", "Tab"},
    {"Left stick click", "LS", "L3", "keybind_lstick_press", "F"},
    {"Right stick click", "RS", "R3", "keybind_rstick_press", "K"},
    {"Right stick up", "Right stick ↑", "Right stick ↑", "keybind_rstick_up", "Up"},
    {"Right stick down", "Right stick ↓", "Right stick ↓", "keybind_rstick_down", "Down"},
    {"Right stick left", "Right stick ←", "Right stick ←", "keybind_rstick_left", "Left"},
    {"Right stick right", "Right stick →", "Right stick →", "keybind_rstick_right", "Right"},
    {"D-pad up", "D-pad ↑", "D-pad ↑", "keybind_dpad_up", "Shift+Up"},
    {"D-pad down", "D-pad ↓", "D-pad ↓", "keybind_dpad_down", "Shift+Down"},
    {"D-pad left", "D-pad ←", "D-pad ←", "keybind_dpad_left", "Shift+Left"},
    {"D-pad right", "D-pad →", "D-pad →", "keybind_dpad_right", "Shift+Right"},
};
static constexpr NSInteger kBindingCount = sizeof(kBindings) / sizeof(kBindings[0]);

@interface Launcher : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource,
                                NSTableViewDelegate> {
  fs::path _data, _iso;
  std::vector<std::string> _extra;
  std::map<std::string, std::string> _changes;
  std::string _document;
  bool _preparing;
  bool _controllerSDL;
  SDL_Gamepad* _preview;
}
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSTextField* status;
@property(nonatomic, strong) NSTextField* gameStatus;
@property(nonatomic, strong) NSTextField* controllerStatus;
@property(nonatomic, strong) NSTextField* controllerInput;
@property(nonatomic, strong) NSPopUpButton* controllerList;
@property(nonatomic, strong) NSButton* rumbleButton;
@property(nonatomic, strong) NSTabView* tabs;
@property(nonatomic, strong) NSButton* playButton;
@property(nonatomic, strong) NSButton* saveButton;
@property(nonatomic, strong) NSTableView* bindings;
@property(nonatomic, strong) NSMutableArray<NSString*>* keys;
@property(nonatomic, strong) NSMutableDictionary<NSString*, NSControl*>* controls;
@property(nonatomic, strong) NSTask* gameTask;
@property(nonatomic, strong) NSTimer* controllerTimer;
@property(nonatomic, strong) NSPopUpButton* buttonNames;
@property(nonatomic, strong) NSButton* setKeyButton;
@property(nonatomic, strong) NSButton* clearKeyButton;
@property(nonatomic, assign) BOOL psNames;
- (instancetype)initWithData:(fs::path)data image:(fs::path)iso extra:(std::vector<std::string>)extra;
- (void)show;
- (void)play:(id)sender;
@end

@implementation Launcher
- (instancetype)initWithData:(fs::path)data image:(fs::path)iso extra:(std::vector<std::string>)extra {
  if ((self = [super init])) { _data = std::move(data); _iso = std::move(iso); _extra = std::move(extra); }
  return self;
}
- (NSString*)get:(const char*)key fallback:(const char*)fallback {
  return Text(launcher::Setting(_document, key, fallback));
}
- (NSTextField*)label:(NSString*)text view:(NSView*)view frame:(NSRect)frame {
  NSTextField* label = [NSTextField wrappingLabelWithString:text];
  label.frame = frame;
  [view addSubview:label];
  return label;
}
- (NSButton*)button:(NSString*)title action:(SEL)action view:(NSView*)view frame:(NSRect)frame {
  NSButton* button = [NSButton buttonWithTitle:title target:self action:action];
  button.frame = frame;
  [view addSubview:button];
  return button;
}
- (NSPopUpButton*)popup:(NSString*)title key:(NSString*)key items:(NSArray<NSString*>*)items
                  tags:(NSArray<NSNumber*>*)tags selected:(NSInteger)selected
                  view:(NSView*)view y:(CGFloat)y {
  [self label:title view:view frame:NSMakeRect(20, y + 4, 190, 24)];
  NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(210, y, 490, 28) pullsDown:NO];
  [popup addItemsWithTitles:items];
  for (NSUInteger i = 0; i < tags.count; ++i) [popup itemAtIndex:i].tag = tags[i].integerValue;
  NSMenuItem* item = [popup.menu itemWithTag:selected];
  if (!item) {
    [popup addItemWithTitle:@"Custom (keep current setting)"];
    popup.lastItem.tag = -999;
    item = popup.lastItem;
  }
  [popup selectItem:item];
  popup.identifier = key;
  popup.target = self;
  popup.action = @selector(changed:);
  [view addSubview:popup];
  self.controls[key] = popup;
  return popup;
}
- (NSButton*)check:(NSString*)title key:(NSString*)key fallback:(const char*)fallback
              view:(NSView*)view y:(CGFloat)y {
  NSButton* button = [NSButton checkboxWithTitle:title target:self action:@selector(changed:)];
  button.frame = NSMakeRect(20, y, 680, 26);
  button.identifier = key;
  button.state = [[self get:key.UTF8String fallback:fallback] isEqualToString:@"true"]
      ? NSControlStateValueOn : NSControlStateValueOff;
  [view addSubview:button];
  self.controls[key] = button;
  return button;
}
- (BOOL)reloadSettings {
  NSError* error = nil;
  NSString* text = [NSString stringWithContentsOfFile:Text((_data / "rr6_recomp.toml").string())
      encoding:NSUTF8StringEncoding error:&error];
  if (!text) { Error([@"Settings could not be read: " stringByAppendingString:error.localizedDescription], false); return NO; }
  _document = text.UTF8String;
  _changes.clear();
  return YES;
}
- (void)buildPages {
  for (NSTabViewItem* item in [self.tabs.tabViewItems copy]) [self.tabs removeTabViewItem:item];
  self.controls = [NSMutableDictionary new];
  NSView* display = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 740, 450)];
  NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"display"];
  item.label = @"Display"; item.view = display; [self.tabs addTabViewItem:item];
  [self popup:@"Language" key:@"user_language"
      items:@[@"English", @"日本語 (Japanese)", @"Deutsch", @"Français", @"Español", @"Italiano"]
      tags:@[@1,@2,@3,@4,@5,@6] selected:[self get:"user_language" fallback:"1"].integerValue view:display y:406];
  [self popup:@"Screen" key:@"fullscreen" items:@[@"Full screen", @"Window"] tags:@[@1,@0]
      selected:[[self get:"fullscreen" fallback:"true"] isEqualToString:@"true"] view:display y:366];
  // Match the SDK's alias precedence, which tests non-default *values*.
  NSInteger shared = [self get:"resolution_scale" fallback:"1"].integerValue;
  NSInteger x = [self get:"draw_resolution_scale_x" fallback:"1"].integerValue;
  NSInteger y = [self get:"draw_resolution_scale_y" fallback:"1"].integerValue;
  if (x == 1) x = shared;
  if (y == 1) y = shared;
  [self popup:@"Render resolution" key:@"render_scale"
      items:@[@"1× — 720p (recommended)", @"2× — 1440p (experimental)", @"3× — 2160p / 4K (experimental)"]
      tags:@[@1,@2,@3] selected:x == y ? x : -999 view:display y:326];
  [self popup:@"Retina output" key:@"window_high_pixel_density"
      items:@[@"Logical resolution (lower GPU cost)", @"High density (sharper output)"] tags:@[@0,@1]
      selected:[[self get:"window_high_pixel_density" fallback:"false"] isEqualToString:@"true"] view:display y:286];
  BOOL immediate = [[self get:"vulkan_allow_present_mode_immediate" fallback:"true"] isEqualToString:@"true"];
  BOOL mailbox = [[self get:"vulkan_allow_present_mode_mailbox" fallback:"true"] isEqualToString:@"true"];
  BOOL relaxed = [[self get:"vulkan_allow_present_mode_fifo_relaxed" fallback:"true"] isEqualToString:@"true"];
  [self popup:@"Display VSync" key:@"display_vsync"
      items:@[@"Off — prefer lowest latency", @"On — sync presentation to the display"] tags:@[@0,@1]
      selected:(!immediate && !mailbox && !relaxed) ? 1 : (immediate && mailbox && relaxed) ? 0 : -999
      view:display y:246];
  NSString* smooth = [self get:"swap_post_effect" fallback:"none"];
  NSArray* effects = @[@"none", @"fxaa", @"fxaa_extreme"];
  NSUInteger effect = [effects indexOfObject:smooth];
  [self popup:@"Edge smoothing" key:@"swap_post_effect" items:@[@"Off", @"FXAA", @"FXAA Extreme"]
      tags:@[@0,@1,@2] selected:effect == NSNotFound ? -999 : (NSInteger)effect view:display y:206];
  [self popup:@"Texture filtering" key:@"anisotropic_override" items:@[@"4× (standard)", @"8×", @"16×"]
      tags:@[@3,@4,@5] selected:[self get:"anisotropic_override" fallback:"3"].integerValue view:display y:166];
  [self check:@"Compile shaders asynchronously" key:@"async_shader_compilation" fallback:"true" view:display y:124];
  NSTextField* note = [self label:@"720p with logical-resolution output keeps GPU work lower. Higher render scales are experimental and use more memory. VSync may add latency. New shaders can still cause brief drops.\n\nWhile playing: F3 shows statistics; F4 opens advanced settings. Changes here apply on the next launch."
      view:display frame:NSMakeRect(20, 15, 680, 92)];
  note.font = [NSFont systemFontOfSize:12]; note.textColor = NSColor.secondaryLabelColor;

  NSView* controls = [[NSView alloc] initWithFrame:display.frame];
  item = [[NSTabViewItem alloc] initWithIdentifier:@"controls"];
  item.label = @"Controls"; item.view = controls; [self.tabs addTabViewItem:item];
  self.controllerStatus = [self label:@"Checking connected controllers…" view:controls frame:NSMakeRect(20, 399, 680, 38)];
  self.controllerList = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(20, 360, 490, 28) pullsDown:NO];
  self.controllerList.target = self; self.controllerList.action = @selector(controllerSelected:);
  [controls addSubview:self.controllerList];
  self.rumbleButton = [self button:@"Test Vibration" action:@selector(rumble:) view:controls frame:NSMakeRect(530, 359, 170, 30)];
  self.controllerInput = [self label:@"" view:controls frame:NSMakeRect(20, 316, 680, 40)];
  self.controllerInput.font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];
  [self check:@"Use keyboard alongside controllers" key:@"mnk_mode" fallback:"true" view:controls y:286];
  self.buttonNames = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(484, 284, 216, 28) pullsDown:NO];
  [self.buttonNames addItemsWithTitles:@[@"Xbox button names", @"PlayStation button names"]];
  [self.buttonNames selectItemAtIndex:self.psNames ? 1 : 0];
  self.buttonNames.target = self; self.buttonNames.action = @selector(namesChanged:);
  [controls addSubview:self.buttonNames];
  NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 88, 680, 186)];
  scroll.hasVerticalScroller = YES; scroll.borderType = NSBezelBorder;
  self.bindings = [[NSTableView alloc] initWithFrame:scroll.bounds];
  self.bindings.rowHeight = 25;
  self.bindings.usesAlternatingRowBackgroundColors = YES;
  for (NSArray* column in @[@[@"action", @"Action", @240], @[@"controller", @"Controller", @190], @[@"keys", @"Keyboard", @220]]) {
    NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:column[0]];
    col.title = column[1]; col.width = [column[2] doubleValue];
    col.editable = [col.identifier isEqualToString:@"keys"];
    [self.bindings addTableColumn:col];
  }
  self.keys = [NSMutableArray new];
  for (const auto& binding : kBindings) [self.keys addObject:[self get:binding.key fallback:binding.fallback]];
  self.bindings.dataSource = self; self.bindings.delegate = self;
  scroll.documentView = self.bindings; [controls addSubview:scroll];
  self.setKeyButton = [self button:@"Set Key…" action:@selector(setKey:) view:controls frame:NSMakeRect(20, 50, 110, 30)];
  self.clearKeyButton = [self button:@"Clear" action:@selector(clearKey:) view:controls frame:NSMakeRect(135, 50, 80, 30)];
  [self button:@"Reset Keyboard" action:@selector(resetKeys:) view:controls frame:NSMakeRect(530, 50, 170, 30)];
  note = [self label:@"Pair Xbox / DualShock 4 in macOS Bluetooth settings, or connect USB. The game uses Xbox prompts. Button names here are a reference. Edit comma-separated keys for alternatives; Shift, Ctrl and Alt modifiers work."
      view:controls frame:NSMakeRect(20, 3, 680, 43)];
  note.font = [NSFont systemFontOfSize:12]; note.textColor = NSColor.secondaryLabelColor;
  [self updateKeyboard];

  NSView* files = [[NSView alloc] initWithFrame:display.frame];
  item = [[NSTabViewItem alloc] initWithIdentifier:@"files"];
  item.label = @"Game Files"; item.view = files; [self.tabs addTabViewItem:item];
  [self label:@"Your Ridge Racer 6 USA disc" view:files frame:NSMakeRect(20, 397, 680, 28)].font = [NSFont boldSystemFontOfSize:15];
  [self label:@"The first Play verifies your ISO and copies about 6 GB. Put one ISO beside the app to select it automatically, or choose it below. Later launches reuse the extracted files."
      view:files frame:NSMakeRect(20, 335, 680, 56)];
  [self button:@"Choose / Import ISO…" action:@selector(importImage:) view:files frame:NSMakeRect(20, 290, 200, 34)];
  [self label:@"DLC" view:files frame:NSMakeRect(20, 244, 680, 28)].font = [NSFont boldSystemFontOfSize:15];
  [self label:@"Put your own DLC packages in the DLC folder. New or changed packages are checked and installed when the game starts."
      view:files frame:NSMakeRect(20, 192, 680, 48)];
  [self button:@"Open DLC Folder" action:@selector(openDLC:) view:files frame:NSMakeRect(20, 150, 180, 34)];
  [self label:@"Settings, saves, game files and logs stay outside the app. Replacing the app keeps them."
      view:files frame:NSMakeRect(20, 90, 680, 48)];
  [self button:@"Open Data Folder" action:@selector(openData:) view:files frame:NSMakeRect(20, 48, 180, 34)];
  [self button:@"Open Logs" action:@selector(openLogs:) view:files frame:NSMakeRect(210, 48, 130, 34)];
  note = [self label:Text(_data.string()) view:files frame:NSMakeRect(20, 8, 680, 34)];
  note.font = [NSFont systemFontOfSize:11]; note.textColor = NSColor.secondaryLabelColor;
}
- (void)show {
  if (![self reloadSettings]) { [NSApp terminate:nil]; return; }
  self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 780, 674)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
      backing:NSBackingStoreBuffered defer:NO];
  self.window.releasedWhenClosed = NO;
  self.window.title = @"Ridge Racer 6"; self.window.delegate = self;
  NSView* view = self.window.contentView;
  [self label:@"RIDGE RACER 6" view:view frame:NSMakeRect(28, 614, 720, 40)].font = [NSFont boldSystemFontOfSize:26];
  self.gameStatus = [self label:@"" view:view frame:NSMakeRect(28, 582, 720, 30)];
  self.gameStatus.textColor = NSColor.secondaryLabelColor;
  // Leave breathing room below the tab strip on every page. Page controls
  // retain their bottom margins while the taller content area adds top space.
  self.tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(20, 76, 740, 498)];
  [view addSubview:self.tabs];
  [self buildPages];
  self.status = [self label:@"" view:view frame:NSMakeRect(28, 19, 400, 44)];
  self.status.font = [NSFont systemFontOfSize:12]; self.status.textColor = NSColor.secondaryLabelColor;
  self.saveButton = [self button:@"Save Settings" action:@selector(save:) view:view frame:NSMakeRect(458, 24, 155, 36)];
  self.playButton = [self button:@"Play" action:@selector(play:) view:view frame:NSMakeRect(624, 24, 128, 36)];
  self.playButton.keyEquivalent = @"\r";
  [self updateReady];
  [self updateControllers:nil];
  self.controllerTimer = [NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(updateControllers:) userInfo:nil repeats:YES];
  [NSRunLoop.mainRunLoop addTimer:self.controllerTimer forMode:NSRunLoopCommonModes];
  [self.window center]; [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}
- (void)updateReady {
  self.gameStatus.stringValue = Ready(_data) ? @"Game files ready. Choose your settings, then Play."
      : @"Game files need preparing. Play will ask for your USA disc image.";
}
- (void)updateControllers:(id)sender {
  (void)sender;
  if (_preparing || self.gameTask.running) return;
  if (!_controllerSDL) {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      self.controllerStatus.stringValue = @"Controller detection is unavailable. The game will try again when you Play.";
      return;
    }
    _controllerSDL = true;
    const fs::path mappings = fs::path(NSBundle.mainBundle.resourceURL.fileSystemRepresentation) / "gamecontrollerdb.txt";
    SDL_AddGamepadMappingsFromFile(mappings.c_str());
    SDL_SetGamepadEventsEnabled(false); SDL_SetJoystickEventsEnabled(false);
  }
  SDL_UpdateGamepads();
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  NSMutableArray<NSString*>* names = [NSMutableArray new];
  NSMutableArray<NSNumber*>* tags = [NSMutableArray new];
  for (int i = 0; i < count; ++i) {
    const char* name = SDL_GetGamepadNameForID(ids[i]);
    [names addObject:Text(name ? name : "Controller")]; [tags addObject:@(ids[i])];
  }
  NSArray* oldTags = [self.controllerList.itemArray valueForKey:@"tag"];
  if (![names isEqualToArray:self.controllerList.itemTitles] || ![tags isEqualToArray:oldTags]) {
    NSInteger selected = self.controllerList.selectedItem.tag;
    [self.controllerList removeAllItems]; [self.controllerList addItemsWithTitles:names];
    for (int i = 0; i < count; ++i) [self.controllerList itemAtIndex:i].tag = ids[i];
    NSMenuItem* previous = [self.controllerList.menu itemWithTag:selected];
    if (previous) [self.controllerList selectItem:previous];
  }
  SDL_free(ids);
  self.controllerList.enabled = count > 0;
  self.controllerStatus.stringValue = count ? @"Controller test — select a device, then move the sticks, triggers and buttons."
      : @"No controller connected. Pair in macOS Bluetooth settings or connect USB.\nKeyboard controls are available below.";
  SDL_JoystickID selected = (SDL_JoystickID)self.controllerList.selectedItem.tag;
  if (_preview && (!SDL_GamepadConnected(_preview) || SDL_GetGamepadID(_preview) != selected)) {
    SDL_CloseGamepad(_preview); _preview = nullptr;
  }
  if (!_preview && count) _preview = SDL_OpenGamepad(selected);
  self.rumbleButton.enabled = _preview && SDL_GetBooleanProperty(SDL_GetGamepadProperties(_preview), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
  if (!_preview) { self.controllerInput.stringValue = @""; return; }
  NSMutableArray* pressed = [NSMutableArray new];
  for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
    if (!SDL_GetGamepadButton(_preview, (SDL_GamepadButton)button)) continue;
    NSString* label = nil;
    if (button < 4) label = self.psNames ? @[@"Cross",@"Circle",@"Square",@"Triangle"][button] : @[@"A",@"B",@"X",@"Y"][button];
    else label = Text(SDL_GetGamepadStringForButton((SDL_GamepadButton)button));
    [pressed addObject:label];
  }
  self.controllerInput.stringValue = [NSString stringWithFormat:
      @"Left: %+.2f, %+.2f   Right: %+.2f, %+.2f   LT / RT: %.0f%% / %.0f%%\nButtons: %@",
      SDL_GetGamepadAxis(_preview, SDL_GAMEPAD_AXIS_LEFTX) / 32768.0,
      SDL_GetGamepadAxis(_preview, SDL_GAMEPAD_AXIS_LEFTY) / 32768.0,
      SDL_GetGamepadAxis(_preview, SDL_GAMEPAD_AXIS_RIGHTX) / 32768.0,
      SDL_GetGamepadAxis(_preview, SDL_GAMEPAD_AXIS_RIGHTY) / 32768.0,
      100.0 * SDL_GetGamepadAxis(_preview, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0,
      100.0 * SDL_GetGamepadAxis(_preview, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0,
      pressed.count ? [pressed componentsJoinedByString:@", "] : @"—"];
}
- (void)controllerSelected:(id)sender { (void)sender; [self updateControllers:nil]; }
- (void)rumble:(id)sender {
  (void)sender;
  if (_preview) self.status.stringValue = SDL_RumbleGamepad(_preview, 22000, 22000, 300)
      ? @"Vibration test sent to the selected controller."
      : @"This controller could not vibrate. Try USB or another connection.";
}
- (void)stopControllers {
  if (_preview) { SDL_CloseGamepad(_preview); _preview = nullptr; }
  if (_controllerSDL) { SDL_QuitSubSystem(SDL_INIT_GAMEPAD); _controllerSDL = false; }
}
- (void)updateKeyboard {
  BOOL enabled = ((NSButton*)self.controls[@"mnk_mode"]).state == NSControlStateValueOn;
  self.bindings.enabled = enabled; self.setKeyButton.enabled = enabled; self.clearKeyButton.enabled = enabled;
}
- (void)changed:(NSControl*)sender {
  NSString* key = sender.identifier;
  if ([sender isKindOfClass:NSPopUpButton.class]) {
    NSInteger value = ((NSPopUpButton*)sender).selectedItem.tag;
    if (value == -999) {
      // Choosing Custom again discards that control's pending preset edits.
      if ([key isEqualToString:@"render_scale"])
        for (const auto* alias : {"resolution_scale", "draw_resolution_scale_x", "draw_resolution_scale_y"}) _changes.erase(alias);
      else if ([key isEqualToString:@"display_vsync"])
        for (const auto* flag : {"vulkan_allow_present_mode_immediate", "vulkan_allow_present_mode_mailbox", "vulkan_allow_present_mode_fifo_relaxed"}) _changes.erase(flag);
      else _changes.erase(key.UTF8String);
      self.status.stringValue = _changes.empty() ? @"Original settings retained." : @"Unsaved changes. Save Settings or Play to apply them.";
      return;
    }
    if ([key isEqualToString:@"render_scale"]) launcher::RenderScale(_changes, (int)value);
    else if ([key isEqualToString:@"display_vsync"]) launcher::DisplayVSync(_changes, value != 0);
    else if ([key isEqualToString:@"swap_post_effect"])
      _changes[key.UTF8String] = launcher::Quote(std::vector<std::string>{"none","fxaa","fxaa_extreme"}.at(value));
    else if ([key isEqualToString:@"fullscreen"] || [key isEqualToString:@"window_high_pixel_density"])
      _changes[key.UTF8String] = value ? "true" : "false";
    else _changes[key.UTF8String] = std::to_string(value);
  } else _changes[key.UTF8String] = ((NSButton*)sender).state == NSControlStateValueOn ? "true" : "false";
  [self updateKeyboard];
  self.status.stringValue = @"Unsaved changes. Save Settings or Play to apply them.";
}
- (BOOL)saveSettings {
  [self.window makeFirstResponder:nil];  // Commit an edited keyboard table cell.
  if (_changes.empty()) { self.status.stringValue = @"Settings are already saved."; return YES; }
  // Re-read before applying just the edited keys, preserving concurrent edits
  // from F4 or a text editor instead of rewriting the launch-time snapshot.
  NSError* error = nil;
  NSString* path = Text((_data / "rr6_recomp.toml").string());
  NSString* current = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
  if (!current) { Error(error.localizedDescription, false); return NO; }
  std::string updated = launcher::UpdateSettings(current.UTF8String, _changes);
  NSData* bytes = [NSData dataWithBytes:updated.data() length:updated.size()];
  if (![bytes writeToFile:path options:NSDataWritingAtomic error:&error]) {
    Error([@"Settings could not be saved: " stringByAppendingString:error.localizedDescription], false); return NO;
  }
  _document = std::move(updated); _changes.clear();
  self.status.stringValue = @"Settings saved. They apply when you press Play.";
  return YES;
}
- (void)save:(id)sender { (void)sender; [self saveSettings]; }
- (BOOL)prepareGame:(BOOL)choose {
  fs::path iso = choose ? ChooseImage() : _iso;
  if (!choose && iso.empty() && Ready(_data)) return YES;
  if (!choose && iso.empty()) iso = BesideApp(NSBundle.mainBundle);
  if (!choose && iso.empty()) iso = ChooseImage();
  if (iso.empty()) return NO;
  _preparing = true;
  self.playButton.enabled = NO; self.saveButton.enabled = NO; self.tabs.hidden = YES;
  [self.window orderOut:nil];
  BOOL ready = Prepare(iso, _data / "game", false);
  _preparing = false;
  self.playButton.enabled = YES; self.saveButton.enabled = YES; self.tabs.hidden = NO;
  [self.window makeKeyAndOrderFront:nil];
  if (ready) _iso.clear();
  [self updateReady];
  return ready;
}
- (void)importImage:(id)sender { (void)sender; [self prepareGame:YES]; }
- (void)play:(id)sender {
  (void)sender;
  if (_preparing || self.gameTask.running || ![self saveSettings] || ![self prepareGame:NO]) return;
  const fs::path bin = fs::path(NSBundle.mainBundle.bundleURL.fileSystemRepresentation) / "Contents/MacOS";
  NSTask* task = [NSTask new];
  task.executableURL = [NSURL fileURLWithPath:Text((bin / "rr6_recomp").string())];
  NSMutableArray* arguments = [NSMutableArray new];
  for (const auto& arg : GameArguments(_data, NSBundle.mainBundle, _extra)) [arguments addObject:Text(arg)];
  task.arguments = arguments;
  NSMutableDictionary* environment = [NSProcessInfo.processInfo.environment mutableCopy];
  environment[@"RR6_MACOS_RELEASE_USER_ROOT"] = Text(_data.string());
  task.environment = environment;
  task.currentDirectoryURL = [NSURL fileURLWithPath:Text(_data.string()) isDirectory:YES];
  __weak Launcher* weakSelf = self;
  task.terminationHandler = ^(NSTask* stopped) {
    dispatch_async(dispatch_get_main_queue(), ^{
      Launcher* launcher = weakSelf;
      if (!launcher) return;
      launcher.gameTask = nil;
      launcher.playButton.enabled = YES; launcher.saveButton.enabled = YES;
      launcher.tabs.hidden = NO;
      if ([launcher reloadSettings]) [launcher buildPages];
      [launcher updateControllers:nil]; [launcher updateReady];
      launcher.status.stringValue = stopped.terminationStatus == 0 ? @"Game closed. Ready to play again."
          : @"Game stopped unexpectedly. Open Logs in Game Files for details.";
      [launcher.window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    });
  };
  // Release the preview device before starting SDL in the game process.
  [self stopControllers];
  NSError* error = nil;
  if (![task launchAndReturnError:&error]) { Error(error.localizedDescription, false); return; }
  self.gameTask = task;
  self.playButton.enabled = NO; self.saveButton.enabled = NO; self.tabs.hidden = YES;
  self.status.stringValue = @"Game running. Quit from the game to return to the launcher.";
  [self.window orderOut:nil];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView*)table { (void)table; return kBindingCount; }
- (id)tableView:(NSTableView*)table objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
  (void)table;
  if ([column.identifier isEqualToString:@"action"]) return Text(kBindings[row].action);
  if ([column.identifier isEqualToString:@"controller"]) return Text(self.psNames ? kBindings[row].ps : kBindings[row].xbox);
  return self.keys[row];
}
- (void)tableView:(NSTableView*)table setObjectValue:(id)value forTableColumn:(NSTableColumn*)column row:(NSInteger)row {
  (void)table;
  if (![column.identifier isEqualToString:@"keys"]) return;
  self.keys[row] = [value description];
  _changes[kBindings[row].key] = launcher::Quote(self.keys[row].UTF8String);
  self.status.stringValue = @"Unsaved keyboard changes.";
}
- (void)namesChanged:(NSPopUpButton*)sender {
  self.psNames = sender.indexOfSelectedItem == 1; [self.bindings reloadData];
}
- (void)clearKey:(id)sender {
  (void)sender;
  NSInteger row = self.bindings.selectedRow;
  if (row < 0) return;
  [self tableView:self.bindings setObjectValue:@"" forTableColumn:[self.bindings tableColumnWithIdentifier:@"keys"] row:row];
  [self.bindings reloadData];
}
- (void)resetKeys:(id)sender {
  (void)sender;
  for (NSInteger row = 0; row < kBindingCount; ++row)
    [self tableView:self.bindings setObjectValue:Text(kBindings[row].fallback)
        forTableColumn:[self.bindings tableColumnWithIdentifier:@"keys"] row:row];
  [self.bindings reloadData];
}
- (void)setKey:(id)sender {
  (void)sender;
  NSInteger row = self.bindings.selectedRow;
  if (row < 0) { self.status.stringValue = @"Select a row first, then Set Key."; return; }
  NSAlert* alert = [NSAlert new];
  alert.messageText = [@"Set key for " stringByAppendingString:Text(kBindings[row].action)];
  alert.informativeText = @"Press a key, with Shift, Control or Option if needed. This replaces the current binding. Command shortcuts are reserved by macOS. Escape cancels.";
  [alert addButtonWithTitle:@"Cancel"];
  __block NSString* captured = nil;
  id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* event) {
    if (event.keyCode == 53) { [NSApp stopModalWithCode:NSModalResponseCancel]; return nil; }
    if (event.modifierFlags & NSEventModifierFlagCommand) return nil;
    NSDictionary* special = @{@36:@"Return", @76:@"Return", @48:@"Tab", @49:@"Space", @51:@"Backspace",
        @117:@"Delete", @123:@"Left", @124:@"Right", @125:@"Down", @126:@"Up",
        @115:@"Home", @119:@"End", @116:@"PageUp", @121:@"PageDown"};
    NSString* name = special[@(event.keyCode)];
    NSString* chars = event.charactersIgnoringModifiers.uppercaseString;
    if (!name && chars.length == 1) {
      unichar c = [chars characterAtIndex:0];
      if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) name = chars;
      if (c >= NSF1FunctionKey && c <= NSF24FunctionKey) name = [NSString stringWithFormat:@"F%d", c - NSF1FunctionKey + 1];
    }
    if (!name) return nil;
    NSMutableString* key = [NSMutableString new];
    if (event.modifierFlags & NSEventModifierFlagShift) [key appendString:@"Shift+"];
    if (event.modifierFlags & NSEventModifierFlagControl) [key appendString:@"Ctrl+"];
    if (event.modifierFlags & NSEventModifierFlagOption) [key appendString:@"Alt+"];
    [key appendString:name]; captured = key;
    [NSApp stopModalWithCode:NSModalResponseOK]; return nil;
  }];
  [alert runModal];
  [NSEvent removeMonitor:monitor];
  if (captured) {
    [self tableView:self.bindings setObjectValue:captured forTableColumn:[self.bindings tableColumnWithIdentifier:@"keys"] row:row];
    [self.bindings reloadData];
  }
}
- (void)openDLC:(id)sender { (void)sender; [self openFolder:_data / "DLC"]; }
- (void)openData:(id)sender { (void)sender; [self openFolder:_data]; }
- (void)openLogs:(id)sender { (void)sender; [self openFolder:_data / "logs"]; }
- (void)openFolder:(fs::path)path {
  [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:Text(path.string()) isDirectory:YES]];
}
- (BOOL)windowShouldClose:(NSWindow*)window {
  (void)window;
  if (self.gameTask.running) { [self.window orderOut:nil]; return NO; }
  [NSApp terminate:nil]; return NO;  // A cancelled quit must keep this window open.
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
  (void)app;
  if (_preparing) return NSTerminateCancel;
  if (self.gameTask.running) {
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"The game is still running";
    alert.informativeText = @"Quit from the game to finish saving and return to the launcher.";
    [alert addButtonWithTitle:@"Keep Playing"];
    [alert runModal]; return NSTerminateCancel;
  }
  [self.window makeFirstResponder:nil];
  if (!_changes.empty()) {
    NSAlert* alert = [NSAlert new]; alert.messageText = @"Save your settings before closing?";
    [alert addButtonWithTitle:@"Save"]; [alert addButtonWithTitle:@"Discard"]; [alert addButtonWithTitle:@"Cancel"];
    NSModalResponse reply = [alert runModal];
    if (reply == NSAlertThirdButtonReturn || (reply == NSAlertFirstButtonReturn && ![self saveSettings])) return NSTerminateCancel;
  }
  [self.controllerTimer invalidate]; [self stopControllers]; return NSTerminateNow;
}
- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)visible {
  (void)app; (void)visible;
  if (self.gameTask.running) {
    NSRunningApplication* game = [NSRunningApplication runningApplicationWithProcessIdentifier:self.gameTask.processIdentifier];
    if ([game activateFromApplication:NSRunningApplication.currentApplication options:0]) return YES;
  }
  if (!_preparing) [self.window makeKeyAndOrderFront:nil];
  return YES;
}
@end

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      bool prepareOnly = false, direct = false, forwarding = false;
      fs::path iso, data;
      std::vector<std::string> extra;
      for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (forwarding) extra.push_back(arg);
        else if (arg == "--") forwarding = true;
        else if (arg == "--prepare-only") prepareOnly = true;
        else if (arg == "--play") direct = true;
        else if (arg == "--data-root") {
          if (i + 1 >= argc) { fprintf(stderr, "--data-root requires a folder.\n"); return 2; }
          data = fs::absolute(argv[++i]);
        }
        else if (arg.rfind("--data-root=", 0) == 0) data = fs::absolute(arg.substr(12));
        else if (arg == "--help") {
          printf("Usage: rr6-launcher [image.iso] [--data-root folder] [--play] [-- game options]\n"
                 "Default: show the macOS settings launcher. --play skips it.\n"
                 "Headless import: rr6-launcher --prepare-only image.iso [--data-root folder]\n");
          return 0;
        } else if (!arg.empty() && arg.front() != '-' && iso.empty()) iso = fs::absolute(arg);
        else { extra.push_back(arg); forwarding = true; }
      }
      if (prepareOnly && iso.empty()) { fprintf(stderr, "--prepare-only requires an ISO path.\n"); return 2; }
      if (data.empty()) {
        NSURL* support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
        data = fs::path(support.fileSystemRepresentation) / "Ridge Racer 6";
      }
      if (prepareOnly) {
        if (!Prepare(iso, data / "game", true)) return 1;
        printf("Game files ready: %s\n", (data / "game").c_str()); return 0;
      }
      [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      [NSApp activateIgnoringOtherApps:YES];
      for (const auto* name : {"logs", "DLC", "userdata"}) fs::create_directories(data / name);
      NSBundle* bundle = NSBundle.mainBundle;
      const fs::path contents = fs::path(bundle.bundleURL.fileSystemRepresentation) / "Contents";
      if (!fs::exists(data / "rr6_recomp.toml"))
        fs::copy_file(contents / "Resources/rr6_recomp.toml", data / "rr6_recomp.toml");
      if (direct) {
        if (!Ready(data) || !iso.empty()) {
          if (iso.empty()) iso = BesideApp(bundle);
          if (iso.empty()) iso = ChooseImage();
          if (iso.empty()) return 0;
          if (!Prepare(iso, data / "game", false)) return 1;
        }
        setenv("RR6_MACOS_RELEASE_USER_ROOT", data.c_str(), 1);
        std::vector<std::string> args{(contents / "MacOS/rr6_recomp").string()};
        const auto gameArgs = GameArguments(data, bundle, extra);
        args.insert(args.end(), gameArgs.begin(), gameArgs.end());
        std::vector<char*> pointers;
        for (auto& arg : args) pointers.push_back(arg.data());
        pointers.push_back(nullptr); execv(pointers[0], pointers.data());
        Error(@"The bundled game executable could not start.", false); return 1;
      }
      NSMenu* menu = [NSMenu new];
      NSMenuItem* appMenu = [NSMenuItem new]; [menu addItem:appMenu];
      appMenu.submenu = [NSMenu new];
      [appMenu.submenu addItemWithTitle:@"Quit Ridge Racer 6" action:@selector(terminate:) keyEquivalent:@"q"];
      NSMenuItem* editMenu = [NSMenuItem new]; editMenu.title = @"Edit"; [menu addItem:editMenu];
      editMenu.submenu = [[NSMenu alloc] initWithTitle:@"Edit"];
      for (NSArray* entry in @[@[@"Cut", @"cut:", @"x"], @[@"Copy", @"copy:", @"c"],
                              @[@"Paste", @"paste:", @"v"], @[@"Select All", @"selectAll:", @"a"]])
        [editMenu.submenu addItemWithTitle:entry[0] action:NSSelectorFromString(entry[1]) keyEquivalent:entry[2]];
      NSApp.mainMenu = menu;
      __attribute__((objc_precise_lifetime)) Launcher* launcher = [[Launcher alloc] initWithData:data image:iso extra:extra];
      NSApp.delegate = launcher;
      [launcher show]; [NSApp run];
      return 0;
    } catch (const std::exception& e) {
      Error([@"Unable to access game files: " stringByAppendingString:Text(e.what())], NSApp == nil);
      return 1;
    }
  }
}
