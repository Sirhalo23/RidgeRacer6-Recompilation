// Keyboard and controller input for our own overlays (the quit question and
// the achievements list). See overlay_input.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <rex/ui/virtual_key.h>

namespace rex::ui {
class Window;
class WindowedAppContext;
}  // namespace rex::ui

namespace rr6 {

// XINPUT button bits. A stick pushed up or down counts as the D-pad.
namespace pad {
constexpr uint32_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010,
                   kBack = 0x0020, kLB = 0x0100, kRB = 0x0200, kA = 0x1000, kB = 0x2000,
                   kX = 0x4000, kY = 0x8000;
}  // namespace pad

struct KeyPress {
  rex::ui::VirtualKey key;
  bool repeat;  // sent by a key that is being held down
};

// UI thread, at start and at shutdown.
void InstallOverlayInput(rex::ui::Window* window, rex::ui::WindowedAppContext* context);
void RemoveOverlayInput();

// What to do, with no overlay open, when the player presses the quit key or
// holds Back + Start, and when the player presses the achievements key. Both
// are called on the UI thread.
void SetMenuRequestHandler(std::function<void()> handler);
void SetAchievementsRequestHandler(std::function<void()> handler);

// UI thread: an overlay says when it appears and when it goes. While one is
// open the game is given an idle controller.
void OverlayOpened();
void OverlayClosed();
bool AnyOverlayOpen();
// False in the first moments after an overlay appeared over the game, so that
// a button the player was pressing for the game does not answer it.
bool OverlayInputSettled();

// UI thread, once per frame, by the overlay that is open: the controller
// buttons pressed since the last call, the buttons down now, and the keys
// pressed since the last call.
uint32_t TakePadPresses();
uint32_t PadHeld();
std::vector<KeyPress> TakeKeyPresses();

// Runs a function on the UI thread a moment later (not from inside drawing).
void RunOnUiThreadLater(std::function<void()> function);
// The game's window, or null once the app is shutting down. UI thread.
rex::ui::Window* OverlayWindow();

// Game thread, for every controller state the game reads: the XINPUT button
// bits and the left stick. Returns true when the game must not see this state.
bool OverlaySeesPad(uint32_t user, uint32_t buttons, int stick_x, int stick_y);

}  // namespace rr6
