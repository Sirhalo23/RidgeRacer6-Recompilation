// Leaving the game: Esc, or Back + Start held for a second, asks "Quit Ridge
// Racer 6?".
//
// The game is a console game and has no way out of its own; until now the only
// ways were Alt+F4 and the window's close button, and a full-screen window has
// no close button. Both still work, and still close at once.
//
//   keyboard    Esc asks; Enter quits, Esc again goes back to the game.
//   controller  Back + Start held for a second asks (Create/Share + Options on
//               a PlayStation pad); A quits, B goes back.
//   mouse       the two buttons can be clicked.
//
// The question is drawn by the SDK's overlay (ImGui), like its F3 and F4
// windows. The game goes on running behind it, and a question nobody answers
// for half a minute goes away again. While it is up, and until the
// buttons that answered it have been let go, the game is given an idle
// controller, so that "B: keep playing" is not also a "cancel" in the menu
// underneath. Keys count as well: the SDK turns them into controller buttons
// before the game reads them, so Space (A) and Backspace (B) answer too.
//
// Quitting asks the window to close, which is what Alt+F4 does; nothing new
// happens to the game on the way out.
//
// The keys are read by a listener of our own, ahead of the overlay and of the
// SDK's key bindings, because both of those take the repeats of a held key
// for new presses: Esc held a moment too long would ask and take the question
// away again, and Enter held as "Start" while the question appears would
// answer it.
//
// rr6_quit_prompt = false turns all of this off. rr6_quit_key names the key.

#include "quit_prompt.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

REXCVAR_DEFINE_BOOL(rr6_quit_prompt, true, "RR6",
                    "Esc, or Back + Start held for a second, asks whether to quit the game.");

REXCVAR_DEFINE_STRING(rr6_quit_key, "Escape", "RR6",
                      "The key that asks whether to quit the game, named as in the key bindings "
                      "(for example Escape or F10).");

namespace rr6 {
namespace {

constexpr uint32_t kStart = 0x0010, kBack = 0x0020, kA = 0x1000, kB = 0x2000;
constexpr int64_t kHoldMs = 1000;   // how long Back + Start must be held
constexpr int64_t kSettleMs = 300;  // answers are ignored for this long after the question appears
constexpr int64_t kGiveUpMs = 30000;  // an unanswered question goes away by itself

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Shared between the UI thread (the question) and the game thread (the pad).
constexpr int kQuit = 1, kGoBack = 2;
std::atomic<bool> g_open{false};
std::atomic<int64_t> g_opened_ms{0};
std::atomic<int> g_answer{0};  // kQuit or kGoBack, from a key or a controller button

// Nothing answers the question in its first moments, so that a button the
// player was pressing for the game does not.
bool Settled() { return NowMs() - g_opened_ms.load() >= kSettleMs; }

// UI thread only.
class QuitDialog;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_context = nullptr;
QuitDialog* g_dialog = nullptr;

class QuitDialog : public rex::ui::ImGuiDialog {
 public:
  explicit QuitDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {
    g_answer = 0;
    g_opened_ms = NowMs();
    g_open = true;
  }
  ~QuitDialog() override {
    g_open = false;
    g_dialog = nullptr;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    // Answers from the keyboard and the controller arrive through g_answer;
    // the two buttons below can also be clicked.
    const bool settled = Settled();
    const int answer = g_answer.exchange(0);
    bool quit = false;
    if (!quitting_) {
      quit = answer == kQuit;
      // Left unanswered, the question goes away: nobody is left looking at it
      // with no way to answer (a controller the question cannot hear, say).
      go_back_ = go_back_ || answer == kGoBack || NowMs() - g_opened_ms.load() >= kGiveUpMs;
    }

    // Sizes are given for a picture 720 lines high and grow with the window.
    const ImVec2 screen = io.DisplaySize;
    const float u = (screen.y > 480.0f ? screen.y : 480.0f) / 720.0f;
    const ImVec4 lime(0.749f, 0.961f, 0.180f, 1.0f);
    const ImVec4 lime_hot(0.816f, 0.980f, 0.345f, 1.0f);
    const ImVec4 ink(0.086f, 0.098f, 0.114f, 1.0f);
    const ImVec4 quiet(0.62f, 0.66f, 0.68f, 1.0f);

    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(8, 10, 12, 165));

    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * u, 24 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * u, 11 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16 * u, 12 * u));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(ink.x, ink.y, ink.z, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##rr6_quit", nullptr, flags)) {
      ImGui::PushFont(nullptr, 30 * u);
      ImGui::TextUnformatted("Quit Ridge Racer 6?");
      ImGui::PopFont();
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + 96 * u, at.y + 4 * u),
                                                ImGui::GetColorU32(lime));
      ImGui::Dummy(ImVec2(96 * u, 8 * u));

      ImGui::PushFont(nullptr, 19 * u);
      ImGui::PushStyleColor(ImGuiCol_Text, quiet);
      ImGui::TextUnformatted(quitting_ ? "Closing the game..."
                                       : "Anything since your last save will be lost.");
      ImGui::PopStyleColor();
      ImGui::Dummy(ImVec2(1, 6 * u));

      const float width = 250 * u;
      ImGui::BeginDisabled(quitting_);
      ImGui::BeginGroup();
      ImGui::PushStyleColor(ImGuiCol_Button, lime);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lime_hot);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, lime_hot);
      ImGui::PushStyleColor(ImGuiCol_Text, ink);
      if (ImGui::Button("Quit to desktop", ImVec2(width, 0)) && settled) {
        quit = true;
      }
      ImGui::PopStyleColor(4);
      Hint("Enter, or A on a controller", quiet, 15 * u);
      ImGui::EndGroup();

      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.23f, 0.26f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.31f, 0.34f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.31f, 0.34f, 1.0f));
      if (ImGui::Button("Keep playing", ImVec2(width, 0)) && settled) {
        go_back_ = true;
      }
      ImGui::PopStyleColor(3);
      Hint("Esc, or B on a controller", quiet, 15 * u);
      ImGui::EndGroup();
      ImGui::EndDisabled();
      ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(7);

    if (quit && !quitting_) {
      quitting_ = true;
      REXLOG_INFO("[quit] the player chose to quit; closing the window");
      // Not from inside the drawing: closing can destroy the window.
      if (g_context) {
        g_context->CallInUIThreadDeferred([] {
          if (g_window) {
            g_window->RequestClose();
          }
        });
      }
    } else if (go_back_ && !quitting_) {
      REXLOG_INFO("[quit] back to the game");
      Close();  // deletes this dialog once the drawing is over
    }
  }

 private:
  static void Hint(const char* text, const ImVec4& colour, float size) {
    ImGui::PushFont(nullptr, size);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  bool go_back_ = false;
  bool quitting_ = false;
};

