// rr6_recomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/ui/overlay/achievement_notification.h>

#include "achievements.h"
#include "overlay_input.h"
#include "quit_prompt.h"

class Rr6RecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Rr6RecompApp>(new Rr6RecompApp(ctx, "rr6_recomp",
        PPCImageConfig));
  }

  // Our overlays: the "Quit Ridge Racer 6?" question (Esc, or Back + Start
  // held) and the achievements list and pop-up, in place of the SDK's.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rr6::InstallOverlayInput(window(), &app_context());
    rr6::InstallQuitPrompt(drawer);
    rr6::InstallAchievements(
        [this] { return rr6::AppParts{imgui_drawer(), immediate_drawer(), runtime()}; });
  }
  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override { return nullptr; }
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog()
      override {
    return rr6::CreateAchievementPopup(imgui_drawer());
  }
  void OnShutdown() override {
    rr6::RemoveAchievements();
    rr6::RemoveQuitPrompt();
    rr6::RemoveOverlayInput();
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
