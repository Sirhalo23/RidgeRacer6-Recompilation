// Keyboard and controller input for our own overlays: the quit question
// (quit_prompt.cpp) and the achievements list (achievements.cpp).
//
// Keyboard. A listener of our own sits ahead of the SDK's overlay and of its
// key bindings, because both of those take the repeats of a held key for new
// presses: Esc held a moment too long would open the question and take it away
// again. With no overlay open it watches for two keys, the quit key
// (rr6_quit_key) and the achievements key (rr6_achievements_key). With an
// overlay open it hands the keys an overlay can use (Esc, Enter, arrows, Page
// Up/Down, Home, End, Y, Backspace and those two) to that overlay, each marked
// as a fresh press or a repeat, and keeps them from everything else.
//
// Controller. The game reads the controller through one function
// (XInputGetState, see input_fixes.cpp), which passes every state through
// OverlaySeesPad. Back + Start held for a second asks for the menu. While an
// overlay is open, button presses are collected for it, and the game is given
// an idle controller: until the overlay is gone and the buttons that were down
// have been let go, so that "B: back" is not also a "cancel" in the game's
// menu underneath. Keys that the SDK turns into controller buttons come the
// same way.

#include "overlay_input.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

REXCVAR_DEFINE_STRING(rr6_quit_key, "Escape", "RR6",
                      "The key that asks whether to quit the game, named as in the key bindings "
                      "(for example Escape or F10).");

REXCVAR_DEFINE_STRING(rr6_achievements_key, "F7", "RR6",
                      "The key that opens the achievements list.");

namespace rr6 {
namespace {

constexpr int64_t kHoldMs = 1000;   // how long Back + Start must be held
constexpr int64_t kSettleMs = 300;  // presses are ignored for this long after an overlay appears

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Shared between the UI thread and the game thread.
std::atomic<int> g_open{0};
std::atomic<int64_t> g_opened_ms{0};
std::atomic<uint32_t> g_pad_pressed{0};
std::atomic<uint32_t> g_pad_held{0};

// UI thread only.
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_context = nullptr;
std::function<void()> g_menu_handler;
std::function<void()> g_achievements_handler;
std::vector<KeyPress> g_keys;

bool UsedByOverlays(rex::ui::VirtualKey key) {
  using rex::ui::VirtualKey;
  switch (key) {
    case VirtualKey::kEscape:
    case VirtualKey::kReturn:
    case VirtualKey::kUp:
    case VirtualKey::kDown:
    case VirtualKey::kLeft:
    case VirtualKey::kRight:
    case VirtualKey::kPrior:
    case VirtualKey::kNext:
    case VirtualKey::kHome:
    case VirtualKey::kEnd:
    case VirtualKey::kBack:
    case VirtualKey::kY:
      return true;
    default:
      return false;
  }
}

class Keys : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    const rex::ui::VirtualKey key = e.virtual_key();
    const bool repeat = e.prev_state();  // the window marks a held key's repeats
    const bool quit_key = key == rex::ui::ParseVirtualKey(REXCVAR_GET(rr6_quit_key));
    const bool achievements_key =
        key == rex::ui::ParseVirtualKey(REXCVAR_GET(rr6_achievements_key));
    if (AnyOverlayOpen()) {
      if (quit_key || achievements_key || UsedByOverlays(key)) {
        if (g_keys.size() < 64) {
          g_keys.push_back({key, repeat});
        }
        e.set_handled(true);
      }
      return;
    }
    if (quit_key && g_menu_handler) {
      if (!repeat) {
        g_menu_handler();
      }
      e.set_handled(true);
    } else if (achievements_key && g_achievements_handler) {
      if (!repeat) {
        g_achievements_handler();
      }
      e.set_handled(true);
    }
  }
};
Keys g_listener;

struct Pad {
  uint32_t previous = 0;
  uint32_t held = 0;
  int64_t combo_since = 0;  // when Back + Start went down together; 0 = they are not
  bool combo_used = false;  // this hold has already asked for the menu
  bool hidden = false;      // the game is not shown this pad until its buttons are let go
};
std::mutex g_pad_mutex;
Pad g_pads[4];

}  // namespace

void InstallOverlayInput(rex::ui::Window* window, rex::ui::WindowedAppContext* context) {
  g_window = window;
  g_context = context;
  if (window) {
    window->AddInputListener(&g_listener, SIZE_MAX);  // ahead of every other listener
  }
}

void RemoveOverlayInput() {
  if (g_window) {
    g_window->RemoveInputListener(&g_listener);
  }
  g_window = nullptr;
  g_menu_handler = nullptr;
  g_achievements_handler = nullptr;
  g_open = 0;
}

void SetMenuRequestHandler(std::function<void()> handler) { g_menu_handler = std::move(handler); }

void SetAchievementsRequestHandler(std::function<void()> handler) {
  g_achievements_handler = std::move(handler);
}

void OverlayOpened() {
  if (g_open.fetch_add(1) == 0) {
    g_opened_ms = NowMs();
    g_pad_pressed = 0;
    g_keys.clear();
  }
}

void OverlayClosed() {
  if (g_open.load() > 0) {
    g_open.fetch_sub(1);
  }
}

bool AnyOverlayOpen() { return g_open.load() > 0; }

bool OverlayInputSettled() { return NowMs() - g_opened_ms.load() >= kSettleMs; }

uint32_t TakePadPresses() { return g_pad_pressed.exchange(0); }

uint32_t PadHeld() { return g_pad_held.load(); }

std::vector<KeyPress> TakeKeyPresses() {
  std::vector<KeyPress> keys;
  keys.swap(g_keys);
  return keys;
}

void RunOnUiThreadLater(std::function<void()> function) {
  if (g_context) {
    g_context->CallInUIThreadDeferred(std::move(function));
  }
}

rex::ui::Window* OverlayWindow() { return g_window; }

bool OverlaySeesPad(uint32_t user, uint32_t buttons, int stick_x, int stick_y) {
  if (user >= 4) {
    return false;
  }
  // The left stick pushed most of the way counts as the D-pad.
  uint32_t seen = buttons & 0xFFFFu;
  if (stick_y > 20000) seen |= pad::kUp;
  if (stick_y < -20000) seen |= pad::kDown;
  if (stick_x < -20000) seen |= pad::kLeft;
  if (stick_x > 20000) seen |= pad::kRight;

  std::lock_guard<std::mutex> lock(g_pad_mutex);
  Pad& p = g_pads[user];
  const uint32_t pressed = seen & ~p.previous;
  p.previous = seen;
  p.held = seen;
  const int64_t now = NowMs();

  if (AnyOverlayOpen()) {
    p.hidden = true;
    p.combo_since = 0;
    g_pad_held = g_pads[0].held | g_pads[1].held | g_pads[2].held | g_pads[3].held;
    if (OverlayInputSettled() && pressed) {
      g_pad_pressed.fetch_or(pressed);
    }
    return true;
  }

  if (p.hidden) {
    if (buttons != 0) {
      return true;
    }
    p.hidden = false;
  }

  const uint32_t combo = pad::kBack | pad::kStart;
  if ((buttons & combo) == combo) {
    if (p.combo_since == 0) {
      p.combo_since = now;
      p.combo_used = false;
    } else if (!p.combo_used && now - p.combo_since >= kHoldMs) {
      p.combo_used = true;
      if (g_context) {
        g_context->CallInUIThread([] {
          if (g_menu_handler && !AnyOverlayOpen()) {
            g_menu_handler();
          }
        });
      }
    }
  } else {
    p.combo_since = 0;
  }
  return false;
}

}  // namespace rr6
