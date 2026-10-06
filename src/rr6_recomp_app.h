// rr6_recomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#include "quit_prompt.h"

class Rr6RecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Rr6RecompApp>(new Rr6RecompApp(ctx, "rr6_recomp",
        PPCImageConfig));
  }

  // The "Quit Ridge Racer 6?" question (Esc, or Back + Start held).
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rr6::InstallQuitPrompt(drawer, window(), &app_context());
  }
  void OnShutdown() override { rr6::RemoveQuitPrompt(); }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
