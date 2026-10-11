#include "settings.h"

#include <cassert>
#include <iostream>
#include <map>

int main() {
  using namespace launcher;
  const std::string original =
      "\xEF\xBB\xBF# My settings\r\n"
      "user_language = 2 # keep this note\r\n"
      "custom_path = \"folder/#file\"\r\n"
      "custom_array = [\n  \"user_language = 6\", # not a setting\n  3\n]\n"
      "custom_string = '''\nresolution_scale = 8\n[not_a_table]\n'''\n"
      "resolution_scale = 3\n"
      "draw_resolution_scale_x = 2\n"
      "[custom]\nuser_language = 4\n";
  assert(UpdateSettings(original, {}) == original);
  assert(Setting(original, "user_language", "1") == "2");
  assert(Setting(original, "custom_path", "") == "folder/#file");
  std::map<std::string, std::string> changes{{"user_language", "1"}};
  RenderScale(changes, 1);
  DisplayVSync(changes, true);
  const auto updated = UpdateSettings(original, changes);
  assert(Setting(updated, "user_language", "") == "1");
  assert(updated.find("user_language = 1 # keep this note\r\n") != std::string::npos);
  assert(updated.find("custom_array = [\n  \"user_language = 6\", # not a setting\n  3\n]\n") != std::string::npos);
  assert(updated.find("custom_string = '''\nresolution_scale = 8\n[not_a_table]\n'''\n") != std::string::npos);
  assert(updated.find("[custom]\nuser_language = 4\n") != std::string::npos);
  assert(Setting(updated, "resolution_scale", "missing") == "missing");
  assert(Setting(updated, "draw_resolution_scale_x", "") == "1");
  assert(Setting(updated, "draw_resolution_scale_y", "") == "1");
  assert(Setting(updated, "vulkan_allow_present_mode_immediate", "") == "false");
  assert(Setting(updated, "vulkan_allow_present_mode_mailbox", "") == "false");
  assert(Setting(updated, "vulkan_allow_present_mode_fifo_relaxed", "") == "false");
  assert(updated.find("vulkan_allow_present_mode_immediate = false") < updated.find("[custom]"));
  assert(UpdateSettings(updated, changes) == updated);
  assert(Setting(UpdateSettings("# untouched\n", {{"keybind_a", Quote("Shift+Space,Return")}}), "keybind_a", "") == "Shift+Space,Return");
  assert(Setting(UpdateSettings("key = 'old' # note", {{"key", Quote("a\\b\"#c")}}), "key", "") == "a\\b\"#c");
  assert(UpdateSettings("key = 1", {{"new", "2"}}) == "key = 1\nnew = 2\n");
  const auto defaults = UpdateSettings("[extra]\nfullscreen = false\n", {{"fullscreen", "true"}});
  assert(defaults == "fullscreen = true\n[extra]\nfullscreen = false\n");
  assert(UpdateSettings("\"user_language\" = 2\n'fullscreen' = true\n", {{"user_language", "1"}, {"fullscreen", "false"}}) == "\"user_language\" = 1\n'fullscreen' = false\n");
  DisplayVSync(changes, false);
  assert(Setting(UpdateSettings(updated, changes), "vulkan_allow_present_mode_immediate", "") == "true");
  // Re-read the latest document: changing language must retain F4/text-editor edits.
  assert(UpdateSettings("fullscreen = false # edited after opening\n", {{"user_language", "1"}}).find("fullscreen = false # edited after opening") == 0);
  std::cout << "macOS launcher settings tests passed\n";
}
