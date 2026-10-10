// Logging which threads keep the processor busy; see thread_stats.cpp.

#pragma once

namespace rr6 {

// Starts a background thread that logs the busiest threads every 30 seconds
// (rr6_thread_stats).
void StartThreadStats();
// Stops it; called when the game closes.
void StopThreadStats();

}  // namespace rr6
