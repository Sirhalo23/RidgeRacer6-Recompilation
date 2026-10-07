// Achievements: the pop-up, the list, and the copy the launcher reads. See
// achievements.cpp.
#pragma once

#include <functional>
#include <memory>

namespace rex {
class Runtime;
namespace ui {
class AchievementNotificationDialog;
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace ui
}  // namespace rex

namespace rr6 {

// The parts of the app the achievements code draws with. Any of them can be
// null early in start-up and during shutdown.
struct AppParts {
  rex::ui::ImGuiDrawer* drawer = nullptr;
  rex::ui::ImmediateDrawer* immediate = nullptr;
  rex::Runtime* runtime = nullptr;
};

// UI thread, once the overlay drawer exists. `parts` is asked whenever the
// parts are needed.
void InstallAchievements(std::function<AppParts()> parts);

// UI thread, at shutdown, before the drawer and the runtime go.
void RemoveAchievements();

// The pop-up shown when an achievement is unlocked, in place of the SDK's.
std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementPopup(
    rex::ui::ImGuiDrawer* drawer);

// UI thread: opens the achievements list over the game, unless it is open.
void ShowAchievementList();

}  // namespace rr6
