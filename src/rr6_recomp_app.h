// rr6_recomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/ui/overlay/achievement_notification.h>

#ifdef __APPLE__
#include <cstdlib>

#include <rex/filesystem.h>
#include <rex/input/flags.h>
#endif

#include "achievements.h"
#include "dlc_install.h"
#include "frame_stats.h"
#include "gpu_choice.h"
#include "overlay_input.h"
#include "quit_prompt.h"
#include "thread_stats.h"
#include "timer_resolution.h"

class Rr6RecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Rr6RecompApp>(new Rr6RecompApp(ctx, "rr6_recomp",
        PPCImageConfig));
  }

#ifdef __APPLE__
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // The release launcher keeps mutable settings outside the signed bundle.
    if (const char* root = std::getenv("RR6_MACOS_RELEASE_USER_ROOT")) {
      paths.config_path = std::filesystem::path(root) / "rr6_recomp.toml";
    }
  }

  void OnPostInitLogging() override {
    // Finder and Terminal may launch from a different working directory.
    // Resolve the default SDL mapping database beside the executable, while
    // preserving an explicit custom mapping path.
    if (REXCVAR_GET(hid_mappings_file) == "gamecontrollerdb.txt") {
      REXCVAR_SET(hid_mappings_file,
                 (rex::filesystem::GetExecutableFolder() / "gamecontrollerdb.txt").string());
    }
  }
#endif

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
    rr6::StopThreadStats();
    rr6::RemoveAchievements();
    rr6::RemoveQuitPrompt();
    rr6::RemoveOverlayInput();
  }

  // Started only to install downloadable content (--rr6_install_content):
  // do that with the game's executable loaded but not started, and leave.
  // Otherwise note which content is installed and start the game as usual.
  void LaunchModule() override {
    if (rr6::ContentInstallRequested()) {
      rr6::InstallRequestedContent(runtime());
      // Leave the way the game does when its window is closed; ending the
      // message loop directly hangs in the SDK's teardown.
      app_context().CallInUIThreadDeferred([this]() {
        if (rex::ui::Window* main_window = window()) {
          main_window->RequestClose();
        }
      });
      return;
    }
    rr6::UseFineTimer();  // short waits really short on Windows (issue #7)
    rr6::AddContentFromDlcFolder(runtime());
    rr6::WriteInstalledContentList(runtime());
    SetGuestFrameStats(rr6::FrameStatsProvider());  // the F3 window's frame rate
    rr6::StartThreadStats();  // the busiest threads, in the log every 30 s
    rex::ReXApp::LaunchModule();
  }

  // Before the graphics backend starts: on a PC with two graphics adapters,
  // use the high-performance one (Windows).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    (void)config;
    rr6::ChooseGraphicsAdapter();
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
