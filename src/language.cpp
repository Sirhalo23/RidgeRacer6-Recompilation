// The language the game shows its text in.
//
// The disc holds English, Japanese, German, French, Spanish and Italian. The
// game asks XGetLanguage which one to use, and the SDK's XGetLanguage always
// answers English (its comment says the region should come from the
// executable). The SDK does have a setting for the console's language,
// user_language, but only ExGetXConfigSetting reads it, and the game does not
// ask that for the language.
//
// So the game program answers XGetLanguage itself, from that same setting:
// "user_language = 3" in rr6_recomp.toml (the launcher's Language choice, the
// Linux script's --language) gives German. The numbers are the console's:
//   1 English  2 Japanese  3 German  4 French  5 Spanish  6 Italian
// Anything else is answered as English, the language every copy has.
//
// The game's code calls the runtime's __imp__XGetLanguage by name; a
// definition here, in the game program, is used in its place (the linker
// takes a symbol from an object file before it looks in the runtime's import
// library, and on Linux the program's own symbol comes first).

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DECLARE(uint32_t, user_language);  // defined by the SDK (xam_user.cpp)

namespace {

const char* const kLanguageNames[] = {"", "English", "Japanese", "German", "French", "Spanish",
                                      "Italian"};

std::atomic<bool> g_reported{false};

}  // namespace

extern "C" REX_FUNC(__imp__XGetLanguage) {
  (void)base;
  uint32_t language = REXCVAR_GET(user_language);
  if (language < 1 || language > 6) {
    language = 1;
  }
  if (!g_reported.exchange(true)) {
    REXLOG_INFO("[language] the game asked for its language: {} ({})", kLanguageNames[language],
                language);
  }
  ctx.r3.u64 = language;
}
