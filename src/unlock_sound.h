// The sound played when an achievement pop-up appears. See unlock_sound.cpp.
#pragma once

namespace rr6 {

// Any thread. Returns at once; the sound plays by itself.
void PlayUnlockSound();

}  // namespace rr6
