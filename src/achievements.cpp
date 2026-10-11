// Achievements, as on the Xbox 360: a pop-up with a sound when one is
// unlocked, and a list to see how far along you are.
//
// What was there already. The SDK reads the game's 36 achievements (names,
// descriptions, icons, 1000 gamerscore) out of the game's own program file,
// records an unlock when the game reports one, keeps the unlocks on disk, and
// has a pop-up and a list window (F7) of its own. This file replaces the two
// windows and adds what was missing:
//
//   The pop-up   at the bottom of the screen: the achievement's icon in a
//                circle, "Achievement unlocked", the gamerscore and the name,
//                and a sound (unlock_sound.cpp).
//   The list     opened with the achievements key (F7), or with Y from the
//                quit question, which is the way to it on a controller and on
//                a Steam Deck. Scrolls with the D-pad, the stick, the arrow
//                and page keys, and the mouse wheel; B or Esc closes it.
//   Online only  15 of the 36 need Xbox Live play, which this version does not
//                have: the online-battle ones, and every one that needs cars
//                only given for online battles (the machine collections and
//                the five messages). They are listed apart, and progress is
//                counted against the 21 that can be earned (565 of the 1000
//                gamerscore). Which ones these are comes from players' guides,
//                not from the game's data; nothing stops one from unlocking.
//   Secret ones  as on the console, an achievement the game marks as hidden
//                shows no description until it is unlocked.
//   For the      the list with its unlock times, and the icons as PNG files,
//   launcher     are written to <user data>/achievements/ at start-up and on
//                every unlock; the launcher's Achievements page reads them.
//                The icons are copied out of the game's program as it sits in
//                memory, on the player's PC.
//
// rr6_preview_achievement = N shows the pop-up for achievement N a few seconds
// after the game starts, without unlocking anything: to see and hear it.

#include "achievements.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/ui/overlay/achievement_notification.h>

#include "overlay_input.h"
#include "unlock_sound.h"

REXCVAR_DEFINE_INT32(rr6_preview_achievement, 0, "RR6",
                     "Diagnostic: show the unlock pop-up for the achievement with this number "
                     "(1 to 36) a few seconds after the game starts. Nothing is unlocked.");

REXCVAR_DECLARE(std::string, rr6_quit_key);
REXCVAR_DECLARE(std::string, rr6_achievements_key);