void Ask() {
  if (!g_drawer || g_dialog || !REXCVAR_GET(rr6_quit_prompt)) {
    return;
  }
  g_dialog = new QuitDialog(g_drawer);
  REXLOG_INFO("[quit] asking whether to quit");
}

// The keyboard. A key counts as pressed when it goes down after having been
// up; the repeats a held key sends (marked by the window as "was already
// down") do not count.
class QuitKeys : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!REXCVAR_GET(rr6_quit_prompt)) {
      return;
    }
    const rex::ui::VirtualKey key = e.virtual_key();
    const bool pressed = !e.prev_state();
    const bool open = g_dialog != nullptr;
    if (key == rex::ui::ParseVirtualKey(REXCVAR_GET(rr6_quit_key))) {
      if (pressed && !open) {
        Ask();
      } else if (pressed && Settled()) {
        g_answer = kGoBack;
      }
      e.set_handled(true);
    } else if (key == rex::ui::VirtualKey::kReturn && open) {
      if (pressed && Settled()) {
        g_answer = kQuit;
      }
      e.set_handled(true);
    }
  }
};
QuitKeys g_keys;

struct Pad {
  uint32_t previous = 0;
  int64_t held_since = 0;  // when Back + Start went down together; 0 = they are not
  bool asked = false;      // this hold has already asked
  bool hidden = false;     // the game is not shown this pad until its buttons are let go
};
std::mutex g_pad_mutex;
Pad g_pads[4];

}  // namespace

void InstallQuitPrompt(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
                       rex::ui::WindowedAppContext* context) {
  g_drawer = drawer;
  g_window = window;
  g_context = context;
  if (window) {
    window->AddInputListener(&g_keys, SIZE_MAX);  // ahead of every other listener
  }
}

void RemoveQuitPrompt() {
  if (g_window) {
    g_window->RemoveInputListener(&g_keys);
  }
  if (g_dialog) {
    delete g_dialog;
  }
  g_drawer = nullptr;
  g_window = nullptr;
  g_open = false;
}

bool QuitPromptSeesPad(uint32_t user, uint32_t buttons) {
  if (user >= 4 || !REXCVAR_GET(rr6_quit_prompt)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_pad_mutex);
  Pad& pad = g_pads[user];
  const uint32_t pressed = buttons & ~pad.previous;
  pad.previous = buttons;
  const int64_t now = NowMs();

  if (g_open.load()) {
    pad.hidden = true;
    pad.held_since = 0;
    if (Settled()) {
      if (pressed & kA) {
        g_answer = kQuit;
      } else if (pressed & kB) {
        g_answer = kGoBack;
      }
    }
    return true;
  }

  if (pad.hidden) {
    if (buttons != 0) {
      return true;
    }
    pad.hidden = false;
  }

  if ((buttons & (kBack | kStart)) == (kBack | kStart)) {
    if (pad.held_since == 0) {
      pad.held_since = now;
      pad.asked = false;
    } else if (!pad.asked && now - pad.held_since >= kHoldMs) {
      pad.asked = true;
      if (g_context) {
        g_context->CallInUIThread([] { Ask(); });
      }
    }
  } else {
    pad.held_since = 0;
  }
  return false;
}

}  // namespace rr6
