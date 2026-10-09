// Windows timer resolution; see timer_resolution.cpp.

#pragma once

namespace rr6 {

// Windows: asks for a 1 ms system timer (rr6_fine_timer) and logs how long a
// 1 ms sleep takes. Does nothing elsewhere.
void UseFineTimer();

}  // namespace rr6