namespace rr6 {
namespace {

using rex::system::AchievementEvent;
using rex::system::AchievementInfo;
using rex::system::AchievementManager;

// The achievements that need Xbox Live play (see the top of this file).
constexpr uint32_t kOnlineOnly[] = {2, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 34, 36};
// In the game's data, an achievement without this flag is hidden until earned.
constexpr uint32_t kFlagShowUnachieved = 0x8;
// The game's title resource (names, descriptions, icons) in the loaded
// program: its address and size from the executable's header.
constexpr uint32_t kTitleDataAddress = 0x82560000;
constexpr uint32_t kTitleDataSize = 0x3AA6C;

const ImU32 kLime = IM_COL32(0xBF, 0xF5, 0x2E, 255);
const ImU32 kInk = IM_COL32(22, 25, 29, 255);
const ImU32 kRow = IM_COL32(34, 39, 44, 255);
const ImU32 kWhite = IM_COL32(255, 255, 255, 255);
const ImU32 kQuiet = IM_COL32(158, 168, 173, 255);
const ImU32 kFaint = IM_COL32(104, 113, 119, 255);

bool IsOnlineOnly(uint32_t id) {
  return std::find(std::begin(kOnlineOnly), std::end(kOnlineOnly), id) != std::end(kOnlineOnly);
}

double Seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

ImU32 WithAlpha(ImU32 colour, float alpha) {
  const ImU32 a = static_cast<ImU32>(((colour >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
  return (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

// An unlock time (100 ns units since 1601) as a local date, "2026-10-06".
std::string DateOf(uint64_t filetime) {
  if (filetime < 116444736000000000ull) {
    return {};
  }
  const std::time_t seconds = static_cast<std::time_t>((filetime - 116444736000000000ull) / 10000000ull);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
  return text;
}

// ---------------------------------------------------------------- shared state

// UI thread only.
std::function<AppParts()> g_parts;
std::unique_ptr<rex::ui::AchievementIconCache> g_icons;
class ListDialog;
ListDialog* g_list = nullptr;
bool g_listening = false;        // the unlock callback has been registered
double g_first_tick = 0;         // when the pop-up dialog drew for the first time
bool g_previewed = false;
bool g_logged_idle = false;      // the pop-up's first rest has been logged

// Set from any thread: an unlock happened, the launcher's copy is out of date.
std::atomic<bool> g_export_needed{true};

AppParts Parts() { return g_parts ? g_parts() : AppParts{}; }

AchievementManager* Manager() {
  const AppParts parts = Parts();
  if (!parts.runtime || !parts.runtime->kernel_state()) {
    return nullptr;
  }
  return &parts.runtime->kernel_state()->achievements();
}

rex::ui::ImmediateTexture* IconOf(const AchievementInfo& info) {
  const AppParts parts = Parts();
  if (!parts.immediate || !parts.runtime) {
    return nullptr;
  }
  if (!g_icons) {
    g_icons = std::make_unique<rex::ui::AchievementIconCache>(parts.immediate, parts.runtime);
  }
  return g_icons->GetIcon(info);
}

ImTextureRef TextureRef(rex::ui::ImmediateTexture* texture) {
  return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)));
}

struct Entry {
  AchievementInfo info;
  uint64_t unlocked_at = 0;  // 0 = locked
  bool online_only = false;
  bool secret = false;       // hidden by the game until it is unlocked
};

// The game's achievements in the game's order, those that can be earned first.
std::vector<Entry> Snapshot() {
  std::vector<Entry> entries;
  AchievementManager* manager = Manager();
  if (!manager) {
    return entries;
  }
  for (AchievementInfo& info : manager->ListAchievements()) {
    Entry entry;
    entry.unlocked_at = manager->GetUnlockTime(info.id);
    if (entry.unlocked_at == 0 && manager->IsUnlocked(info.id)) {
      entry.unlocked_at = 1;  // unlocked, time unknown
    }
    entry.online_only = IsOnlineOnly(info.id);
    entry.secret = (info.flags & kFlagShowUnachieved) == 0;
    entry.info = std::move(info);
    entries.push_back(std::move(entry));
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry& a, const Entry& b) { return a.online_only < b.online_only; });
  return entries;
}

// ------------------------------------------------------- the launcher's copy

uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

// Writes the icons the game's program carries (PNG files, numbered by image
// id) into `folder`, skipping those already there.
void ExportIcons(rex::Runtime* runtime, const std::filesystem::path& folder) {
  if (!runtime->memory()) {
    return;
  }
  const uint8_t* data = runtime->memory()->TranslateVirtual<const uint8_t*>(kTitleDataAddress);
  if (!data || std::memcmp(data, "XDBF", 4) != 0) {
    REXLOG_INFO("[achievements] the game's title data is not where it is expected; no icons copied");
    return;
  }
  const uint32_t table_length = Be32(data + 8);
  const uint32_t count = Be32(data + 12);
  const uint32_t free_length = Be32(data + 16);
  const uint64_t base = 24ull + uint64_t(table_length) * 18 + uint64_t(free_length) * 8;
  if (count > table_length || base >= kTitleDataSize) {
    return;
  }
  std::error_code ec;
  int written = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* entry = data + 24 + i * 18;
    const uint16_t space = Be16(entry);
    const uint64_t id = (uint64_t(Be32(entry + 2)) << 32) | Be32(entry + 6);
    const uint32_t offset = Be32(entry + 10);
    const uint32_t length = Be32(entry + 14);
    if (space != 2 || id > 0xFFFFFFFFull || length < 8 || base + offset + length > kTitleDataSize) {
      continue;  // 2 = images
    }
    const uint8_t* image = data + base + offset;
    if (std::memcmp(image, "\x89PNG\r\n\x1a\n", 8) != 0) {
      continue;
    }
    const std::filesystem::path file = folder / (std::to_string(id) + ".png");
    if (std::filesystem::exists(file, ec) && std::filesystem::file_size(file, ec) == length) {
      continue;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(image), length);
    if (out) {
      ++written;
    }
  }
  if (written) {
    REXLOG_INFO("[achievements] {} icons written to {}", written, folder.string());
  }
}

std::string OneLine(std::string text) {
  for (char& c : text) {
    if (c == '\t' || c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  return text;
}

// Writes <user data>/achievements/list.txt: one line per achievement, fields
// separated by tabs, for the launcher's Achievements page.
void ExportForLauncher() {
  const AppParts parts = Parts();
  if (!parts.runtime) {
    return;
  }
  const std::vector<Entry> entries = Snapshot();
  if (entries.empty() || parts.runtime->user_data_root().empty()) {
    return;
  }
  const std::filesystem::path folder = parts.runtime->user_data_root() / "achievements";
  std::error_code ec;
  std::filesystem::create_directories(folder / "icons", ec);
  ExportIcons(parts.runtime, folder / "icons");

  const std::filesystem::path list = folder / "list.txt";
  const std::filesystem::path partial = folder / "list.txt.part";
  {
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    out << "RR6-ACHIEVEMENTS 1\n"
        << "# id, gamerscore, online only, secret, unlock time (0 = locked), image, name, "
           "description, how to earn it\n";
    for (const Entry& e : entries) {
      out << e.info.id << '\t' << e.info.gamerscore << '\t' << (e.online_only ? 1 : 0) << '\t'
          << (e.secret ? 1 : 0) << '\t' << e.unlocked_at << '\t' << e.info.image_id << '\t'
          << OneLine(e.info.label) << '\t' << OneLine(e.info.description) << '\t'
          << OneLine(e.info.unachieved_description) << '\n';
    }
    if (!out) {
      return;
    }
  }
  std::filesystem::rename(partial, list, ec);
  if (ec) {
    std::filesystem::remove(list, ec);
    std::filesystem::rename(partial, list, ec);
  }
}

// ------------------------------------------------------------------ drawing

// Text at a given size, in the overlay's font. `wrap` > 0 breaks lines there.
void Text(ImDrawList* list, float size, ImVec2 at, ImU32 colour, const char* text, float wrap = 0.0f) {
  ImGui::PushFont(nullptr, size);
  list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, colour, text, nullptr, wrap);
  ImGui::PopFont();
}

ImVec2 Measure(float size, const char* text) {
  ImGui::PushFont(nullptr, size);
  const ImVec2 result = ImGui::CalcTextSize(text);
  ImGui::PopFont();
  return result;
}

// The achievement's icon, or a plain tile when there is none to show.
void Icon(ImDrawList* list, const Entry& entry, ImVec2 at, float size, float rounding, float alpha,
          float u) {
  const ImVec2 end(at.x + size, at.y + size);
  const bool unlocked = entry.unlocked_at != 0;
  rex::ui::ImmediateTexture* texture =
      (unlocked || !entry.secret) ? IconOf(entry.info) : nullptr;
  if (texture) {
    const ImU32 tint = unlocked ? WithAlpha(kWhite, alpha) : IM_COL32(110, 110, 110, int(255 * alpha));
    list->AddImageRounded(TextureRef(texture), at, end, ImVec2(0, 0), ImVec2(1, 1), tint, rounding);
  } else {
    list->AddRectFilled(at, end, WithAlpha(IM_COL32(52, 58, 64, 255), alpha), rounding);
    const char* mark = entry.secret && !unlocked ? "?" : "G";
    const ImVec2 mark_size = Measure(size * 0.5f, mark);
    Text(list, size * 0.5f, ImVec2(at.x + (size - mark_size.x) * 0.5f, at.y + (size - mark_size.y) * 0.5f),
         WithAlpha(kQuiet, alpha), mark);
  }
  (void)u;
}

// --------------------------------------------------------------- the pop-up

// While any overlay window is registered with the SDK's overlay drawer, the
// SDK draws every frame on the UI thread, overlay on top, and asks for the next
// paint as soon as one is done; with none, the game's frames go to the screen
// straight from the thread that finishes them, which costs less. The pop-up is
// only on screen for a few seconds after an unlock, so it takes itself off the
// drawer while it has nothing to show and puts itself back when one comes in.
// It stays on at start-up until its start-up jobs (Tick) are done.
class Popup;
Popup* g_popup = nullptr;  // UI thread

class Popup : public rex::ui::AchievementNotificationDialog {
 public:
  explicit Popup(rex::ui::ImGuiDrawer* drawer)
      : rex::ui::AchievementNotificationDialog(drawer), drawer_(drawer) {
    g_popup = this;
  }
  ~Popup() override {
    if (g_popup == this) {
      g_popup = nullptr;
    }
  }

  // Any thread.
  void Push(const AchievementEvent& event) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      waiting_.push_back(event);
    }
    RunOnUiThreadLater([] {
      if (g_popup) {
        g_popup->Attach();
      }
    });
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    Tick();
    const double now = Seconds();
    if (!showing_) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!waiting_.empty()) {
        current_ = waiting_.front();
        waiting_.pop_front();
        showing_ = true;
        shown_at_ = now;
        PlayUnlockSound();
      } else if (StartupDone()) {
        Detach();
        return;
      }
    }
    if (!showing_) {
      return;
    }
    constexpr double kArrive = 0.30, kStay = 5.0, kLeave = 0.45;
    const double age = now - shown_at_;
    if (age >= kArrive + kStay + kLeave) {
      showing_ = false;
      return;
    }
    float alpha = 1.0f, slide = 0.0f;
    if (age < kArrive) {
      const float t = static_cast<float>(age / kArrive);
      alpha = t;
      slide = (1.0f - t) * (1.0f - t);  // comes up from a little lower, slowing down
    } else if (age > kArrive + kStay) {
      alpha = 1.0f - static_cast<float>((age - kArrive - kStay) / kLeave);
    }

