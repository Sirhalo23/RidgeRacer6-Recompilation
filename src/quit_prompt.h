// A way to leave the game without Alt+F4. See quit_prompt.cpp.
#pragma once

#include <cstdint>

namespace rex::ui {
class ImGuiDrawer;
class Window;
class WindowedAppContext;
}  // namespace rex::ui

namespace rr6 {

// Called once the window and its overlay drawer exist (UI thread).
void InstallQuitPrompt(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
                       rex::ui::WindowedAppContext* context);

// Called when the app shuts down (UI thread).
void RemoveQuitPrompt();

// Called for every controller state the game reads (game thread), with the
// XINPUT button bits. Returns true when the game must not see this state:
// while the question is on screen, and afterwards until the buttons that
// answered it have been let go.
bool QuitPromptSeesPad(uint32_t user, uint32_t buttons);

}  // namespace rr6
