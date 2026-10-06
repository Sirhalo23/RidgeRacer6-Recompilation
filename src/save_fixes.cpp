// Save-data support.
//
// The game's save library (Namco "nuSave") opens its save with
// XContentCreate(OPEN_EXISTING) and reads the outcome with XGetOverlappedResult.
// It understands exactly one failure value: ERROR_FUNCTION_FAILED (0x65B), which
// it treats as "there is no save data yet" and then offers to create one. Any
// other failure is treated as "the storage device could not be read".
//
// The SDK completes a missing-content open with the raw Win32 code
// (ERROR_PATH_NOT_FOUND, 3) instead, so on a fresh install the game concluded
// the hard drive was broken and never created a save. Here the not-found result
// of a content open is reported the way the game expects.
//
// Thumbnail: after saving, the game hands the system a small picture for the
// dashboard (XContentSetThumbnail). The SDK stores it as a file called
// "__thumbnail.png" inside the save's own folder. When the game loads, it
// lists that folder and takes the first file it is given as its save data;
// its own data file has a different, encoded name every time it saves. If the
// picture happens to be listed first, the game reads the picture, fails its
// check and reports "Game Data is corrupted" (on Windows the listing is
// alphabetical, so it depends on the name the data file got; on Linux it was
// seen on the first try). Nothing here needs the picture, so it is not stored:
// the save folder then holds exactly one file. rr6_save_thumbnail = true
// restores the SDK behaviour.
//
// The remaining hooks only log the save-related calls (to the normal log, at
// info level) so the whole load/save sequence can be followed in a test run.

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "generated/default/rr6_recomp_init.h"

REXCVAR_DEFINE_BOOL(rr6_save_thumbnail, false, "RR6",
                    "Store the save's thumbnail picture in the save folder, as the SDK does by "
                    "default. Off because the game can then mistake the picture for its save "
                    "data and report the save as corrupted.");

namespace {

constexpr uint32_t kErrorFileNotFound = 2;
constexpr uint32_t kErrorPathNotFound = 3;
constexpr uint32_t kErrorIoIncomplete = 0x3E4;
constexpr uint32_t kErrorFunctionFailed = 0x65B;

std::mutex g_mutex;
std::unordered_set<uint32_t> g_content_opens;  // guest XOVERLAPPED addresses awaiting a result

std::string GuestString(uint8_t* base, uint32_t address, size_t max_length = 64) {
  if (!address) {
    return "(null)";
  }
  const char* text = reinterpret_cast<const char*>(REX_RAW_ADDR(address));
  return std::string(text, strnlen(text, max_length));
}

// Big-endian UTF-16 from guest memory; non-ASCII characters become '?'.
std::string GuestWideString(uint8_t* base, uint32_t address, size_t max_chars = 64) {
  if (!address) {
    return "(null)";
  }
  std::string out;
  for (size_t i = 0; i < max_chars; ++i) {
    const uint16_t c = REX_LOAD_U16(address + 2 * static_cast<uint32_t>(i));
    if (!c) {
      break;
    }
    out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  }
  return out;
}

// Logs the first few calls of a kind, then only when the outcome changes.
struct ChangeLogger {
  std::mutex mutex;
  uint64_t last = ~0ull;
  int count = 0;
  bool ShouldLog(uint64_t key) {
    std::lock_guard<std::mutex> lock(mutex);
    const bool changed = key != last;
    last = key;
    return changed || ++count <= 3;
  }
};
ChangeLogger g_device_state_log;
ChangeLogger g_device_data_log;

}  // namespace

// XContentCreate(user, root_name, XCONTENT_DATA*, flags, disposition*, license_mask*, XOVERLAPPED*)
REX_HOOK_RAW(sub_82247E10) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t root = ctx.r4.u32;
  const uint32_t data = ctx.r5.u32;
  const uint32_t flags = ctx.r6.u32;
  const uint32_t overlapped = ctx.r9.u32;
  if (overlapped) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_content_opens.insert(overlapped);
  }
  std::string root_name = GuestString(base, root, 16);
  uint32_t device = 0, type = 0;
  std::string display_name = "(null)", file_name = "(null)";
  if (data) {
    device = REX_LOAD_U32(data);
    type = REX_LOAD_U32(data + 4);
    display_name = GuestWideString(base, data + 8, 64);
    file_name = GuestString(base, data + 0x108, 42);
  }
  __imp__sub_82247E10(ctx, base);
  REXLOG_INFO("[save] XContentCreate(user={}, root='{}', device={}, type={}, name='{}', file='{}', "
              "flags={:#x}) -> {:#x}",
              user, root_name, device, type, display_name, file_name, flags, ctx.r3.u32);
}

