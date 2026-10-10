// Use the fast graphics card on PCs that have two (issue #7).
//
// Many laptops have a graphics chip built into the processor and a separate,
// much faster graphics card. The SDK's Direct3D 12 backend takes the first
// adapter Windows lists that can run Direct3D 12 (d3d12_adapter = -1), and on
// such laptops that is usually the built-in chip. A tester with an RTX 4060
// laptop got 29-51 frames per second, about the same at the lowest settings.
//
// Before the graphics backend starts (OnPreSetup), the game program lists the
// adapters itself in the same order the SDK does, asks Windows which one is
// the high-performance one (IDXGIFactory6::EnumAdapterByGpuPreference; on older
// Windows, the one with the most video memory) and sets d3d12_adapter to it
// when that is not the first. The log names the adapters and the choice.
//
// A d3d12_adapter set by hand is kept, unless it no longer names a hardware
// adapter (a card was removed); the SDK would then fail to start at all.
// rr6_prefer_fast_gpu = false leaves the choice to the SDK.

#include "gpu_choice.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <cstdint>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

REXCVAR_DECLARE(int32_t, d3d12_adapter);  // defined by the SDK (d3d12_provider.cpp)
#endif

REXCVAR_DEFINE_BOOL(rr6_prefer_fast_gpu, true, "RR6",
                    "On a PC with two graphics adapters (a laptop with a built-in chip and a "
                    "graphics card), use the high-performance one.");

namespace rr6 {

#if defined(_WIN32)

namespace {

using Microsoft::WRL::ComPtr;

struct Adapter {
  UINT index = 0;
  LUID luid{};
  std::string name;
  uint64_t video_memory_mb = 0;
  bool usable = false;  // hardware, and can create a Direct3D 12 device
};

std::string Narrow(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return std::string();
  std::string out(size_t(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

bool SameLuid(const LUID& a, const LUID& b) {
  return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

}  // namespace

void ChooseGraphicsAdapter() {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return;

  // The adapters in the order the SDK goes through them.
  std::vector<Adapter> adapters;
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
    DXGI_ADAPTER_DESC1 desc;
    Adapter entry;
    entry.index = i;
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      entry.luid = desc.AdapterLuid;
      entry.name = Narrow(desc.Description);
      entry.video_memory_mb = uint64_t(desc.DedicatedVideoMemory) >> 20;
      entry.usable = !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                     SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                                 __uuidof(ID3D12Device), nullptr));
    }
    adapters.push_back(entry);
    adapter.Reset();
  }

  std::string list;
  const Adapter* first_usable = nullptr;
  size_t usable_count = 0;
  for (const Adapter& entry : adapters) {
    if (!entry.usable) continue;
    if (!first_usable) first_usable = &entry;
    ++usable_count;
    if (!list.empty()) list += ", ";
    list += fmt::format("{} '{}' ({} MB)", entry.index, entry.name, entry.video_memory_mb);
  }
  REXLOG_INFO("[gpu] graphics adapters: {}", list.empty() ? std::string("none found") : list);

  const int32_t chosen = REXCVAR_GET(d3d12_adapter);
  if (chosen >= 0) {
    const bool valid = size_t(chosen) < adapters.size() && adapters[size_t(chosen)].usable;
    if (valid) {
      REXLOG_INFO("[gpu] d3d12_adapter = {} is set; keeping it", chosen);
      return;
    }
    REXLOG_WARN("[gpu] d3d12_adapter = {} is not a usable adapter any more; choosing again",
                chosen);
    REXCVAR_SET(d3d12_adapter, -1);
  } else if (chosen != -1) {
    return;  // -2: software rendering asked for
  }
  if (!REXCVAR_GET(rr6_prefer_fast_gpu) || usable_count < 2) return;

  // Windows' own idea of the high-performance adapter.
  const Adapter* fast = nullptr;
  ComPtr<IDXGIFactory6> factory6;
  if (SUCCEEDED(factory.As(&factory6))) {
    ComPtr<IDXGIAdapter1> preferred;
    for (UINT i = 0; !fast && SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                                  i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&preferred)));
         ++i) {
      DXGI_ADAPTER_DESC1 desc;
      if (SUCCEEDED(preferred->GetDesc1(&desc))) {
        for (const Adapter& entry : adapters) {
          if (entry.usable && SameLuid(entry.luid, desc.AdapterLuid)) {
            fast = &entry;
            break;
          }
        }
      }
      preferred.Reset();
    }
  }
  if (!fast) {
    for (const Adapter& entry : adapters) {
      if (entry.usable && (!fast || entry.video_memory_mb > fast->video_memory_mb)) fast = &entry;
    }
  }
  if (!fast || fast == first_usable) {
    REXLOG_INFO("[gpu] using {} '{}', the high-performance adapter", first_usable->index,
                first_usable->name);
    return;
  }
  REXCVAR_SET(d3d12_adapter, int32_t(fast->index));
  REXLOG_INFO("[gpu] using {} '{}', the high-performance adapter, instead of {} '{}'", fast->index,
              fast->name, first_usable->index, first_usable->name);
}

#else

void ChooseGraphicsAdapter() {}

#endif

}  // namespace rr6