    const ImVec2 screen = io.DisplaySize;
    const float u = (screen.y > 480.0f ? screen.y : 480.0f) / 720.0f;
    const AchievementInfo& info = current_.achievement;
    Entry entry;
    entry.info = info;
    entry.unlocked_at = 1;

    char score[32];
    std::snprintf(score, sizeof(score), "%uG", info.gamerscore);
    const char* heading = "Achievement unlocked";
    // Not "small": the Windows headers define that word as a macro.
    const float small_px = 16 * u, large_px = 22 * u;
    const ImVec2 heading_size = Measure(small_px, heading);
    const ImVec2 score_size = Measure(large_px, score);
    const ImVec2 name_size = Measure(large_px, info.label.c_str());
    const float height = 80 * u, icon = 60 * u, edge = (height - icon) * 0.5f;
    const float text_width = std::max(heading_size.x, score_size.x + 12 * u + name_size.x);
    const float width = std::max(380 * u, edge + icon + 18 * u + text_width + 36 * u);
    const float x = (screen.x - width) * 0.5f;
    const float y = screen.y - 92 * u - height + slide * 36 * u;

    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 a(x, y), b(x + width, y + height);
    list->AddRectFilled(ImVec2(a.x, a.y + 4 * u), ImVec2(b.x, b.y + 4 * u),
                        IM_COL32(0, 0, 0, int(90 * alpha)), height * 0.5f);
    list->AddRectFilled(a, b, WithAlpha(IM_COL32(22, 25, 29, 240), alpha), height * 0.5f);
    list->AddRect(a, b, WithAlpha(IM_COL32(0xBF, 0xF5, 0x2E, 170), alpha), height * 0.5f, 0, 2 * u);

