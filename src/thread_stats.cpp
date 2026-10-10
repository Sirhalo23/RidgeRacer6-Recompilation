// Which threads keep the processor busy (issue #7).
//
// Two testers see the game slow down while their graphics card is mostly idle,
// so the time goes into the processor. The game runs on several threads: the
// game's own (named XThreadNNNN by the SDK), the one that turns the game's
// graphics commands into Direct3D or Vulkan work ("GPU Commands"), the audio
// worker, the XMA music decoder, and more. Every 30 seconds this logs the
// busiest of them, as a percentage of one processor core, next to the [fps]
// line: a thread near 100% is the one holding the game back.
//
//   [threads] busiest over 30 s: XThread0007 97%, GPU Commands 64%, ...
//             (all threads 245% of one core; 8 logical processors)
//
// Windows: Toolhelp lists the threads, GetThreadTimes gives their processor
// time and GetThreadDescription their names. Linux: /proc/self/task.

#include "thread_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>
#else
#include <dirent.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#endif

REXCVAR_DEFINE_BOOL(rr6_thread_stats, true, "RR6",
                    "Every 30 seconds, log the busiest threads (processor time as a percentage "
                    "of one core), to show what holds the game back on a slow PC.");

namespace {

struct ThreadSample {
  std::string name;
  double cpu_seconds = 0.0;  // total processor time used so far
};

using Samples = std::map<uint64_t, ThreadSample>;  // by thread id

#if defined(_WIN32)

using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

std::string Narrow(const wchar_t* text) {
  if (!text || !*text) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return std::string();
  std::string out(size_t(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

Samples TakeSamples() {
  static const auto get_description = reinterpret_cast<GetThreadDescriptionFn>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
  Samples samples;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return samples;
  const DWORD process = GetCurrentProcessId();
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != process) continue;
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (!thread) continue;
    FILETIME created, exited, kernel, user;
    if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
      ThreadSample sample;
      const uint64_t k = (uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
      const uint64_t u = (uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
      sample.cpu_seconds = double(k + u) / 1e7;
      if (get_description) {
        PWSTR description = nullptr;
        if (SUCCEEDED(get_description(thread, &description)) && description) {
          sample.name = Narrow(description);
          LocalFree(description);
        }
      }
      if (sample.name.empty()) sample.name = "thread " + std::to_string(entry.th32ThreadID);
      samples[entry.th32ThreadID] = std::move(sample);
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  return samples;
}

#else

Samples TakeSamples() {
  Samples samples;
  const double ticks = double(sysconf(_SC_CLK_TCK));
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return samples;
  while (dirent* item = readdir(dir)) {
    if (item->d_name[0] < '0' || item->d_name[0] > '9') continue;
    const std::string base = std::string("/proc/self/task/") + item->d_name;
    std::ifstream stat(base + "/stat");
    std::string line;
    if (!std::getline(stat, line)) continue;
    // The name is in brackets and may contain spaces; the fields after it are
    // numbered from 3 (state). utime and stime are fields 14 and 15.
    const size_t close = line.rfind(')');
    if (close == std::string::npos) continue;
    std::istringstream rest(line.substr(close + 2));
    std::string field;
    double utime = 0, stime = 0;
    for (int index = 3; rest >> field; ++index) {
      if (index == 14) utime = std::atof(field.c_str());
      if (index == 15) {
        stime = std::atof(field.c_str());
        break;
      }
    }
    ThreadSample sample;
    sample.cpu_seconds = (utime + stime) / ticks;
    std::ifstream comm(base + "/comm");
    std::getline(comm, sample.name);
    if (sample.name.empty()) sample.name = std::string("thread ") + item->d_name;
    samples[std::strtoull(item->d_name, nullptr, 10)] = std::move(sample);
  }
  closedir(dir);
  return samples;
}

#endif

// Never destroyed: the detached thread may still be waiting on it while the
// process ends.
struct State {
  std::mutex mutex;
  std::condition_variable wake;
  bool stop = false;
};
State& state() {
  static State* s = new State;
  return *s;
}
std::atomic<bool> g_started{false};

void Run() {
  using Clock = std::chrono::steady_clock;
  Samples previous = TakeSamples();
  auto previous_time = Clock::now();
  const unsigned processors = std::max(1u, std::thread::hardware_concurrency());
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(state().mutex);
      if (state().wake.wait_for(lock, std::chrono::seconds(30), [] { return state().stop; })) {
        return;
      }
    }
    Samples current = TakeSamples();
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - previous_time).count();
    std::vector<std::pair<double, std::string>> busy;  // (percent of one core, name)
    double total = 0.0;
    for (const auto& [id, sample] : current) {
      const auto old = previous.find(id);
      const double used = sample.cpu_seconds - (old != previous.end() ? old->second.cpu_seconds : 0.0);
      if (used <= 0.0 || seconds <= 0.0) continue;
      const double percent = 100.0 * used / seconds;
      total += percent;
      busy.emplace_back(percent, sample.name);
    }
    std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string list;
    for (size_t i = 0; i < busy.size() && i < 6; ++i) {
      if (busy[i].first < 2.0) break;
      if (!list.empty()) list += ", ";
      list += fmt::format("{} {:.0f}%", busy[i].second, busy[i].first);
    }
    REXLOG_INFO("[threads] busiest over {:.0f} s: {} (all threads {:.0f}% of one core; {} logical "
                "processors)",
                seconds, list.empty() ? std::string("none") : list, total, processors);
    previous = std::move(current);
    previous_time = now;
  }
}

}  // namespace

namespace rr6 {

void StartThreadStats() {
  if (!REXCVAR_GET(rr6_thread_stats) || g_started.exchange(true)) return;
  // Detached: the game may end the process without running destructors, and a
  // joinable std::thread left at exit would abort it.
  std::thread(Run).detach();
}

void StopThreadStats() {
  {
    std::lock_guard<std::mutex> lock(state().mutex);
    state().stop = true;
  }
  state().wake.notify_all();
}

}  // namespace rr6
