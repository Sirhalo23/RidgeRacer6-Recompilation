// Waits for the graphics chip and for file reads; see wait_stats.cpp.

#pragma once

namespace rr6 {

// Logs the waits since the last call and starts counting again. Called every
// 30 seconds by the [threads] logger (thread_stats.cpp).
void LogWaitStats();

}  // namespace rr6
