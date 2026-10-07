// A way to leave the game without Alt+F4. See quit_prompt.cpp.
#pragma once

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace rr6 {

// Called once the window and its overlay drawer exist (UI thread), after
// InstallOverlayInput.
void InstallQuitPrompt(rex::ui::ImGuiDrawer* drawer);

// Called when the app shuts down (UI thread).
void RemoveQuitPrompt();

}  // namespace rr6
