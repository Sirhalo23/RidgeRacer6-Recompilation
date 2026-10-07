// The sound played when an achievement pop-up appears.
//
// The file is looked for in this order, starting from the folder the program
// is in (bin\ in a package):
//
//   ..\achievement.wav         the player's own, next to the launcher
//   achievement.wav            the same, next to the program
//   sounds\achievement.wav     the one that comes with the build: an original
//                              chime made by tools/make_chime.py
//
// The first two let players use any sound they like without this project
// having to ship it. A .wav file in ordinary PCM format is expected.
//
// Windows plays it through the system (PlaySound), Linux through SDL, which
// the SDK's runtime library carries; neither touches the game's own audio.
// rr6_achievement_sound = false turns it off.

#include "unlock_sound.h"

#include <filesystem>
#include <string>
#include <system_error>

#include <rex/cvar.h>
#include <rex/logging.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mmsystem.h>
#else
#include <unistd.h>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#endif

REXCVAR_DEFINE_BOOL(rr6_achievement_sound, true, "RR6",
                    "Play a sound when an achievement is unlocked. Put your own achievement.wav "
                    "next to the launcher to change the sound.");

namespace rr6 {
namespace {

std::filesystem::path ProgramFolder() {
#ifdef _WIN32
  wchar_t buffer[32768];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
  if (length == 0 || length >= std::size(buffer)) {
    return {};
  }
  return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#else
  char buffer[4096];
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length <= 0) {
    return {};
  }
  return std::filesystem::path(std::string(buffer, static_cast<size_t>(length))).parent_path();
#endif
}

std::filesystem::path FindSound() {
  const std::filesystem::path folder = ProgramFolder();
  if (folder.empty()) {
    return {};
  }
  const std::filesystem::path candidates[] = {
      folder.parent_path() / "achievement.wav",
      folder / "achievement.wav",
      folder / "sounds" / "achievement.wav",
  };
  std::error_code ec;
  for (const std::filesystem::path& candidate : candidates) {
    if (std::filesystem::is_regular_file(candidate, ec)) {
      return candidate;
    }
  }
  return {};
}

}  // namespace

void PlayUnlockSound() {
  if (!REXCVAR_GET(rr6_achievement_sound)) {
    return;
  }
  const std::filesystem::path sound = FindSound();
  if (sound.empty()) {
    REXLOG_INFO("[achievements] no achievement.wav found; the pop-up is silent");
    return;
  }
#ifdef _WIN32
  if (!PlaySoundW(sound.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
    REXLOG_INFO("[achievements] the sound could not be played");
  }
#else
  // One stream at a time: a new sound replaces one still playing.
  static SDL_AudioStream* stream = nullptr;
  if (stream) {
    SDL_DestroyAudioStream(stream);
    stream = nullptr;
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXLOG_INFO("[achievements] no audio for the sound: {}", SDL_GetError());
    return;
  }
  SDL_AudioSpec spec;
  Uint8* data = nullptr;
  Uint32 length = 0;
  if (!SDL_LoadWAV(sound.string().c_str(), &spec, &data, &length)) {
    REXLOG_INFO("[achievements] {} could not be read: {}", sound.string(), SDL_GetError());
    return;
  }
  stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (stream && SDL_PutAudioStreamData(stream, data, static_cast<int>(length))) {
    SDL_FlushAudioStream(stream);
    SDL_ResumeAudioStreamDevice(stream);
  } else {
    REXLOG_INFO("[achievements] the sound could not be played: {}", SDL_GetError());
  }
  SDL_free(data);
#endif
}

}  // namespace rr6