    const ImVec2 icon_at(x + edge, y + edge);
    Icon(list, entry, icon_at, icon, icon * 0.5f, alpha, u);
    list->AddCircle(ImVec2(icon_at.x + icon * 0.5f, icon_at.y + icon * 0.5f), icon * 0.5f + 1.5f * u,
                    WithAlpha(kLime, alpha), 48, 2 * u);

    const float tx = icon_at.x + icon + 18 * u;
    const float block = heading_size.y + 5 * u + score_size.y;
    const float ty = y + (height - block) * 0.5f;
    Text(list, small_px, ImVec2(tx, ty), WithAlpha(kQuiet, alpha), heading);
    Text(list, large_px, ImVec2(tx, ty + heading_size.y + 5 * u), WithAlpha(kLime, alpha), score);
    Text(list, large_px, ImVec2(tx + score_size.x + 12 * u, ty + heading_size.y + 5 * u),
         WithAlpha(kWhite, alpha), info.label.c_str());
  }

 private:
  // UI thread. Adding twice or removing twice does nothing; removing from
  // inside OnDraw is allowed (the drawer finishes the frame first).
  void Attach() {
    if (!attached_) {
      attached_ = true;
      drawer_->AddDialog(this);
    }
  }
  void Detach() {
    if (attached_) {
      attached_ = false;
      drawer_->RemoveDialog(this);
      if (!g_logged_idle) {
        g_logged_idle = true;
        REXLOG_INFO("[achievements] pop-up idle: off the overlay until the next unlock");
      }
    }
  }

  // The start-up jobs in Tick are done: the unlock listener is registered,
  // the launcher's copy is written, and any preview has been queued.
  bool StartupDone() const {
    return g_listening && !g_export_needed.load() &&
           (REXCVAR_GET(rr6_preview_achievement) <= 0 || g_previewed);
  }

  // Things that need doing now and then: every frame while the pop-up is on
  // the drawer, which it is at start-up and after every unlock (an unlock is
  // when the launcher's copy goes out of date).
  void Tick() {
    AchievementManager* manager = Manager();
    if (!manager) {
      return;
    }
    const double now = Seconds();
    if (g_first_tick == 0) {
      g_first_tick = now;
    }
    if (!g_listening) {
      g_listening = true;
      manager->RegisterUnlockCallback([](const AchievementEvent&) {
        g_export_needed = true;
        // Silent unlocks still need the launcher export refreshed, even when
        // they do not dispatch a notification that would reattach the popup.
        RunOnUiThreadLater([] {
          if (g_popup) {
            g_popup->Attach();
          }
        });
      });
    }
    if (g_export_needed.exchange(false)) {
      ExportForLauncher();
    }
    const int preview = REXCVAR_GET(rr6_preview_achievement);
    if (preview > 0 && !g_previewed && now - g_first_tick > 6.0) {
      g_previewed = true;
      if (auto info = manager->FindAchievement(static_cast<uint32_t>(preview))) {
        REXLOG_INFO("[achievements] showing the pop-up for {} as a preview", info->label);
        AchievementEvent event;
        event.achievement = *info;
        Push(event);
      }
    }
  }

  rex::ui::ImGuiDrawer* drawer_;
  bool attached_ = true;  // the base class adds the dialog to the drawer
  std::mutex mutex_;
  std::deque<AchievementEvent> waiting_;
  AchievementEvent current_;
  bool showing_ = false;
  double shown_at_ = 0;
};

