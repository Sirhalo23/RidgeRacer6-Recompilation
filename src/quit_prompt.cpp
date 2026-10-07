// Leaving the game: Esc, or Back + Start held for a second, asks "Quit Ridge
// Racer 6?".
//
// The game is a console game and has no way out of its own; before this the
// only ways were Alt+F4 and the window's close button, and a full-screen
// window has no close button. Both still work, and still close at once.
//
//   keyboard    Esc asks; Enter quits, Esc again goes back to the game, Y (or
//               the achievements key) opens the achievements list.
//   controller  Back + Start held for a second asks (Create/Share + Options on
//               a PlayStation pad, View + Menu on a Steam Deck); A quits, B
//               goes back, Y opens the achievements list.
//   mouse       the buttons can be clicked.
//
// The question is drawn by the SDK's overlay (ImGui), like its F3 and F4
// windows. The game goes on running behind it, and a question nobody answers
// for half a minute goes away again. Input comes through overlay_input.cpp,
// which also keeps it from the game meanwhile.
//
// Quitting asks the window to close, which is what Alt+F4 does; nothing new
// happens to the game on the way out.
//
// rr6_quit_prompt = false turns the question off. rr6_quit_key names the key.

#include "quit_prompt.h"

#include <chrono>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

#include "achievements.h"
#include "overlay_input.h"

REXCVAR_DEFINE_BOOL(rr6_quit_prompt, true, "RR6",
                    "Esc, or Back + Start held for a second, asks whether to quit the game.");

REXCVAR_DECLARE(std::string, rr6_quit_key);
REXCVAR_DECLARE(std::string, rr6_achievements_key);

namespace rr6 {
namespace {

constexpr int64_t kGiveUpMs = 30000;  // an unanswered question goes away by itself

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// UI thread only.
class QuitDialog;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
QuitDialog* g_dialog = nullptr;

class QuitDialog : public rex::ui::ImGuiDialog {
 public:
  explicit QuitDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {
    opened_ms_ = NowMs();
    OverlayOpened();
  }
  ~QuitDialog() override {
    OverlayClosed();
    g_dialog = nullptr;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    // Answers from the keyboard and the controller; the buttons below can
    // also be clicked. Nothing answers in the first moments.
    const bool settled = OverlayInputSettled();
    const uint32_t buttons = TakePadPresses();
    bool quit = false, go_back = false, achievements = false;
    if (settled) {
      quit = (buttons & pad::kA) != 0;
      go_back = (buttons & pad::kB) != 0;
      achievements = (buttons & pad::kY) != 0;
    }
    const rex::ui::VirtualKey quit_key = rex::ui::ParseVirtualKey(REXCVAR_GET(rr6_quit_key));
    const rex::ui::VirtualKey list_key =
        rex::ui::ParseVirtualKey(REXCVAR_GET(rr6_achievements_key));
    for (const KeyPress& press : TakeKeyPresses()) {
      if (press.repeat || !settled) {
        continue;
      }
      if (press.key == quit_key) {
        go_back = true;
      } else if (press.key == rex::ui::VirtualKey::kReturn) {
        quit = true;
      } else if (press.key == list_key || press.key == rex::ui::VirtualKey::kY) {
        achievements = true;
      }
    }
    // Left unanswered, the question goes away: nobody is left looking at it
    // with no way to answer.
    if (NowMs() - opened_ms_ >= kGiveUpMs) {
      go_back = true;
    }

    // Sizes are given for a picture 720 lines high and grow with the window.
    const ImVec2 screen = io.DisplaySize;
    const float u = (screen.y > 480.0f ? screen.y : 480.0f) / 720.0f;
    const ImVec4 lime(0.749f, 0.961f, 0.180f, 1.0f);
    const ImVec4 lime_hot(0.816f, 0.980f, 0.345f, 1.0f);
    const ImVec4 ink(0.086f, 0.098f, 0.114f, 1.0f);
    const ImVec4 quiet(0.62f, 0.66f, 0.68f, 1.0f);
    const ImVec4 grey(0.20f, 0.23f, 0.26f, 1.0f);
    const ImVec4 grey_hot(0.27f, 0.31f, 0.34f, 1.0f);

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
      ImGui::PushStyleColor(ImGuiCol_Button, grey);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, grey_hot);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, grey_hot);
      if (ImGui::Button("Keep playing", ImVec2(width, 0)) && settled) {
        go_back = true;
      }
      ImGui::PopStyleColor(3);
      Hint("Esc, or B on a controller", quiet, 15 * u);
      ImGui::EndGroup();

      // A second row: the achievements list, which is also the only way to it
      // without a keyboard.
      ImGui::Dummy(ImVec2(1, 4 * u));
      ImGui::PushStyleColor(ImGuiCol_Button, grey);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, grey_hot);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, grey_hot);
      if (ImGui::Button("Achievements", ImVec2(width * 2 + 16 * u, 0)) && settled) {
        achievements = true;
      }
      ImGui::PopStyleColor(3);
      Hint("Y on the keyboard or a controller", quiet, 15 * u);
      ImGui::EndDisabled();
      ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(7);

    if (quitting_) {
      return;
    }
    if (quit) {
      quitting_ = true;
      REXLOG_INFO("[quit] the player chose to quit; closing the window");
      // Not from inside the drawing: closing can destroy the window.
      RunOnUiThreadLater([] {
        if (rex::ui::Window* window = OverlayWindow()) {
          window->RequestClose();
        }
      });
    } else if (achievements) {
      REXLOG_INFO("[quit] on to the achievements list");
      ShowAchievementList();
      Close();  // deletes this dialog once the drawing is over
    } else if (go_back) {
      REXLOG_INFO("[quit] back to the game");
      Close();
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

  int64_t opened_ms_ = 0;
  bool quitting_ = false;
};

void Ask() {
  if (!g_drawer || g_dialog || AnyOverlayOpen() || !REXCVAR_GET(rr6_quit_prompt)) {
    return;
  }
  g_dialog = new QuitDialog(g_drawer);
  REXLOG_INFO("[quit] asking whether to quit");
}

}  // namespace

void InstallQuitPrompt(rex::ui::ImGuiDrawer* drawer) {
  g_drawer = drawer;
  SetMenuRequestHandler([] { Ask(); });
}

void RemoveQuitPrompt() {
  if (g_dialog) {
    delete g_dialog;
  }
  g_drawer = nullptr;
}

}  // namespace rr6
