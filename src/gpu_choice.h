// Choosing the graphics adapter; see gpu_choice.cpp.

#pragma once

namespace rr6 {

// Windows: on a PC with more than one graphics adapter, sets d3d12_adapter to
// the high-performance one before the graphics backend starts. Does nothing
// elsewhere.
void ChooseGraphicsAdapter();

}  // namespace rr6