// ----------------------------------------------------------------- the list

class ListDialog : public rex::ui::ImGuiDialog {
 public:
  explicit ListDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) { OverlayOpened(); }
  ~ListDialog() override {
    OverlayClosed();
    g_list = nullptr;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    const double now = Seconds();
    if (entries_.empty() || now - refreshed_at_ > 0.5) {
      entries_ = Snapshot();
      refreshed_at_ = now;
    }
    const ImVec2 screen = io.DisplaySize;
    const float u = (screen.y > 480.0f ? screen.y : 480.0f) / 720.0f;
    const float row_height = 84 * u;

    // Input: rows to move by, or pages, or to an end; and closing.
    const bool settled = OverlayInputSettled();
    float rows = 0;
    bool close = false, to_top = false, to_end = false;
    const uint32_t pressed = TakePadPresses();
    const uint32_t held = PadHeld();
    const float page = 4;
    if (settled) {
      if (pressed & (pad::kB | pad::kY | pad::kBack)) close = true;
      if (pressed & pad::kUp) rows -= 1;
      if (pressed & pad::kDown) rows += 1;
      if (pressed & (pad::kLeft | pad::kLB)) rows -= page;
      if (pressed & (pad::kRight | pad::kRB)) rows += page;
      // A direction held down keeps scrolling after a short wait.
      const int direction = (held & pad::kDown) ? 1 : (held & pad::kUp) ? -1 : 0;
      if (direction != held_direction_) {
        held_direction_ = direction;
        held_since_ = now;
        repeated_at_ = now;
      } else if (direction != 0 && now - held_since_ > 0.40 && now - repeated_at_ > 0.07) {
        repeated_at_ = now;
        rows += static_cast<float>(direction);
      }
    }
    const rex::ui::VirtualKey quit_key = rex::ui::ParseVirtualKey(REXCVAR_GET(rr6_quit_key));
    const rex::ui::VirtualKey list_key =
        rex::ui::ParseVirtualKey(REXCVAR_GET(rr6_achievements_key));
    for (const KeyPress& press : TakeKeyPresses()) {
      using rex::ui::VirtualKey;
      if (!settled) {
        continue;
      }
      if (press.key == VirtualKey::kUp || press.key == VirtualKey::kDown) {
        // A held arrow key scrolls at the same pace as a held D-pad, however
        // fast the keyboard repeats.
        if (press.repeat && now - key_repeated_at_ < 0.07) {
          continue;
        }
        key_repeated_at_ = now;
        rows += press.key == VirtualKey::kDown ? 1.0f : -1.0f;
      }
      else if (press.key == VirtualKey::kPrior || press.key == VirtualKey::kLeft) rows -= page;
      else if (press.key == VirtualKey::kNext || press.key == VirtualKey::kRight) rows += page;
      else if (press.key == VirtualKey::kHome) to_top = true;
      else if (press.key == VirtualKey::kEnd) to_end = true;
      else if (!press.repeat && (press.key == quit_key || press.key == list_key ||
                                 press.key == VirtualKey::kBack || press.key == VirtualKey::kY)) {
        close = true;
      }
    }

    // Totals: progress is counted against what can be earned here.
    int earnable = 0, earned = 0, online = 0, online_earned = 0;
    uint32_t score_possible = 0, score = 0;
    for (const Entry& e : entries_) {
      if (e.online_only) {
        ++online;
        online_earned += e.unlocked_at != 0;
      } else {
        ++earnable;
        score_possible += e.info.gamerscore;
        if (e.unlocked_at != 0) {
          ++earned;
          score += e.info.gamerscore;
        }
      }
    }

    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(8, 10, 12, 175));

    const ImVec2 size(std::min(820 * u, screen.x - 40 * u), std::min(620 * u, screen.y - 40 * u));
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 12 * u);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.086f, 0.098f, 0.114f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.30f, 0.34f, 0.37f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.40f, 0.45f, 0.48f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(0.749f, 0.961f, 0.180f, 1));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##rr6_achievements", nullptr, flags)) {
      ImDrawList* list = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetWindowPos();
      const float margin = 30 * u;
      const float inner = size.x - 2 * margin;

      // Heading: title, totals, progress.
      Text(list, 30 * u, ImVec2(origin.x + margin, origin.y + 22 * u), kWhite, "Achievements");
      list->AddRectFilled(ImVec2(origin.x + margin, origin.y + 62 * u),
                          ImVec2(origin.x + margin + 96 * u, origin.y + 66 * u), kLime);
      char totals[96];
      std::snprintf(totals, sizeof(totals), "%d of %d earned", earned, earnable);
      char points[64];
      std::snprintf(points, sizeof(points), "%uG of %uG", score, score_possible);
      const ImVec2 totals_size = Measure(20 * u, totals);
      const ImVec2 points_size = Measure(17 * u, points);
      Text(list, 20 * u, ImVec2(origin.x + size.x - margin - totals_size.x, origin.y + 24 * u), kWhite, totals);
      Text(list, 17 * u, ImVec2(origin.x + size.x - margin - points_size.x, origin.y + 50 * u), kLime, points);
      const float bar_y = origin.y + 84 * u;
      list->AddRectFilled(ImVec2(origin.x + margin, bar_y), ImVec2(origin.x + margin + inner, bar_y + 6 * u),
                          IM_COL32(48, 54, 60, 255), 3 * u);
      if (earnable > 0 && earned > 0) {
        list->AddRectFilled(ImVec2(origin.x + margin, bar_y),
                            ImVec2(origin.x + margin + inner * earned / earnable, bar_y + 6 * u), kLime, 3 * u);
      }

      // The rows, in a part of the window that scrolls.
      const float top = 104 * u, bottom = 46 * u;
      ImGui::SetCursorPos(ImVec2(margin, top));
      if (ImGui::BeginChild("##rows", ImVec2(inner + 18 * u, size.y - top - bottom), 0, ImGuiWindowFlags_NoNav)) {
        ImDrawList* rows_list = ImGui::GetWindowDrawList();
        const float view = ImGui::GetWindowHeight();
        const float content = entries_.size() * row_height + (online > 0 ? 58 * u : 0);
        const float most = std::max(0.0f, content - view);
        // The mouse wheel and the scroll bar move the view themselves: follow them.
        const float actual = ImGui::GetScrollY();
        if (std::fabs(actual - last_set_) > 1.5f) {
          scroll_ = target_ = actual;
        }
        target_ += rows * row_height;
        if (to_top) target_ = 0;
        if (to_end) target_ = most;
        target_ = std::clamp(target_, 0.0f, most);
        scroll_ += (target_ - scroll_) * std::min(1.0f, io.DeltaTime * 16.0f);
        if (std::fabs(target_ - scroll_) < 0.5f) {
          scroll_ = target_;
        }
        ImGui::SetScrollY(scroll_);
        last_set_ = std::floor(scroll_);

        const ImVec2 clip_min = ImGui::GetWindowPos();
        bool online_heading_done = false;
        for (const Entry& e : entries_) {
          if (e.online_only && !online_heading_done) {
            online_heading_done = true;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (at.y + 58 * u > clip_min.y && at.y < clip_min.y + view) {
              char heading[128];
              std::snprintf(heading, sizeof(heading), "Need online play, which this version does not have (%d)", online);
              Text(rows_list, 17 * u, ImVec2(at.x, at.y + 24 * u), kQuiet, heading);
            }
            ImGui::Dummy(ImVec2(inner, 58 * u));
          }
          const ImVec2 at = ImGui::GetCursorScreenPos();
          if (at.y + row_height > clip_min.y && at.y < clip_min.y + view) {
            DrawRow(rows_list, e, at, inner, row_height, u);
          }
          ImGui::Dummy(ImVec2(inner, row_height));
        }
        (void)online_earned;
      }
      ImGui::EndChild();

      const char* hint = "Up / Down: scroll        B or Esc: back";
      Text(list, 15 * u, ImVec2(origin.x + margin, origin.y + size.y - 31 * u), kFaint, hint);
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(5);

    if (close) {
      Close();  // deletes this dialog once the drawing is over
    }
  }

 private:
  static void DrawRow(ImDrawList* list, const Entry& e, ImVec2 at, float width, float height, float u) {
    const bool unlocked = e.unlocked_at != 0;
    const ImVec2 a(at.x, at.y + 4 * u), b(at.x + width, at.y + height - 4 * u);
    list->AddRectFilled(a, b, unlocked ? IM_COL32(40, 50, 34, 255) : kRow, 8 * u);
    if (unlocked) {
      list->AddRectFilled(a, ImVec2(a.x + 4 * u, b.y), kLime, 8 * u, ImDrawFlags_RoundCornersLeft);
    }
    const float icon = 56 * u;
    const ImVec2 icon_at(a.x + 16 * u, a.y + (b.y - a.y - icon) * 0.5f);
    Icon(list, e, icon_at, icon, 8 * u, 1.0f, u);

    // Right side: the score, and where it stands.
    char score[32];
    std::snprintf(score, sizeof(score), "%uG", e.info.gamerscore);
    const ImVec2 score_size = Measure(20 * u, score);
    const float right = b.x - 18 * u;
    Text(list, 20 * u, ImVec2(right - score_size.x, a.y + 12 * u), unlocked ? kLime : kQuiet, score);
    std::string state;
    if (unlocked) {
      const std::string date = DateOf(e.unlocked_at);
      state = date.empty() ? "Unlocked" : "Unlocked " + date;
    } else if (e.online_only) {
      state = "Online only";
    }
    float state_width = 0;
    if (!state.empty()) {
      const ImVec2 state_size = Measure(14 * u, state.c_str());
      state_width = state_size.x;
      Text(list, 14 * u, ImVec2(right - state_size.x, a.y + 42 * u), unlocked ? kLime : kFaint, state.c_str());
    }

    // Name and description. A secret one says nothing until it is unlocked.
    const float text_x = icon_at.x + icon + 16 * u;
    const float text_width = right - std::max(score_size.x, state_width) - 16 * u - text_x;
    const bool hidden = e.secret && !unlocked;
    const char* name = hidden ? "Secret achievement" : e.info.label.c_str();
    const std::string& description =
        hidden ? kSecretText : (unlocked || e.info.unachieved_description.empty())
                                   ? e.info.description
                                   : e.info.unachieved_description;
    list->PushClipRect(ImVec2(text_x, a.y), ImVec2(text_x + text_width, b.y), true);
    Text(list, 20 * u, ImVec2(text_x, a.y + 10 * u), unlocked ? kWhite : IM_COL32(214, 219, 222, 255), name);
    Text(list, 14.5f * u, ImVec2(text_x, a.y + 37 * u), unlocked ? IM_COL32(190, 200, 186, 255) : kQuiet,
         description.c_str(), text_width);
    list->PopClipRect();
  }

  static const std::string kSecretText;

  std::vector<Entry> entries_;
  double refreshed_at_ = 0;
  float scroll_ = 0, target_ = 0, last_set_ = 0;
  int held_direction_ = 0;
  double held_since_ = 0, repeated_at_ = 0, key_repeated_at_ = 0;
};

const std::string ListDialog::kSecretText = "Keep playing to find out what this one is.";

}  // namespace

void InstallAchievements(std::function<AppParts()> parts) {
  g_parts = std::move(parts);
  SetAchievementsRequestHandler([] { ShowAchievementList(); });
}

void RemoveAchievements() {
  if (g_list) {
    delete g_list;
  }
  g_icons.reset();
  g_parts = nullptr;
}

std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementPopup(
    rex::ui::ImGuiDrawer* drawer) {
  if (!drawer) {
    return nullptr;
  }
  return std::make_unique<Popup>(drawer);
}

void ShowAchievementList() {
  const AppParts parts = Parts();
  if (!parts.drawer || g_list || !Manager()) {
    return;
  }
  g_list = new ListDialog(parts.drawer);
  REXLOG_INFO("[achievements] list opened");
}

}  // namespace rr6
