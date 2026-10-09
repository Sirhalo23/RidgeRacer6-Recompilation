// The game's frame rate, for the SDK's F3 window and for the log.
// See frame_stats.cpp.

#pragma once

#include <memory>

#include <rex/ui/overlay/debug_overlay.h>

namespace rr6 {

// What the F3 window shows: frames per second and frame time, measured over
// half a second at a time.
rex::ui::DebugOverlayDialog::FrameStatsProvider FrameStatsProvider();

}  // namespace rr6