// XGetOverlappedResult(XOVERLAPPED*, result*, wait)
REX_HOOK_RAW(sub_82247F80) {
  const uint32_t overlapped = ctx.r3.u32;
  __imp__sub_82247F80(ctx, base);
  const uint32_t result = ctx.r3.u32;
  if (result == kErrorIoIncomplete || !overlapped) {
    return;  // still running
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_content_opens.erase(overlapped) == 0) {
      return;  // not a content open
    }
  }
  if (result == kErrorFileNotFound || result == kErrorPathNotFound) {
    REXLOG_INFO("[save] content open: not found ({}) -> reporting ERROR_FUNCTION_FAILED so the "
                "game treats it as 'no save data yet'",
                result);
    REX_STORE_U32(overlapped, kErrorFunctionFailed);
    ctx.r3.u64 = kErrorFunctionFailed;
  } else {
    REXLOG_INFO("[save] content open/create finished with result {:#x}", result);
  }
}

// XContentClose(root_name, XOVERLAPPED*)
REX_HOOK_RAW(sub_82247E38) {
  const std::string root_name = GuestString(base, ctx.r3.u32, 16);
  __imp__sub_82247E38(ctx, base);
  REXLOG_INFO("[save] XContentClose(root='{}') -> {:#x}", root_name, ctx.r3.u32);
}

// XContentDelete(user, XCONTENT_DATA*, XOVERLAPPED*)
REX_HOOK_RAW(sub_82247E30) {
  const uint32_t data = ctx.r4.u32;
  const std::string file_name = data ? GuestString(base, data + 0x108, 42) : "(null)";
  __imp__sub_82247E30(ctx, base);
  REXLOG_INFO("[save] XContentDelete(file='{}') -> {:#x}", file_name, ctx.r3.u32);
}

// XContentSetThumbnail(user, XCONTENT_DATA*, image*, image_size, XOVERLAPPED*)
REX_HOOK_RAW(sub_82247E40) {
  const uint32_t size = ctx.r6.u32;
  const uint32_t overlapped = ctx.r7.u32;
  // Only the plain synchronous form is answered here (the only form this game
  // uses); a call with an XOVERLAPPED needs the system to complete it.
  if (!REXCVAR_GET(rr6_save_thumbnail) && !overlapped) {
    ctx.r3.u64 = 0;  // ERROR_SUCCESS
    REXLOG_INFO("[save] XContentSetThumbnail({} bytes): not stored, so the save folder holds only "
                "the save data",
                size);
    return;
  }
  __imp__sub_82247E40(ctx, base);
  REXLOG_INFO("[save] XContentSetThumbnail({} bytes) -> {:#x}", size, ctx.r3.u32);
}

// XContentCreateEnumerator(user, device, type, flags, max_items, buffer_size*, handle*)
REX_HOOK_RAW(sub_82247E48) {
  const uint32_t device = ctx.r4.u32;
  const uint32_t type = ctx.r5.u32;
  const uint32_t flags = ctx.r6.u32;
  __imp__sub_82247E48(ctx, base);
  REXLOG_INFO("[save] XContentCreateEnumerator(device={}, type={}, flags={:#x}) -> {:#x}", device,
              type, flags, ctx.r3.u32);
}

// XContentGetDeviceState(device, XOVERLAPPED*)
REX_HOOK_RAW(sub_82247E50) {
  const uint32_t device = ctx.r3.u32;
  __imp__sub_82247E50(ctx, base);
  const uint32_t result = ctx.r3.u32;
  if (g_device_state_log.ShouldLog((static_cast<uint64_t>(device) << 32) | result)) {
    REXLOG_INFO("[save] XContentGetDeviceState(device={}) -> {:#x}", device, result);
  }
}

// XContentGetDeviceData(device, XDEVICE_DATA*)
REX_HOOK_RAW(sub_82247E58) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t out = ctx.r4.u32;
  __imp__sub_82247E58(ctx, base);
  const uint32_t result = ctx.r3.u32;
  if (g_device_data_log.ShouldLog((static_cast<uint64_t>(device) << 32) | result)) {
    const uint64_t total = (result == 0 && out) ? REX_LOAD_U64(out + 8) : 0;
    const uint64_t free_bytes = (result == 0 && out) ? REX_LOAD_U64(out + 16) : 0;
    REXLOG_INFO("[save] XContentGetDeviceData(device={}) -> {:#x}, total {} MB, free {} MB", device,
                result, total >> 20, free_bytes >> 20);
  }
}
