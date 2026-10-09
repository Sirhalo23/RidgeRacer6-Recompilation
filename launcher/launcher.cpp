// Ridge Racer 6 PC - launcher.
//
// A small native Windows program (no dependencies beyond Windows itself) that
// lets a player choose display and control settings, writes them to the game's
// settings file (rr6_recomp.toml, next to the game executable) and starts the
// game. The game reads that file itself; nothing here is required to play.
// A fourth page shows the player's achievements, from what the game saved.
//
// Files: launcher.cpp (this), disc_image.cpp/.h (copies the game files out of
// the player's disc image), movie_still.c/.h and pl_mpeg_sofdec.h (stills from
// the opening movie), launcher.rc, launcher.manifest, launcher.ico.
//
// The picture at the top of the window is not stored in this program. It is
// taken, on the player's PC, from the opening movie of the player's own copy
// of the game (see movie_still.c), or from a picture file the player puts next
// to the launcher (launcher-art.png / .jpg / .bmp). Without either, a drawn
// banner is shown.
//
// Build: see build.sh (cross-compiles with MinGW-w64; the same commands work
// in an MSYS2 MinGW-w64 shell on Windows).
//
// Command-line switches for automated checks:
//   --save-and-exit     write the settings file with the current choices and quit
//   --screen WxH        pretend the screen has this size (e.g. --screen 3440x1440)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// GDI+ expects the min/max macros that NOMINMAX removes.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "disc_image.h"
#include "movie_still.h"

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::wstring Widen(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool FileExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileUtf8(const std::wstring& path, std::string* out) {
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return false;
  std::string data;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  fclose(f);
  if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
      (unsigned char)data[2] == 0xBF) {
    data.erase(0, 3);
  }
  *out = data;
  return true;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& data) {
  FILE* f = _wfopen(path.c_str(), L"wb");
  if (!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  fclose(f);
  return ok;
}

// ---------------------------------------------------------------------------
// Settings file: flat "name = value" lines (the subset of TOML the game uses)
// ---------------------------------------------------------------------------

struct Entry {
  std::string key;
  std::string value;  // decoded: without quotes and escapes
  bool quoted = false;
  std::string raw;    // the value as it was in the file; empty once changed here
};

void AppendUtf8(std::string* out, uint32_t c) {
  if (c < 0x80) {
    *out += (char)c;
  } else if (c < 0x800) {
    *out += (char)(0xC0 | (c >> 6));
    *out += (char)(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    *out += (char)(0xE0 | (c >> 12));
    *out += (char)(0x80 | ((c >> 6) & 0x3F));
    *out += (char)(0x80 | (c & 0x3F));
  } else {
    *out += (char)(0xF0 | (c >> 18));
    *out += (char)(0x80 | ((c >> 12) & 0x3F));
    *out += (char)(0x80 | ((c >> 6) & 0x3F));
    *out += (char)(0x80 | (c & 0x3F));
  }
}

// A TOML basic string ("..."), starting at v[0]. Sets *value to the decoded
// text and returns the length of the string in v, quotes included (0 if it
// does not close on this line).
size_t ReadBasicString(const std::string& v, std::string* value) {
  value->clear();
  for (size_t i = 1; i < v.size(); ++i) {
    char c = v[i];
    if (c == '"') return i + 1;
    if (c != '\\' || i + 1 >= v.size()) {
      *value += c;
      continue;
    }
    char e = v[++i];
    switch (e) {
      case 'n': *value += '\n'; break;
      case 't': *value += '\t'; break;
      case 'r': *value += '\r'; break;
      case 'b': *value += '\b'; break;
      case 'f': *value += '\f'; break;
      case 'u':
      case 'U': {
        size_t digits = e == 'u' ? 4 : 8;
        if (i + digits >= v.size()) return 0;
        AppendUtf8(value, (uint32_t)strtoul(v.substr(i + 1, digits).c_str(), nullptr, 16));
        i += digits;
        break;
      }
      default: *value += e; break;  // \\ and \"
    }
  }
  return 0;
}

std::string QuoteBasicString(const std::string& text) {
  std::string out = "\"";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char)c;
    } else if (c < 0x20 || c == 0x7F) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04X", c);
      out += buf;
    } else {
      out += (char)c;
    }
  }
  return out + "\"";
}

class Settings {
 public:
  void Parse(const std::string& text) {
    entries_.clear();
    size_t pos = 0;
    while (pos <= text.size()) {
      size_t end = text.find('\n', pos);
      if (end == std::string::npos) end = text.size();
      std::string line = Trim(text.substr(pos, end - pos));
      pos = end + 1;
      if (line.empty() || line[0] == '#' || line[0] == '[') continue;
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      Entry e;
      e.key = Trim(line.substr(0, eq));
      std::string v = Trim(line.substr(eq + 1));
      if (!v.empty() && v[0] == '"' && v.compare(0, 3, "\"\"\"") != 0) {
        size_t length = ReadBasicString(v, &e.value);
        e.raw = length ? v.substr(0, length) : v;
        e.quoted = true;
      } else if (!v.empty() && v[0] == '\'' && v.compare(0, 3, "\'\'\'") != 0) {
        // A literal string: no escapes at all.
        size_t close = v.find('\'', 1);
        e.value = v.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        e.raw = close == std::string::npos ? v : v.substr(0, close + 1);
        e.quoted = true;
      } else {
        // A number, true/false, or something this simple reader does not
        // know (kept as it is).
        size_t hash = v.find('#');
        if (hash != std::string::npos) v = Trim(v.substr(0, hash));
        e.value = v;
        e.raw = v;
      }
      if (!e.key.empty()) {
        Set(e.key, e.value, e.quoted);
        for (Entry& stored : entries_) {
          if (stored.key == e.key) stored.raw = e.raw;
        }
      }
    }
  }

  std::string Serialize(const std::string& header) const {
    std::string out = header;
    for (const Entry& e : entries_) {
      // Unchanged settings are written back exactly as they were read.
      const std::string value = !e.raw.empty() ? e.raw : e.quoted ? QuoteBasicString(e.value) : e.value;
      out += e.key + " = " + value + "\n";
    }
    return out;
  }

  bool Has(const std::string& key) const { return Find(key) != nullptr; }
  std::string Get(const std::string& key, const std::string& fallback = "") const {
    const Entry* e = Find(key);
    return e ? e->value : fallback;
  }
  bool GetBool(const std::string& key, bool fallback) const {
    const Entry* e = Find(key);
    if (!e) return fallback;
    return e->value == "true" || e->value == "1";
  }
  double GetDouble(const std::string& key, double fallback) const {
    const Entry* e = Find(key);
    if (!e) return fallback;
    char* endp = nullptr;
    double v = strtod(e->value.c_str(), &endp);
    return endp == e->value.c_str() ? fallback : v;
  }
  int GetInt(const std::string& key, int fallback) const {
    return (int)std::lround(GetDouble(key, fallback));
  }

  void Set(const std::string& key, const std::string& value, bool quoted) {
    for (Entry& e : entries_) {
      if (e.key == key) {
        if (e.value != value || e.quoted != quoted) e.raw.clear();
        e.value = value;
        e.quoted = quoted;
        return;
      }
    }
    entries_.push_back(Entry{key, value, quoted, ""});
  }
  void SetBool(const std::string& key, bool v) { Set(key, v ? "true" : "false", false); }
  void SetInt(const std::string& key, int v) { Set(key, std::to_string(v), false); }
  void SetString(const std::string& key, const std::string& v) { Set(key, v, true); }
  void SetDouble(const std::string& key, double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", v);
    Set(key, buf, false);
  }
  void Remove(const std::string& key) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& e) { return e.key == key; }),
                   entries_.end());
  }
  const std::vector<Entry>& entries() const { return entries_; }
  // The same value as `e` (decoded value and kind), or false if not present.
  bool Matches(const Entry& e) const {
    const Entry* mine = Find(e.key);
    return mine && mine->value == e.value && mine->quoted == e.quoted;
  }

 private:
  const Entry* Find(const std::string& key) const {
    for (const Entry& e : entries_) {
      if (e.key == key) return &e;
    }
    return nullptr;
  }
  std::vector<Entry> entries_;
};

// ---------------------------------------------------------------------------
// Keyboard bindings
// ---------------------------------------------------------------------------

struct Binding {
  const char* cvar;
  const wchar_t* xbox;
  const wchar_t* playstation;
  const wchar_t* hint;
  const char* default_keys;
};

// Defaults chosen for a racing game: arrows steer, Up/Down also work the
// triggers, Space is A, Enter is Start. No Shift/Ctrl/Alt combinations: the
// game ignores plain keys while a modifier is held.
const Binding kBindings[] = {
    {"keybind_lstick_left", L"Left stick left", L"Left stick left", L"steer left", "Left,A"},
    {"keybind_lstick_right", L"Left stick right", L"Left stick right", L"steer right", "Right,D"},
    {"keybind_lstick_up", L"Left stick up", L"Left stick up", L"menu up", "Up,W"},
    {"keybind_lstick_down", L"Left stick down", L"Left stick down", L"menu down", "Down,S"},
    {"keybind_a", L"A", L"Cross", L"confirm", "Space"},
    {"keybind_b", L"B", L"Circle", L"cancel", "Backspace,B"},
    {"keybind_x", L"X", L"Square", L"", "X"},
    {"keybind_y", L"Y", L"Triangle", L"", "Y"},
    {"keybind_right_trigger", L"RT", L"R2", L"accelerate", "Up,W"},
    {"keybind_left_trigger", L"LT", L"L2", L"brake", "Down,S"},
    {"keybind_right_shoulder", L"RB", L"R1", L"", "E"},
    {"keybind_left_shoulder", L"LB", L"L1", L"", "Q"},
    {"keybind_start", L"Start", L"Options", L"pause / press start", "Return,P"},
    {"keybind_back", L"Back", L"Create / Share", L"", "Tab"},
    {"keybind_dpad_up", L"D-pad up", L"D-pad up", L"", "Numpad8"},
    {"keybind_dpad_down", L"D-pad down", L"D-pad down", L"", "Numpad2"},
    {"keybind_dpad_left", L"D-pad left", L"D-pad left", L"", "Numpad4"},
    {"keybind_dpad_right", L"D-pad right", L"D-pad right", L"", "Numpad6"},
    {"keybind_rstick_up", L"Right stick up", L"Right stick up", L"", "I"},
    {"keybind_rstick_down", L"Right stick down", L"Right stick down", L"", "K"},
    {"keybind_rstick_left", L"Right stick left", L"Right stick left", L"", "J"},
    {"keybind_rstick_right", L"Right stick right", L"Right stick right", L"", "L"},
    {"keybind_lstick_press", L"Left stick click", L"L3", L"", "N"},
    {"keybind_rstick_press", L"Right stick click", L"R3", L"", "M"},
};
const int kBindingCount = (int)(sizeof(kBindings) / sizeof(kBindings[0]));

// Windows virtual-key code -> the key name the game understands.
std::string KeyName(UINT vk) {
  if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
  if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
  if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Numpad" + std::to_string(vk - VK_NUMPAD0);
  switch (vk) {
    case VK_OEM_3: return "Backtick";
    case VK_OEM_MINUS: return "Minus";
    case VK_OEM_PLUS: return "Plus";
    case VK_OEM_COMMA: return "Comma";
    case VK_OEM_PERIOD: return "Period";
    case VK_OEM_1: return "Semicolon";
    case VK_OEM_2: return "Slash";
    case VK_OEM_5: return "Backslash";
    case VK_OEM_4: return "LBracket";
    case VK_OEM_6: return "RBracket";
    case VK_OEM_7: return "Quote";
    case VK_RETURN: return "Return";
    case VK_SPACE: return "Space";
    case VK_TAB: return "Tab";
    case VK_BACK: return "Backspace";
    case VK_DELETE: return "Delete";
    case VK_INSERT: return "Insert";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_ADD: return "NumpadPlus";
    case VK_SUBTRACT: return "NumpadMinus";
    case VK_MULTIPLY: return "NumpadStar";
    case VK_DIVIDE: return "NumpadSlash";
    case VK_CAPITAL: return "CapsLock";
    default: return "";
  }
}

// Keys the game's own overlays use; binding them would clash.
bool IsReservedKey(const std::string& name) {
  return name == "F3" || name == "F4" || name == "F7" || name == "Backtick";
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------

enum : int {
  IDC_TAB0 = 100, IDC_TAB1, IDC_TAB2, IDC_TAB3, IDC_TAB4,  // the page buttons in the banner, in page order
  IDC_SCREEN, IDC_SHAPE, IDC_HUD, IDC_SCALE, IDC_SMOOTH, IDC_ANISO, IDC_LANGUAGE, IDC_FOLIAGE,
  IDC_VSYNC,
  IDC_KEYBOARD, IDC_NAMES, IDC_BINDLIST, IDC_SETKEY, IDC_ADDKEY, IDC_CLEARKEY, IDC_RESETKEYS,
  IDC_PLAY, IDC_SAVE, IDC_DEFAULTS, IDC_LOGS, IDC_SAVES, IDC_DIAG, IDC_STATUS,
  IDC_STOP_COPY, IDC_COPY_AGAIN, IDC_ACH_LIST, IDC_ACH_SOUND,
  IDC_DLC_LIST, IDC_DLC_ADD, IDC_DLC_FOLDER, IDC_DLC_OPEN, IDC_DLC_NOTE,
};
const int kPageCount = 5;
const int kAchievementsPage = 2;
const int kDlcPage = 3;
const int kTroublePage = 4;

// Colours, taken from the game's own menus: white pages, a yellow-green
// accent, charcoal bars.
const COLORREF kPaper = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kMist = RGB(0xF1, 0xF3, 0xEE);
const COLORREF kLine = RGB(0xD5, 0xDA, 0xCE);
const COLORREF kInk = RGB(0x1E, 0x23, 0x26);
const COLORREF kSteel = RGB(0x5F, 0x68, 0x6E);

// Pictures for the banner. `hero` is a whole movie frame, `logo` the game's
// title cut out of another frame with transparency; both are 32-bit BGRA.
struct Art {
  int hero_w = 0, hero_h = 0;
  std::vector<uint8_t> hero;
  int logo_w = 0, logo_h = 0;
  std::vector<uint8_t> logo;
};

// Where the game files stand: there, still to be copied out of the player's
// disc image, or being copied right now.
enum class Setup { kReady, kNeeded, kCopying };

struct App {
  HINSTANCE instance = nullptr;
  HWND window = nullptr;
  HWND page[kPageCount] = {};
  int current_page = 0;
  std::map<int, HWND> controls;  // by control id
  std::set<HWND> muted;          // labels drawn in the quieter text colour
  HWND hot = nullptr;            // custom-drawn button under the mouse pointer
  int dpi = 96;
  int banner_h = 208;            // 96-DPI units; smaller on very small screens
  HBRUSH paper_brush = nullptr;
  HBRUSH mist_brush = nullptr;
  HFONT font = nullptr;
  HFONT bold = nullptr;
  HFONT tab_font = nullptr;
  HFONT play_font = nullptr;
  std::wstring display_face;     // typeface for the banner, the tabs and Play
  Art art;                       // from the player's own opening movie; empty until loaded
  Gdiplus::Bitmap* custom_art = nullptr;  // launcher-art.* next to the launcher, if any
  HBITMAP banner = nullptr;      // the composed banner, as large as it is shown
  HDC banner_dc = nullptr;
  Setup setup = Setup::kReady;
  disc::Progress* copy = nullptr;         // the copy in progress, if any
  double copy_fraction = 0.0;             // what the progress bar shows
  RECT copy_bar = {0, 0, 0, 0};           // where it is drawn, in the main window
  bool close_after_copy = false;          // the window was closed while copying
  std::wstring dir;         // folder the launcher is in (with trailing backslash)
  std::wstring game_exe;    // full path of rr6_recomp.exe, empty if not found
  std::wstring game_data;   // folder with default.xex
  std::wstring config_path; // rr6_recomp.toml next to the game exe
  std::wstring prefs_path;  // launcher-only choices
  Settings settings;
  Settings prefs;
  // What the launcher's controls stood for when they were last loaded or
  // saved, so that a save writes only what was changed here and keeps what the
  // game's F4 window or a text editor changed in the file meanwhile.
  Settings baseline;
  bool replace_all = false;  // "Restore default settings": write everything
  int screen_w = 1920, screen_h = 1080;
  std::vector<std::string> keys;  // current key list per binding ("Left,A")
  bool ps_names = false;
  // The game's runtime (rexruntime.dll) knows vsync_to_display: from our SDK
  // fork's v0.10.0.101 on. Older ones would ignore the setting.
  bool runtime_has_vsync = false;
} app;

std::string captured_key;  // result of the key-capture popup

double ScreenAspect() { return app.screen_h > 0 ? (double)app.screen_w / app.screen_h : 16.0 / 9.0; }
bool ScreenIsWide() { return ScreenAspect() > 1.80; }

// ---------------------------------------------------------------------------
// Locating the game
// ---------------------------------------------------------------------------

void LocateGame() {
  wchar_t path[MAX_PATH * 4];
  GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
  std::wstring full(path);
  app.dir = full.substr(0, full.find_last_of(L"\\/") + 1);

  const wchar_t* exe_candidates[] = {L"bin\\rr6_recomp.exe", L"rr6_recomp.exe",
                                     L"out\\build\\win-amd64-release\\rr6_recomp.exe"};
  for (const wchar_t* c : exe_candidates) {
    if (FileExists(app.dir + c)) {
      app.game_exe = app.dir + c;
      break;
    }
  }
  std::wstring exe_dir =
      app.game_exe.empty() ? app.dir : app.game_exe.substr(0, app.game_exe.find_last_of(L'\\') + 1);
  app.config_path = exe_dir + L"rr6_recomp.toml";
  std::string runtime;
  app.runtime_has_vsync = ReadFileUtf8(exe_dir + L"rexruntime.dll", &runtime) &&
                          runtime.find("vsync_to_display") != std::string::npos;
  app.prefs_path = exe_dir + L"rr6_launcher.ini";

  const wchar_t* data_candidates[] = {L"game", L"..\\game"};
  app.game_data = app.dir + L"game";
  for (const wchar_t* c : data_candidates) {
    if (DirExists(app.dir + c)) {
      app.game_data = app.dir + c;
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Controls <-> settings
// ---------------------------------------------------------------------------

// Layout is written in 96-DPI units and scaled to the screen's DPI.
int S(int v) { return MulDiv(v, app.dpi, 96); }

HWND Ctl(int id) {
  auto it = app.controls.find(id);
  return it == app.controls.end() ? nullptr : it->second;
}
int ComboGet(int id) { return (int)SendMessageW(Ctl(id), CB_GETCURSEL, 0, 0); }
void ComboSet(int id, int index) { SendMessageW(Ctl(id), CB_SETCURSEL, index, 0); }
bool CheckGet(int id) { return SendMessageW(Ctl(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void CheckSet(int id, bool on) { SendMessageW(Ctl(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
void SetStatus(const std::wstring& text) { SetWindowTextW(Ctl(IDC_STATUS), text.c_str()); }

void RefreshBindList() {
  HWND list = Ctl(IDC_BINDLIST);
  int selected = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
  int top = (int)SendMessageW(list, LVM_GETTOPINDEX, 0, 0);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
  for (int i = 0; i < kBindingCount; ++i) {
    // With PlayStation names, also show the Xbox name the game itself uses.
    std::wstring name = kBindings[i].xbox;
    if (app.ps_names && name != kBindings[i].playstation) {
      name = std::wstring(kBindings[i].playstation) + L"  [" + kBindings[i].xbox + L"]";
    }
    if (kBindings[i].hint[0]) name += std::wstring(L"  (") + kBindings[i].hint + L")";
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = i;
    item.pszText = const_cast<wchar_t*>(name.c_str());
    SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
    std::wstring keys = app.keys[i].empty() ? L"(none)" : Widen(app.keys[i]);
    size_t comma;
    size_t from = 0;
    while ((comma = keys.find(L',', from)) != std::wstring::npos) {
      keys.replace(comma, 1, L"  or  ");
      from = comma + 6;
    }
    LVITEMW sub = {};
    sub.mask = LVIF_TEXT;
    sub.iItem = i;
    sub.iSubItem = 1;
    sub.pszText = const_cast<wchar_t*>(keys.c_str());
    SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&sub);
  }
  if (selected >= 0) {
    LVITEMW sel = {};
    sel.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    sel.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, selected, (LPARAM)&sel);
  }
  // Keep the scroll position: show the last row first, then the old top row.
  if (top > 0) {
    SendMessageW(list, LVM_ENSUREVISIBLE, kBindingCount - 1, FALSE);
    SendMessageW(list, LVM_ENSUREVISIBLE, top, FALSE);
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
}

void UpdateEnabledStates() {
  bool windowed = ComboGet(IDC_SCREEN) == 1;
  bool fill = ComboGet(IDC_SHAPE) == 0 && !windowed && ScreenIsWide();
  EnableWindow(Ctl(IDC_SHAPE), !windowed && ScreenIsWide());
  EnableWindow(Ctl(IDC_HUD), fill);
  EnableWindow(Ctl(IDC_VSYNC), app.runtime_has_vsync);
  bool keyboard = CheckGet(IDC_KEYBOARD);
  for (int id : {IDC_BINDLIST, IDC_SETKEY, IDC_ADDKEY, IDC_CLEARKEY, IDC_RESETKEYS}) {
    EnableWindow(Ctl(id), keyboard);
  }
}

// The render height multiple a Sharpness choice stands for: 1-4 as chosen, or
// for Automatic the next whole multiple of 720 lines, except that screens only
// a little taller than a multiple (768, 900) are not worth the extra work.
int ScaleForChoice(int choice) {
  if (choice >= 1) return std::min(4, choice);
  int scale_y = (int)std::ceil(app.screen_h / 720.0 - 0.25);
  return std::min(4, std::max(1, scale_y));
}

// What the controls stand for, as the settings they would write.
void StoreInto(Settings& s, Settings& p);

void RememberBaseline() {
  Settings s = app.settings, p = app.prefs;
  StoreInto(s, p);
  app.baseline = s;
}

void LoadIntoControls() {
  const Settings& s = app.settings;
  const Settings& p = app.prefs;
  ComboSet(IDC_SCREEN, s.GetBool("fullscreen", true) ? 0 : 1);
  ComboSet(IDC_SHAPE, p.Get("picture", "fill") == "16x9" ? 1 : 0);
  ComboSet(IDC_HUD, s.GetBool("rr6_hud_edges", true) ? 0 : 1);
  std::string scale = p.Get("scale", "auto");
  int scale_index = 0;
  if (scale != "auto") scale_index = std::min(4, std::max(1, atoi(scale.c_str())));
  // The render size in the file wins over the launcher's own note of the
  // choice: it may have been changed in the game's F4 window or by hand.
  const int file_scale = s.GetInt("draw_resolution_scale_y", 0);
  if (file_scale >= 1 && file_scale <= 4 && file_scale != ScaleForChoice(scale_index)) {
    scale_index = file_scale;
  }
  ComboSet(IDC_SCALE, scale_index);
  std::string smooth = s.Get("swap_post_effect", "none");
  ComboSet(IDC_SMOOTH, smooth == "fxaa" ? 1 : smooth == "fxaa_extreme" ? 2 : 0);
  int aniso = s.GetInt("anisotropic_override", 5);
  ComboSet(IDC_ANISO, aniso <= 3 ? 0 : aniso == 4 ? 1 : 2);
  ComboSet(IDC_LANGUAGE, std::min(6, std::max(1, s.GetInt("user_language", 1))) - 1);
  CheckSet(IDC_FOLIAGE, s.GetBool("use_fuzzy_alpha_epsilon", true));
  CheckSet(IDC_VSYNC, s.GetBool("vsync_to_display", false));
  CheckSet(IDC_KEYBOARD, s.GetBool("mnk_mode", true));
  CheckSet(IDC_DIAG, p.GetBool("diagnostic", false));
  app.ps_names = p.Get("names", "xbox") == "playstation";
  ComboSet(IDC_NAMES, app.ps_names ? 1 : 0);
  app.keys.assign(kBindingCount, "");
  for (int i = 0; i < kBindingCount; ++i) {
    app.keys[i] = s.Has(kBindings[i].cvar) ? s.Get(kBindings[i].cvar) : kBindings[i].default_keys;
  }
  RefreshBindList();
  UpdateEnabledStates();
}

// Works out the settings that depend on the screen and stores everything.
void StoreInto(Settings& s, Settings& p) {
  bool windowed = ComboGet(IDC_SCREEN) == 1;
  bool want_fill = ComboGet(IDC_SHAPE) == 0;
  bool fill = want_fill && !windowed && ScreenIsWide();
  double aspect = ScreenAspect();

  s.SetBool("fullscreen", !windowed);
  p.Set("picture", want_fill ? "fill" : "16x9", false);
  if (fill) {
    s.SetDouble("rr6_aspect_ratio", aspect);
    s.SetBool("present_letterbox", false);
  } else {
    s.Set("rr6_aspect_ratio", "0", false);
    s.SetBool("present_letterbox", true);
  }
  s.SetBool("rr6_hud_fix", true);
  s.SetBool("rr6_hud_edges", ComboGet(IDC_HUD) == 0);

  // Internal render size. The game draws 1280x720; scale_y multiplies the
  // height and scale_x the width. When the picture is stretched to a wide
  // screen, the width needs proportionally more.
  int scale_choice = ComboGet(IDC_SCALE);  // 0 = auto, 1..4 = fixed
  p.Set("scale", scale_choice == 0 ? "auto" : std::to_string(scale_choice), false);
  int scale_y = scale_choice;
  if (scale_choice == 0) scale_y = ScaleForChoice(0);
  int scale_x = scale_y;
  if (fill) {
    scale_x = (int)std::ceil(scale_y * aspect / (16.0 / 9.0) - 0.01);
    scale_x = std::min(8, std::max(scale_y, scale_x));
  }
  s.Remove("resolution_scale");
  s.SetInt("draw_resolution_scale_x", scale_x);
  s.SetInt("draw_resolution_scale_y", scale_y);

  const char* smooth[] = {"none", "fxaa", "fxaa_extreme"};
  s.SetString("swap_post_effect", smooth[std::min(2, std::max(0, ComboGet(IDC_SMOOTH)))]);
  s.SetInt("anisotropic_override", 3 + std::min(2, std::max(0, ComboGet(IDC_ANISO))));
  s.SetInt("user_language", 1 + std::min(5, std::max(0, ComboGet(IDC_LANGUAGE))));
  s.SetBool("use_fuzzy_alpha_epsilon", CheckGet(IDC_FOLIAGE));
  if (app.runtime_has_vsync) s.SetBool("vsync_to_display", CheckGet(IDC_VSYNC));

  // Keyboard: the game treats it as one more controller. The mouse is left
  // alone (mnk_mouse would capture the pointer for the right stick).
  s.SetBool("mnk_mode", CheckGet(IDC_KEYBOARD));
  s.SetBool("mnk_mouse", false);
  for (int i = 0; i < kBindingCount; ++i) {
    s.SetString(kBindings[i].cvar, app.keys[i]);
  }
  // The controller database (gamecontrollerdb.txt) is found by its default
  // name in the folder the game is started from, so no path is stored here.
  s.Remove("hid_mappings_file");
  p.SetBool("diagnostic", CheckGet(IDC_DIAG));
  p.Set("names", app.ps_names ? "playstation" : "xbox", false);
}

bool SaveAll() {
  // Start from the file as it is now, not as it was when the launcher
  // started: the game's F4 window ("Save to config") or a text editor may
  // have changed it since. Of the settings the launcher looks after, only
  // those changed here are written, plus any the file does not have yet.
  Settings fresh = app.settings;
  std::string text;
  if (!app.replace_all && ReadFileUtf8(app.config_path, &text)) {
    fresh = Settings();
    fresh.Parse(text);
  }
  Settings wanted = fresh;
  StoreInto(wanted, app.prefs);
  if (app.replace_all) {
    fresh = wanted;
  } else {
    for (const Entry& e : wanted.entries()) {
      if (!fresh.Has(e.key) || !app.baseline.Matches(e)) fresh.Set(e.key, e.value, e.quoted);
    }
    for (const char* gone : {"resolution_scale", "hid_mappings_file"}) {
      if (!wanted.Has(gone)) fresh.Remove(gone);
    }
  }
  app.settings = fresh;
  app.replace_all = false;
  // Show what the file now holds (it may differ from the controls where the
  // file was changed elsewhere), and take that as the new starting point.
  LoadIntoControls();
  RememberBaseline();
  bool ok = WriteFileUtf8(
      app.config_path,
      app.settings.Serialize(
          "# Ridge Racer 6 PC settings. Written by \"RR6 Launcher.exe\"; the in-game F4\n"
          "# menu edits the same file. Delete this file to go back to defaults.\n"));
  WriteFileUtf8(app.prefs_path,
                app.prefs.Serialize("# Choices that only the launcher uses.\n"));
  SetStatus(ok ? L"Settings saved." : L"Could not write the settings file.");
  return ok;
}

void LoadDefaults() {
  app.settings = Settings();
  app.prefs = Settings();
  app.replace_all = true;
  LoadIntoControls();
  SetStatus(L"Defaults loaded. Press Save or Play to keep them.");
}


// ---------------------------------------------------------------------------
// Key capture popup
// ---------------------------------------------------------------------------

LRESULT CALLBACK CaptureProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      if (wp == VK_ESCAPE) {
        captured_key.clear();
        DestroyWindow(hwnd);
        return 0;
      }
      if (wp == VK_SHIFT || wp == VK_CONTROL || wp == VK_MENU || wp == VK_LWIN || wp == VK_RWIN) {
        SetWindowTextW(GetDlgItem(hwnd, 1),
                       L"Shift, Ctrl and Alt cannot be used on their own.\nPress another key, or Esc to cancel.");
        return 0;
      }
      std::string name = KeyName((UINT)wp);
      if (name.empty() || IsReservedKey(name)) {
        SetWindowTextW(GetDlgItem(hwnd, 1),
                       L"That key cannot be used (F3, F4, F7 and ` open the game's own menus).\n"
                       L"Press another key, or Esc to cancel.");
        return 0;
      }
      captured_key = name;
      DestroyWindow(hwnd);
      return 0;
    }
    case WM_CLOSE:
      captured_key.clear();
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostThreadMessageW(GetCurrentThreadId(), WM_APP + 1, 0, 0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// Returns the pressed key's name, or "" if cancelled.
std::string CaptureKey(const std::wstring& button_name) {
  captured_key.clear();
  RECT pr;
  GetWindowRect(app.window, &pr);
  int w = S(440), h = S(150);
  HWND popup = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"RR6KeyCapture",
                               (L"Key for " + button_name).c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               pr.left + ((pr.right - pr.left) - w) / 2,
                               pr.top + ((pr.bottom - pr.top) - h) / 2, w, h, app.window, nullptr,
                               app.instance, nullptr);
  HWND text = CreateWindowExW(0, L"STATIC", L"Press the key to use.\nEsc cancels.",
                              WS_CHILD | WS_VISIBLE | SS_CENTER, S(10), S(26), w - S(30), S(70), popup,
                              (HMENU)(INT_PTR)1, app.instance, nullptr);
  SendMessageW(text, WM_SETFONT, (WPARAM)app.font, TRUE);
  EnableWindow(app.window, FALSE);
  ShowWindow(popup, SW_SHOW);
  SetFocus(popup);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (msg.message == WM_APP + 1) break;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  EnableWindow(app.window, TRUE);
  SetForegroundWindow(app.window);
  SetFocus(Ctl(IDC_BINDLIST));
  RedrawWindow(app.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  return captured_key;
}

int SelectedBinding() {
  return (int)SendMessageW(Ctl(IDC_BINDLIST), LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

void ChangeKey(bool add) {
  int i = SelectedBinding();
  if (i < 0 || i >= kBindingCount) {
    SetStatus(L"Select a button in the list first.");
    return;
  }
  std::string key = CaptureKey(app.ps_names ? kBindings[i].playstation : kBindings[i].xbox);
  if (key.empty()) return;
  if (add && !app.keys[i].empty()) {
    if (("," + app.keys[i] + ",").find("," + key + ",") == std::string::npos) {
      app.keys[i] += "," + key;
    }
  } else {
    app.keys[i] = key;
  }
  RefreshBindList();
  SetStatus(L"Key changed. Press Save or Play to keep it.");
}

// ---------------------------------------------------------------------------
// Starting the game
// ---------------------------------------------------------------------------

// Starts a program and waits for it while keeping this window responsive.
bool RunAndWait(std::wstring command, const std::wstring& cwd, DWORD* exit_code, DWORD flags) {
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, flags, nullptr, cwd.c_str(), &si,
                      &pi)) {
    return false;
  }
  CloseHandle(pi.hThread);
  for (;;) {
    DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT);
    if (r != WAIT_OBJECT_0 + 1) break;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        PostQuitMessage((int)msg.wParam);
        CloseHandle(pi.hProcess);
        return false;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  GetExitCodeProcess(pi.hProcess, exit_code);
  CloseHandle(pi.hProcess);
  return true;
}

// Where the game keeps saves and caches: Documents\rr6_recomp. Empty if it
// cannot be worked out. A user_data_root line in the settings file does not
// move it: the SDK picks this folder before it reads that file, so only a
// --user_data_root on the command line would, and the launcher passes none.
std::wstring UserDataRoot() {
  std::wstring root;
  PWSTR documents = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) || !documents) {
    return L"";
  }
  root = std::wstring(documents) + L"\\rr6_recomp";
  CoTaskMemFree(documents);
  return root;
}

// The graphics layer stores every pipeline state it has ever built and
// rebuilds all of them at the next start. Game builds from before the
// depth-bias fix (src/depth_bias_fix.cpp) added about 770 entries per second
// of racing: 273,000 after a few short sessions. Such a file is of no use any
// more, so an oversized one is deleted; the few hundred states the game really
// needs are rebuilt on demand in the first seconds of play.
void TrimPipelineStorage(const std::wstring& root) {
  std::wstring folder = root + L"\\cache\\shaders\\shareable\\";
  WIN32_FIND_DATAW fd;
  HANDLE find = FindFirstFileW((folder + L"*.d3d12.xpso").c_str(), &fd);
  if (find == INVALID_HANDLE_VALUE) return;
  const ULONGLONG kLimit = 4ull * 1024 * 1024;
  do {
    ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    if (size > kLimit) DeleteFileW((folder + fd.cFileName).c_str());
  } while (FindNextFileW(find, &fd));
  FindClose(find);
}

// Earlier builds stored a thumbnail picture ("__thumbnail.png") inside each
// save folder. The game takes the first file it finds there as its save data,
// so when the picture was listed first it reported "Game Data is corrupted".
// Current builds no longer write the picture; this removes ones left behind.
// Layout: <root>\<profile id>\4E4D07D3\00000001\<save name>\__thumbnail.png
void RemoveSaveThumbnails(const std::wstring& root) {
  WIN32_FIND_DATAW profile;
  HANDLE profiles = FindFirstFileW((root + L"\\*").c_str(), &profile);
  if (profiles == INVALID_HANDLE_VALUE) return;
  do {
    if (!(profile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || profile.cFileName[0] == L'.') continue;
    std::wstring saves = root + L"\\" + profile.cFileName + L"\\4E4D07D3\\00000001\\";
    WIN32_FIND_DATAW save;
    HANDLE find = FindFirstFileW((saves + L"*").c_str(), &save);
    if (find == INVALID_HANDLE_VALUE) continue;
    do {
      if (!(save.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || save.cFileName[0] == L'.') continue;
      DeleteFileW((saves + save.cFileName + L"\\__thumbnail.png").c_str());
    } while (FindNextFileW(find, &save));
    FindClose(find);
  } while (FindNextFileW(profiles, &profile));
  FindClose(profiles);
}

// The game files are ready when the copy out of the disc image has finished
// (it leaves a marker file). Files put there by hand, as in the development
// tree, carry no marker: they count as ready unless this is the tester package
// (recognised by its tools folder) or a copy was started and not finished.
bool GameFilesReady() {
  if (!FileExists(app.game_data + L"\\default.xex")) return false;
  // An unfinished copy wins over everything else in the folder.
  if (FileExists(app.game_data + L"\\" + Widen(disc::kCopyingMarker))) return false;
  if (FileExists(app.game_data + L"\\" + Widen(disc::kReadyMarker))) return true;
  return !FileExists(app.dir + L"tools\\prepare-game.ps1");
}

void Play() {
  if (app.setup != Setup::kReady) return;  // the button offers the disc image instead
  if (app.game_exe.empty()) {
    MessageBoxW(app.window,
                L"rr6_recomp.exe was not found next to this launcher.\n\n"
                L"Extract the whole package again and start the launcher from that folder.",
                L"Ridge Racer 6", MB_ICONERROR);
    return;
  }
  wchar_t sys[MAX_PATH];
  GetSystemDirectoryW(sys, MAX_PATH);
  if (!FileExists(std::wstring(sys) + L"\\vcruntime140_1.dll") ||
      !FileExists(std::wstring(sys) + L"\\msvcp140_atomic_wait.dll")) {
    MessageBoxW(app.window,
                L"The Microsoft Visual C++ runtime is missing on this PC.\n\n"
                L"Install \"Visual C++ Redistributable 2015-2022 (x64)\" from\n"
                L"https://aka.ms/vs/17/release/vc_redist.x64.exe\nand press Play again.",
                L"Ridge Racer 6", MB_ICONWARNING);
    return;
  }
  if (waveOutGetNumDevs() == 0) {
    int choice = MessageBoxW(
        app.window,
        L"Windows reports no sound output device.\n\n"
        L"The game needs one and is likely to close right after starting without it. Plug in "
        L"headphones or speakers (or enable an output device in the Windows sound settings), then "
        L"press Play again.\n\nStart the game anyway?",
        L"Ridge Racer 6", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (choice != IDYES) return;
  }
  if (!SaveAll()) {
    MessageBoxW(app.window,
                L"The settings file could not be written. Is the folder read-only, or still inside "
                L"the zip? Extract the package to a normal folder first.",
                L"Ridge Racer 6", MB_ICONERROR);
    return;
  }
  CreateDirectoryW((app.dir + L"logs").c_str(), nullptr);

  if (!GameFilesReady()) {
    MessageBoxW(app.window, (L"The game files were not found in:\n" + app.game_data).c_str(),
                L"Ridge Racer 6", MB_ICONERROR);
    return;
  }

  std::wstring user_data = UserDataRoot();
  if (!user_data.empty()) {
    TrimPipelineStorage(user_data);
    RemoveSaveThumbnails(user_data);
  }

  bool diagnostic = CheckGet(IDC_DIAG);
  std::wstring log = app.dir + (diagnostic ? L"logs\\run-debug.log" : L"logs\\run.log");
  std::wstring cmd = L"\"" + app.game_exe + L"\" --game_data_root \"" + app.game_data +
                     L"\" --gpu_plugin=xenos --log_file \"" + log + L"\"";
  if (diagnostic) {
    cmd += L" --log_level debug --log_flush_interval 1 --log_max_file_size_mb 50 --log_max_files 3";
  }

  // Stay in the background while the game runs, so a crash can be reported
  // instead of the game just vanishing.
  SetStatus(L"The game is running.");
  ShowWindow(app.window, SW_HIDE);
  DWORD code = 0;
  bool ran = RunAndWait(cmd, app.dir, &code, 0);
  if (!IsWindow(app.window)) return;
  if (!ran) {
    ShowWindow(app.window, SW_SHOW);
    SetStatus(L"The game could not be started.");
    MessageBoxW(app.window, L"The game could not be started.", L"Ridge Racer 6", MB_ICONERROR);
    return;
  }
  char line[96];
  snprintf(line, sizeof(line), "exit code %lu (0x%08lX)\r\n", (unsigned long)code, (unsigned long)code);
  WriteFileUtf8(app.dir + L"logs\\last-exit.txt", line);

  // After a crash, or whenever the detailed log was asked for, pack the logs,
  // Windows' crash record and basic PC details into bug-report.zip.
  bool report = false;
  std::wstring report_path = app.dir + L"bug-report.zip";
  std::wstring collect = app.dir + L"tools\\collect-report.ps1";
  if ((diagnostic || code != 0) && FileExists(collect)) {
    DeleteFileW(report_path.c_str());
    DWORD rc = 1;
    std::wstring c = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + collect + L"\"";
    report = RunAndWait(c, app.dir, &rc, CREATE_NO_WINDOW) && FileExists(report_path);
    if (!IsWindow(app.window)) return;
  }
  if (code == 0 && !report) {
    DestroyWindow(app.window);
    return;
  }

  ShowWindow(app.window, SW_SHOW);
  SetForegroundWindow(app.window);
  std::wstring text;
  if (code == 0) {
    SetStatus(L"bug-report.zip was saved in the game folder.");
    text = L"The detailed log was packed into bug-report.zip in the game folder.\n\n"
           L"If you are reporting a problem, send that file together with a short description of "
           L"what you were doing and what went wrong.";
  } else {
    wchar_t head[160];
    swprintf(head, 160, L"The game closed unexpectedly (code 0x%08lX).", (unsigned long)code);
    SetStatus(head);
    text = head;
    if (code == 0xC0000135) {
      text += L"\n\nWindows could not find a file the game needs. Extract the whole package again "
              L"and check that the Visual C++ runtime is installed.";
    }
    if (report) {
      text += L"\n\nA file named bug-report.zip was saved in the game folder. Please send it "
              L"together with a short description of what you were doing.";
      if (!diagnostic) {
        text += L"\n\nIf it happens again, tick \"Record a detailed log\" on the Troubleshooting "
                L"tab before pressing Play: the report then says much more.";
      }
    } else if (diagnostic) {
      text += L"\n\nThe detailed log is in the logs folder (Troubleshooting tab, \"Open logs "
              L"folder\"). Please send everything in it together with a short description of what "
              L"you were doing.";
    } else {
      text += L"\n\nTo help find the cause: on the Troubleshooting tab, tick \"Record a detailed "
              L"log\", press Play, and when it happens again send everything in the logs folder "
              L"(the \"Open logs folder\" button on the same tab).";
    }
  }
  MessageBoxW(app.window, text.c_str(), L"Ridge Racer 6", code == 0 ? MB_ICONINFORMATION : MB_ICONWARNING);
  if (report) {
    std::wstring args = L"/select,\"" + report_path + L"\"";
    ShellExecuteW(app.window, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
  }
}

// ---------------------------------------------------------------------------
// Banner pictures
// ---------------------------------------------------------------------------
//
// Nothing of the game's artwork is built into this program. The banner shows,
// in this order of preference:
//   1. launcher-art.png / .jpg / .jpeg / .bmp next to the launcher (the
//      player's own picture, for example a scan of their game's cover);
//   2. a still from the opening movie of the player's own copy of the game,
//      with the game's title taken from a later frame of the same movie;
//   3. a drawn banner in the colours of the game's menus.
// The movie stills are decoded once and kept in the game folder
// (.rr6-launcher-art), next to the files they were made from.

const UINT WM_APP_ART = WM_APP + 2;

// opening.sfd of the USA disc. The picture numbers below only mean something
// for exactly this file, so any other file is left alone.
const ULONGLONG kOpeningMovieSize = 193267712ull;
const int kHeroPicture = 352;   // night highway, light trails, a car on the right
const int kLogoPicture = 1352;  // the title on a nearly black background
const double kHeroFocusX = 0.5, kHeroFocusY = 0.615;

const char kArtMagic[8] = {'R', 'R', '6', 'A', 'R', 'T', '1', 0};
struct ArtHeader {
  char magic[8];
  int32_t hero_w, hero_h, logo_w, logo_h;
};

bool art_loading = false;

std::wstring MoviePath() { return app.game_data + L"\\opening.sfd"; }
std::wstring ArtCachePath() { return app.game_data + L"\\.rr6-launcher-art"; }

bool FileSizeOf(const std::wstring& path, ULONGLONG* size) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
  if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
  *size = ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
  return true;
}

bool ReadArtCache(const std::wstring& path, Art* art) {
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return false;
  ArtHeader h;
  bool ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, kArtMagic, sizeof(kArtMagic)) &&
            h.hero_w >= 64 && h.hero_w <= 4096 && h.hero_h >= 64 && h.hero_h <= 4096 && h.logo_w >= 0 &&
            h.logo_w <= 4096 && h.logo_h >= 0 && h.logo_h <= 1024;
  if (ok) {
    art->hero_w = h.hero_w;
    art->hero_h = h.hero_h;
    art->logo_w = h.logo_w;
    art->logo_h = h.logo_h;
    art->hero.resize((size_t)h.hero_w * h.hero_h * 4);
    art->logo.resize((size_t)h.logo_w * h.logo_h * 4);
    ok = fread(art->hero.data(), 1, art->hero.size(), f) == art->hero.size() &&
         (art->logo.empty() || fread(art->logo.data(), 1, art->logo.size(), f) == art->logo.size());
  }
  fclose(f);
  if (!ok) *art = Art();
  return ok;
}

void WriteArtCache(const std::wstring& path, const Art& art) {
  std::wstring temp = path + L".tmp";
  FILE* f = _wfopen(temp.c_str(), L"wb");
  if (!f) return;
  ArtHeader h;
  memcpy(h.magic, kArtMagic, sizeof(kArtMagic));
  h.hero_w = art.hero_w;
  h.hero_h = art.hero_h;
  h.logo_w = art.logo_w;
  h.logo_h = art.logo_h;
  bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
            fwrite(art.hero.data(), 1, art.hero.size(), f) == art.hero.size() &&
            (art.logo.empty() || fwrite(art.logo.data(), 1, art.logo.size(), f) == art.logo.size());
  ok = fclose(f) == 0 && ok;
  if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(temp.c_str());
  }
}

// Cuts the title out of its movie frame. The frame is nearly black around the
// lettering, so "how red or how bright" serves as the transparency.
void MakeLogo(const Rr6Still& frame, Art* art) {
  if (frame.width != 1280 || frame.height != 720 || !frame.bgra) return;
  const int x0 = 216, x1 = 1088, y0 = 320, y1 = 400, feather = 10;
  const int w = x1 - x0, h = y1 - y0;
  std::vector<uint8_t> logo((size_t)w * h * 4);
  size_t solid = 0;
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = frame.bgra + ((size_t)(y + y0) * frame.width + x0) * 4;
    uint8_t* dst = logo.data() + (size_t)y * w * 4;
    double edge_y = std::min(1.0, std::min(y, h - 1 - y) / (double)feather);
    for (int x = 0; x < w; ++x, src += 4, dst += 4) {
      const int b = src[0], g = src[1], r = src[2];
      double red = std::min(1.0, std::max(0.0, (r - b - 16) / 48.0));
      double luma = 0.299 * r + 0.587 * g + 0.114 * b;
      double bright = std::min(1.0, std::max(0.0, (luma - 150.0) / 60.0));
      double edge_x = std::min(1.0, std::min(x, w - 1 - x) / (double)feather);
      double alpha = std::max(red, bright) * edge_x * edge_y;
      if (alpha > 0.5) ++solid;
      dst[0] = (uint8_t)b;
      dst[1] = (uint8_t)g;
      dst[2] = (uint8_t)r;
      dst[3] = (uint8_t)(alpha * 255.0 + 0.5);
    }
  }
  // The lettering covers about a third of this rectangle. Anything far from
  // that is not the frame this was written for.
  double coverage = (double)solid / ((double)w * h);
  if (coverage < 0.12 || coverage > 0.60) return;
  art->logo_w = w;
  art->logo_h = h;
  art->logo.swap(logo);
}

struct ArtJob {
  HWND window;
  std::wstring movie;
  std::wstring cache;
};

DWORD WINAPI ArtThread(LPVOID param) {
  ArtJob* job = (ArtJob*)param;
  Art* art = new Art;
  FILE* f = _wfopen(job->movie.c_str(), L"rb");
  if (f) {
    const int pictures[2] = {kHeroPicture, kLogoPicture};
    Rr6Still stills[2];
    rr6_movie_stills(f, pictures, 2, stills);
    fclose(f);
    if (stills[0].bgra && stills[0].width >= 64 && stills[0].height >= 64) {
      art->hero_w = stills[0].width;
      art->hero_h = stills[0].height;
      art->hero.assign(stills[0].bgra, stills[0].bgra + (size_t)stills[0].width * stills[0].height * 4);
      MakeLogo(stills[1], art);
    }
    rr6_movie_free(&stills[0]);
    rr6_movie_free(&stills[1]);
  }
  if (art->hero.empty()) {
    delete art;
    art = nullptr;
  } else {
    WriteArtCache(job->cache, *art);
  }
  if (!PostMessageW(job->window, WM_APP_ART, 0, (LPARAM)art)) delete art;
  delete job;
  return 0;
}

// Starts decoding the movie stills unless a picture is already there. The
// result arrives as WM_APP_ART.
void LoadArtInBackground() {
  if (art_loading || app.custom_art || !app.art.hero.empty() || !app.window) return;
  ULONGLONG size = 0;
  if (!FileSizeOf(MoviePath(), &size) || size != kOpeningMovieSize) return;
  ArtJob* job = new ArtJob{app.window, MoviePath(), ArtCachePath()};
  HANDLE thread = CreateThread(nullptr, 0, ArtThread, job, 0, nullptr);
  if (!thread) {
    delete job;
    return;
  }
  SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
  CloseHandle(thread);
  art_loading = true;
}

// Called once at start, before the window is shown: uses what is available
// immediately and leaves the slow part to LoadArtInBackground().
void LoadArtAtStart() {
  for (const wchar_t* name : {L"launcher-art.png", L"launcher-art.jpg", L"launcher-art.jpeg", L"launcher-art.bmp"}) {
    if (!FileExists(app.dir + name)) continue;
    Gdiplus::Bitmap* picture = Gdiplus::Bitmap::FromFile((app.dir + name).c_str());
    if (picture && picture->GetLastStatus() == Gdiplus::Ok && picture->GetWidth() >= 64 &&
        picture->GetHeight() >= 64) {
      app.custom_art = picture;
      return;
    }
    delete picture;
  }
  ULONGLONG size = 0;
  if (FileSizeOf(MoviePath(), &size) && size == kOpeningMovieSize) ReadArtCache(ArtCachePath(), &app.art);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

const int kWidth = 720;      // client width, 96-DPI units
const int kStrip = 40;       // the dark strip with the page buttons, at the banner's foot
const int kPageHeight = 352;
const int kBarHeight = 76;

Gdiplus::Color Rgb(COLORREF c, BYTE alpha = 255) {
  return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}
const COLORREF kLime = RGB(0xBF, 0xF5, 0x2E);
const COLORREF kLimeHot = RGB(0xD0, 0xFA, 0x58);
const COLORREF kLimeDown = RGB(0xA6, 0xDB, 0x1A);
const COLORREF kGreen = RGB(0x4E, 0x8A, 0x00);
const COLORREF kStripColor = RGB(0x16, 0x19, 0x1D);

// A flat-topped hexagon stretched to w x h: the shape of the highlights in the
// game's menus.
void AddHexagon(Gdiplus::GraphicsPath* path, float x, float y, float w, float h) {
  float c = std::min(h * 0.2887f, w / 2);
  Gdiplus::PointF p[6] = {{x + c, y},     {x + w - c, y}, {x + w, y + h / 2},
                          {x + w - c, y + h}, {x + c, y + h}, {x, y + h / 2}};
  path->AddPolygon(p, 6);
}

void AddRoundRect(Gdiplus::GraphicsPath* path, float x, float y, float w, float h, float r) {
  float d = r * 2;
  path->AddArc(x, y, d, d, 180, 90);
  path->AddArc(x + w - d, y, d, d, 270, 90);
  path->AddArc(x + w - d, y + h - d, d, d, 0, 90);
  path->AddArc(x, y + h - d, d, d, 90, 90);
  path->CloseFigure();
}

// Scales and crops `image` so it fills w x h, keeping the point (fx, fy) of
// the picture as close to the middle as the picture's edges allow.
void DrawCover(Gdiplus::Graphics* g, Gdiplus::Image* image, int w, int h, double fx, double fy) {
  double iw = image->GetWidth(), ih = image->GetHeight();
  if (iw < 1 || ih < 1) return;
  double scale = std::max(w / iw, h / ih);
  double cw = w / scale, ch = h / scale;
  double cx = std::min(std::max(fx * iw - cw / 2, 0.0), iw - cw);
  double cy = std::min(std::max(fy * ih - ch / 2, 0.0), ih - ch);
  Gdiplus::ImageAttributes attributes;
  attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
  g->DrawImage(image, Gdiplus::RectF(0, 0, (float)w, (float)h), (float)cx, (float)cy, (float)cw, (float)ch,
               Gdiplus::UnitPixel, &attributes);
}

// The banner shown before any picture is available (every first start, since
// the game files have not been copied out of the disc image yet).
void DrawPlainBanner(Gdiplus::Graphics* g, int w, int h) {
  Gdiplus::LinearGradientBrush ground(Gdiplus::Point(0, 0), Gdiplus::Point(0, h),
                                      Gdiplus::Color(255, 0x2A, 0x31, 0x37), Gdiplus::Color(255, 0x15, 0x18, 0x1C));
  g->FillRectangle(&ground, 0, 0, w, h);

  // Honeycomb, as behind the game's menus; a few cells lit.
  const float r = (float)S(21);
  const float dx = r * 1.5f, dy = r * 1.7320508f;
  Gdiplus::Pen cell(Gdiplus::Color(20, 255, 255, 255), std::max(1.0f, S(1) * 1.0f));
  for (int col = -1; col * dx < w + r; ++col) {
    for (int row = -1; row * dy < h + dy; ++row) {
      float cx = col * dx, cy = row * dy + ((col & 1) ? dy / 2 : 0);
      Gdiplus::GraphicsPath hex;
      AddHexagon(&hex, cx - r, cy - dy / 2, r * 2, dy);
      unsigned pick = (unsigned)(col * 7 + row * 13 + 40) % 9u;
      if (cx > w * 0.42f && pick == 0) {
        Gdiplus::SolidBrush lit(Rgb(kLime, (BYTE)(28 + ((unsigned)(col * 5 + row * 3 + 40) % 4u) * 22)));
        g->FillPath(&lit, &hex);
      }
      g->DrawPath(&cell, &hex);
    }
  }
  // Calm ground on the left, for the title.
  Gdiplus::LinearGradientBrush calm(Gdiplus::Point(0, 0), Gdiplus::Point((int)(w * 0.56), 0),
                                    Gdiplus::Color(255, 0x1F, 0x24, 0x29), Gdiplus::Color(0, 0x1F, 0x24, 0x29));
  g->FillRectangle(&calm, 0, 0, (int)(w * 0.56) - 1, h);

  // Slanted bands, as on the game's title screen.
  struct Band {
    float left, width;
    BYTE alpha;
  };
  const float slant = h * 0.62f;
  const Band bands[] = {{0.845f * w, (float)S(58), 255},
                        {0.845f * w + S(72), (float)S(16), 150},
                        {0.845f * w - S(26), (float)S(7), 110}};
  for (const Band& band : bands) {
    Gdiplus::PointF p[4] = {{band.left, 0},
                            {band.left + band.width, 0},
                            {band.left + band.width - slant, (float)h},
                            {band.left - slant, (float)h}};
    Gdiplus::SolidBrush brush(Rgb(kLime, band.alpha));
    g->FillPolygon(&brush, p, 4);
  }
}

void ComposeBanner() {
  const int w = S(kWidth), h = S(app.banner_h), strip = S(kStrip);
  Gdiplus::Bitmap canvas(w, h, PixelFormat32bppPARGB);
  {
    Gdiplus::Graphics g(&canvas);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    const bool movie = !app.custom_art && !app.art.hero.empty();
    if (app.custom_art) {
      DrawCover(&g, app.custom_art, w, h, 0.5, 0.5);
    } else if (movie) {
      Gdiplus::Bitmap hero(app.art.hero_w, app.art.hero_h, app.art.hero_w * 4, PixelFormat32bppRGB,
                           app.art.hero.data());
      DrawCover(&g, &hero, w, h, kHeroFocusX, kHeroFocusY);
    } else {
      DrawPlainBanner(&g, w, h);
    }

    Gdiplus::SolidBrush strip_brush(Rgb(kStripColor, 232));
    g.FillRectangle(&strip_brush, 0, h - strip, w, strip);

    Gdiplus::FontFamily family(app.display_face.c_str());
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);

    // The game's title: from the movie when there is one, plain lettering
    // otherwise. The player's own picture is left as it is.
    if (movie && !app.art.logo.empty()) {
      Gdiplus::Bitmap logo(app.art.logo_w, app.art.logo_h, app.art.logo_w * 4, PixelFormat32bppARGB,
                           app.art.logo.data());
      float lw = (float)S(330);
      float lh = lw * app.art.logo_h / app.art.logo_w;
      // The lettering starts 22 source pixels into the cut-out.
      float lx = S(24) - lw * 22.0f / app.art.logo_w;
      float ly = h - strip - lh - S(6);
      if (ly >= 0) g.DrawImage(&logo, Gdiplus::RectF(lx, ly, lw, lh));
    } else if (!app.custom_art) {
      Gdiplus::Font title(&family, (float)S(30), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
      Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
      float ty = (float)(h - strip - S(30) - S(22));
      if (ty >= S(4)) g.DrawString(L"Ridge Racer 6", -1, &title, Gdiplus::PointF((float)S(24), ty), &format, &white);
    }

    Gdiplus::Font small(&family, (float)S(13), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush quiet(Gdiplus::Color(255, 0x9A, 0xA3, 0xA9));
    const wchar_t* caption = L"Unofficial PC build";
    Gdiplus::RectF bounds;
    g.MeasureString(caption, -1, &small, Gdiplus::PointF(0, 0), &format, &bounds);
    g.DrawString(caption, -1, &small,
                 Gdiplus::PointF(w - S(24) - bounds.Width, h - strip + (strip - bounds.Height) / 2), &format,
                 &quiet);
  }

  HBITMAP bitmap = nullptr;
  if (canvas.GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &bitmap) != Gdiplus::Ok || !bitmap) return;
  if (!app.banner_dc) {
    HDC screen = GetDC(nullptr);
    app.banner_dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
  }
  SelectObject(app.banner_dc, bitmap);  // the first time, this puts the DC's stock bitmap aside
  if (app.banner) DeleteObject(app.banner);
  app.banner = bitmap;
  if (app.window) RedrawWindow(app.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

bool IsTab(int id) { return id >= IDC_TAB0 && id < IDC_TAB0 + kPageCount; }

void FillSolid(HDC dc, const RECT& rc, COLORREF color) {
  SetBkColor(dc, color);
  ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, L"", 0, nullptr);
}

// Draws one of the custom buttons: a page button in the banner, Play, or an
// ordinary button.
void DrawButton(const DRAWITEMSTRUCT* item) {
  const int id = (int)item->CtlID;
  const HWND button = item->hwndItem;
  const int w = item->rcItem.right - item->rcItem.left, h = item->rcItem.bottom - item->rcItem.top;
  if (w <= 0 || h <= 0) return;
  const bool pressed = (item->itemState & ODS_SELECTED) != 0;
  const bool disabled = (item->itemState & ODS_DISABLED) != 0;
  const bool focus = (item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT);
  const bool hot = app.hot == button && !disabled;
  wchar_t text[96];
  GetWindowTextW(button, text, 96);

  HDC dc = CreateCompatibleDC(item->hDC);
  HBITMAP surface = CreateCompatibleBitmap(item->hDC, w, h);
  HGDIOBJ old_bitmap = SelectObject(dc, surface);
  RECT rc = {0, 0, w, h};
  SetBkMode(dc, TRANSPARENT);

  if (IsTab(id)) {
    POINT origin = {0, 0};
    MapWindowPoints(button, app.window, &origin, 1);
    if (app.banner_dc) {
      BitBlt(dc, 0, 0, w, h, app.banner_dc, origin.x, origin.y, SRCCOPY);
    } else {
      FillSolid(dc, rc, kStripColor);
    }
    const bool active = id - IDC_TAB0 == app.current_page;
    HGDIOBJ old_font = SelectObject(dc, app.tab_font);
    SetTextColor(dc, active ? RGB(255, 255, 255) : hot ? RGB(0xE6, 0xEA, 0xEC) : RGB(0xA9, 0xB1, 0xB6));
    SIZE size = {0, 0};
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    DrawTextW(dc, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (active) {
      RECT mark = {(w - size.cx) / 2, h - S(4), (w + size.cx) / 2, h - S(1)};
      FillSolid(dc, mark, kLime);
    }
    if (focus) {
      RECT ring = {S(5), S(8), w - S(5), h - S(8)};
      HBRUSH brush = CreateSolidBrush(kLime);
      FrameRect(dc, &ring, brush);
      DeleteObject(brush);
    }
    SelectObject(dc, old_font);
  } else {
    FillSolid(dc, rc, GetParent(button) == app.window ? kMist : kPaper);
    const bool play = id == IDC_PLAY;
    {
      // Drawn on a GDI+ bitmap first: shapes drawn straight onto a window's
      // device context are not always smoothed.
      Gdiplus::Bitmap shape_layer(w, h, PixelFormat32bppPARGB);
      {
        // Coordinates are pixel centres here (GDI+'s default), so filled shapes
        // start half a pixel out and one-pixel lines sit on whole numbers.
        Gdiplus::Graphics g(&shape_layer);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Rgb(GetParent(button) == app.window ? kMist : kPaper));
        if (play) {
          Gdiplus::GraphicsPath shape;
          AddHexagon(&shape, -0.5f, -0.5f, (float)w, (float)h);
          Gdiplus::SolidBrush fill(Rgb(disabled ? kLine : pressed ? kLimeDown : hot ? kLimeHot : kLime));
          g.FillPath(&fill, &shape);
          if (focus) {
            float inset = (float)S(4);
            Gdiplus::GraphicsPath ring;
            AddHexagon(&ring, inset * 1.6f, inset, w - 1 - inset * 3.2f, h - 1 - inset * 2);
            Gdiplus::Pen pen(Rgb(kInk), (float)std::max(1, S(1)));
            g.DrawPath(&pen, &ring);
          }
        } else {
          float line = (float)std::max(1, S(1));
          if (focus) line *= 2;
          float inset = (line - 1) / 2;
          Gdiplus::GraphicsPath shape;
          AddRoundRect(&shape, inset, inset, w - 1 - inset * 2, h - 1 - inset * 2, (float)S(3));
          Gdiplus::SolidBrush fill(Rgb(pressed ? RGB(0xE4, 0xF4, 0xB8) : hot ? RGB(0xF4, 0xFB, 0xDD) : kPaper));
          g.FillPath(&fill, &shape);
          Gdiplus::Pen pen(Rgb(disabled ? kLine : (focus || hot || pressed) ? kGreen : RGB(0xA9, 0xB1, 0xA4)), line);
          g.DrawPath(&pen, &shape);
        }
      }
      Gdiplus::Graphics target(dc);
      target.DrawImage(&shape_layer, 0, 0, w, h);
    }
    HGDIOBJ old_font = SelectObject(dc, play ? app.play_font : app.font);
    SetTextColor(dc, disabled ? RGB(0x9C, 0xA4, 0xA9) : kInk);
    RECT label = rc;
    if (pressed) OffsetRect(&label, 0, S(1));
    DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(dc, old_font);
  }

  BitBlt(item->hDC, item->rcItem.left, item->rcItem.top, w, h, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old_bitmap);
  DeleteObject(surface);
  DeleteDC(dc);
}

// Custom-drawn buttons need a little help: tracking the mouse for the hover
// look, and keeping their style when the dialog keyboard handling moves the
// "default button" mark around (which would turn them back into plain ones).
LRESULT CALLBACK ButtonProc(HWND button, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclass_id, DWORD_PTR) {
  switch (msg) {
    case WM_MOUSEMOVE:
      if (app.hot != button) {
        HWND previous = app.hot;
        app.hot = button;
        if (previous) InvalidateRect(previous, nullptr, FALSE);
        InvalidateRect(button, nullptr, FALSE);
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, button, 0};
        TrackMouseEvent(&track);
      }
      break;
    case WM_MOUSELEAVE:
      if (app.hot == button) {
        app.hot = nullptr;
        InvalidateRect(button, nullptr, FALSE);
      }
      break;
    case WM_LBUTTONDBLCLK:
      msg = WM_LBUTTONDOWN;  // a fast second click is a click
      break;
    case WM_GETDLGCODE:
      return DLGC_BUTTON |
             (GetDlgCtrlID(button) == IDC_PLAY ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON);
    case BM_SETSTYLE:
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_NCDESTROY:
      if (app.hot == button) app.hot = nullptr;
      RemoveWindowSubclass(button, ButtonProc, subclass_id);
      break;
  }
  return DefSubclassProc(button, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Game files: copying them out of the player's disc image
// ---------------------------------------------------------------------------
//
// The first time, the big button reads "Choose disc image..." instead of
// "Play": the player points at their .iso wherever it is, and the game files
// are copied out of it into the "game" folder next to the launcher, with a
// progress bar in the bottom bar. The image itself is only read, and is not
// needed again afterwards.

const UINT WM_APP_COPY_DONE = WM_APP + 3;
const UINT_PTR kCopyTimer = 1;

// SHA-256 of default.xex on the USA disc, the only one this build matches.
const char kGameExecutableSha256[] = "39D3C0004EC62AEB0FE3E7E1889CF25D98FBD27987B6BC6B5FF30A56FFBA6C00";

std::wstring Gigabytes(uint64_t bytes) {
  wchar_t text[32];
  swprintf(text, 32, L"%.1f", (double)bytes / (1024.0 * 1024.0 * 1024.0));
  return text;
}

// Sets up the bottom bar for the current state: which buttons are there, what
// the big one says, how much room the status text has.
void LayoutBar() {
  const int bar_y = app.banner_h + kPageHeight;
  const bool copying = app.setup == Setup::kCopying;
  const int right = S(kWidth - 24);
  const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;

  HWND play = Ctl(IDC_PLAY);
  const wchar_t* label = app.setup == Setup::kReady ? L"Play" : L"Choose disc image...";
  SetWindowTextW(play, label);
  HDC dc = GetDC(app.window);
  HGDIOBJ old_font = SelectObject(dc, app.play_font);
  SIZE size = {0, 0};
  GetTextExtentPoint32W(dc, label, (int)wcslen(label), &size);
  SelectObject(dc, old_font);
  ReleaseDC(app.window, dc);
  const int play_w = std::max(S(134), (int)size.cx + S(64));
  const int play_x = right - play_w;
  const int save_x = play_x - S(12) - S(88);
  SetWindowPos(play, nullptr, play_x, S(bar_y + 14), play_w, S(48), flags);
  SetWindowPos(Ctl(IDC_SAVE), nullptr, save_x, S(bar_y + 22), S(88), S(32), flags);
  SetWindowPos(Ctl(IDC_STOP_COPY), nullptr, right - S(88), S(bar_y + 22), S(88), S(32), flags);
  ShowWindow(play, copying ? SW_HIDE : SW_SHOW);
  ShowWindow(Ctl(IDC_SAVE), copying ? SW_HIDE : SW_SHOW);
  ShowWindow(Ctl(IDC_STOP_COPY), copying ? SW_SHOW : SW_HIDE);
  EnableWindow(Ctl(IDC_COPY_AGAIN), !copying);

  const int status_right = copying ? right - S(88) - S(20) : save_x - S(16);
  SetWindowPos(Ctl(IDC_STATUS), nullptr, S(24), S(bar_y + 14), status_right - S(24), copying ? S(20) : S(50),
               flags);
  app.copy_bar = {S(24), S(bar_y + 44), status_right, S(bar_y + 44) + S(8)};
  RECT bar = {0, S(bar_y), S(kWidth), S(bar_y + kBarHeight)};
  RedrawWindow(app.window, &bar, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// Looks at the game folder again and shows the matching state.
void RefreshSetup() {
  app.setup = GameFilesReady() ? Setup::kReady : Setup::kNeeded;
  LayoutBar();
  if (app.game_exe.empty()) {
    SetStatus(L"The game (rr6_recomp.exe) was not found next to the launcher.");
  } else if (app.setup == Setup::kNeeded) {
    SetStatus(L"The game files are not here yet. Choose your Ridge Racer 6 disc image (.iso) and they "
              L"are copied out of it: once, about 6 GB, a few minutes.");
  }
}

// Asks for the disc image. Returns its path, or "" if the player backed out.
std::wstring PickDiscImage() {
  wchar_t file[2048] = L"";
  // An image already in PUT-ISO-HERE (the other way of supplying one, which
  // "Play without the launcher.bat" uses) is offered first.
  std::wstring folder = app.dir + L"PUT-ISO-HERE";
  WIN32_FIND_DATAW found;
  HANDLE find = FindFirstFileW((folder + L"\\*.iso").c_str(), &found);
  if (find != INVALID_HANDLE_VALUE) {
    FindClose(find);
    wcsncpy(file, found.cFileName, 2047);
  } else {
    folder.clear();
  }
  OPENFILENAMEW dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = app.window;
  dialog.lpstrFilter = L"Disc images (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
  dialog.lpstrFile = file;
  dialog.nMaxFile = 2048;
  dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
  dialog.lpstrTitle = L"Choose your Ridge Racer 6 disc image";
  dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) return L"";
  return file;
}

struct CopyJob {
  HWND window;
  disc::Options options;
  disc::Progress* progress;
};

DWORD WINAPI CopyThread(LPVOID param) {
  CopyJob* job = (CopyJob*)param;
  std::string* detail = new std::string;
  disc::Result result = disc::Extract(job->options, job->progress, detail);
  if (!PostMessageW(job->window, WM_APP_COPY_DONE, (WPARAM)result, (LPARAM)detail)) delete detail;
  delete job;
  return 0;
}

// Lets the player pick the image and starts copying. With `everything`, files
// that are already in the game folder are copied again as well.
void ChooseDiscImage(bool everything) {
  if (app.setup == Setup::kCopying) return;
  std::wstring image = PickDiscImage();
  if (image.empty()) return;

  app.copy = new disc::Progress;
  CopyJob* job = new CopyJob;
  job->window = app.window;
  job->options.image = image;
  job->options.out = app.game_data;
  job->options.expected_sha256 = kGameExecutableSha256;
  job->options.overwrite = everything;
  job->progress = app.copy;
  HANDLE thread = CreateThread(nullptr, 0, CopyThread, job, 0, nullptr);
  if (!thread) {
    delete job;
    delete app.copy;
    app.copy = nullptr;
    SetStatus(L"The copy could not be started.");
    return;
  }
  CloseHandle(thread);
  app.setup = Setup::kCopying;
  app.copy_fraction = 0.0;
  LayoutBar();
  SetStatus(L"Reading the disc image...");
  SetTimer(app.window, kCopyTimer, 200, nullptr);
  SetFocus(Ctl(IDC_STOP_COPY));
}

// Five times a second while copying: the status line and the progress bar.
void ShowCopyProgress() {
  if (app.setup != Setup::kCopying || !app.copy) return;
  const uint64_t total = app.copy->total, done = app.copy->done;
  if (total == 0 || app.copy->cancel) return;
  std::wstring text = L"Copying game files: " + Gigabytes(done) + L" of " + Gigabytes(total) + L" GB";
  const std::string file = app.copy->File();
  if (!file.empty()) text += L"  (" + Widen(file) + L")";
  wchar_t shown[256] = L"";
  GetWindowTextW(Ctl(IDC_STATUS), shown, 256);
  if (text != shown) SetStatus(text);
  app.copy_fraction = (double)done / (double)total;
  InvalidateRect(app.window, &app.copy_bar, FALSE);
}

void StopCopy() {
  if (app.setup != Setup::kCopying || !app.copy) return;
  app.copy->cancel = true;
  SetStatus(L"Stopping...");
}

// The copying thread has finished: `result` says how.
void CopyFinished(disc::Result result, const std::string& detail) {
  KillTimer(app.window, kCopyTimer);
  delete app.copy;
  app.copy = nullptr;
  if (app.close_after_copy) {
    DestroyWindow(app.window);
    return;
  }
  RefreshSetup();

  std::wstring problem;
  switch (result) {
    case disc::Result::kOk:
      SetStatus(L"The game files are ready. Press Play.");
      LoadArtInBackground();  // the opening movie is there now
      SetFocus(Ctl(IDC_PLAY));
      return;
    case disc::Result::kCancelled:
      SetStatus(L"Copying stopped. Choose the disc image again to carry on from where it stopped.");
      return;
    case disc::Result::kCannotOpen:
      problem = L"The file could not be opened. Is it still being downloaded or used by another program?";
      break;
    case disc::Result::kNotGameDisc:
      problem = L"This file is not an Xbox 360 game disc image: no game was found in it.";
      break;
    case disc::Result::kUnsafeName:
      problem = L"The disc image contains a file name that cannot be used on this PC, so nothing was "
                L"copied.";
      break;
    case disc::Result::kDamaged:
      problem = L"The disc image is damaged: its list of files is broken, so nothing was copied. Make "
                L"a new copy of the disc image.";
      break;
    case disc::Result::kWrongVersion:
      problem = L"This is a different game, or a different version of Ridge Racer 6, than this build was "
                L"made for. It only works with the USA disc (title ID 4E4D07D3).\n\n"
                L"Nothing was copied.";
      break;
    case disc::Result::kNoSpace: {
      unsigned long long needed = 0, available = 0;
      sscanf(detail.c_str(), "%llu %llu", &needed, &available);
      problem = L"There is not enough free disk space for the game files: " + Gigabytes(needed) +
                L" GB are needed, and the drive this folder is on has " + Gigabytes(available) +
                L" GB free.\n\nFree some space, or move this folder to another drive, and choose the disc "
                L"image again.";
      break;
    }
    case disc::Result::kReadError:
      problem = L"The disc image could not be read to the end. It may be damaged or incomplete.\n\n"
                L"Files copied so far are kept; choosing a good image carries on from there.";
      break;
    case disc::Result::kWriteError:
      problem = L"A game file could not be written:\n" + Widen(detail) +
                L"\n\nIs this folder read-only, or still inside the zip? Extract the package to a normal "
                L"folder first.";
      break;
  }
  SetStatus(L"The game files were not copied. Choose the disc image to try again.");
  MessageBoxW(app.window, problem.c_str(), L"Ridge Racer 6", MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

// Creates a control. Position and size are in 96-DPI units relative to the
// parent, which is the main window or one of the pages.
HWND Add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
         int id, DWORD ex_style = 0) {
  HWND c = CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                           parent, (HMENU)(INT_PTR)id, app.instance, nullptr);
  SendMessageW(c, WM_SETFONT, (WPARAM)app.font, TRUE);
  if (id > 0) app.controls[id] = c;
  return c;
}

HWND AddCombo(HWND parent, int x, int y, int w, int id, std::initializer_list<const wchar_t*> items) {
  HWND c = Add(parent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 240, id);
  for (const wchar_t* item : items) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)item);
  return c;
}

HWND AddButton(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id) {
  HWND c = Add(parent, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id);
  SetWindowSubclass(c, ButtonProc, 1, 0);
  return c;
}

// Explanatory text, in the quieter colour.
HWND AddNote(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
  HWND c = Add(parent, L"STATIC", text, SS_NOPREFIX, x, y, w, h, -1);
  app.muted.insert(c);
  return c;
}

HWND AddHeading(HWND parent, const wchar_t* text, int x, int y, int w) {
  HWND c = Add(parent, L"STATIC", text, SS_NOPREFIX, x, y, w, 20, -1);
  SendMessageW(c, WM_SETFONT, (WPARAM)app.bold, TRUE);
  return c;
}

// ---------------------------------------------------------------------------
// Achievements page
// ---------------------------------------------------------------------------
//
// The game keeps a copy of its achievement list, with what has been unlocked
// and when, in <save data folder>\achievements\list.txt, and the icons as PNG
// files next to it (src/achievements.cpp in the game; they come out of the
// player's own copy of the game). This page shows that copy. It exists once
// the game has been started; until then the page says so.

struct Achievement {
  int id = 0, score = 0, image = 0;
  bool online_only = false;  // needs Xbox Live play, which this version does not have
  bool secret = false;       // the game hides it until it is unlocked
  uint64_t unlocked = 0;     // FILETIME of the unlock, 1 = unlocked at an unknown time, 0 = locked
  std::wstring name, done_text, how_text;
};

struct AchievementPage {
  std::vector<Achievement> list;  // those that can be earned first, then the online-only ones
  std::map<int, Gdiplus::Bitmap*> icons;  // by image id; nullptr = looked for and not there
  FILETIME stamp = {0, 0};        // last change of list.txt when it was read
  bool found = false;
  int scroll = 0;                 // pixels
  int content = 0;                // height of all rows, pixels
};
AchievementPage ach;

const int kAchHeader = 50;   // 96-DPI units
const int kAchRow = 62;
const int kAchGroup = 40;    // the line that introduces the online-only ones

std::wstring AchievementFolder() {
  std::wstring root = UserDataRoot();
  return root.empty() ? L"" : root + L"\\achievements\\";
}

// Reads a picture file into a bitmap of its own (the file is not kept open).
Gdiplus::Bitmap* LoadPicture(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return nullptr;
  DWORD size = GetFileSize(file, nullptr), got = 0;
  Gdiplus::Bitmap* result = nullptr;
  if (size > 0 && size < 4 * 1024 * 1024) {
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size)) {
      void* bytes = GlobalLock(memory);
      BOOL ok = bytes && ReadFile(file, bytes, size, &got, nullptr) && got == size;
      GlobalUnlock(memory);
      IStream* stream = nullptr;
      if (ok && SUCCEEDED(CreateStreamOnHGlobal(memory, TRUE, &stream)) && stream) {
        Gdiplus::Bitmap* source = Gdiplus::Bitmap::FromStream(stream);
        if (source && source->GetLastStatus() == Gdiplus::Ok && source->GetWidth() > 0) {
          result = new Gdiplus::Bitmap(source->GetWidth(), source->GetHeight(), PixelFormat32bppARGB);
          Gdiplus::Graphics g(result);
          g.DrawImage(source, 0, 0, (INT)source->GetWidth(), (INT)source->GetHeight());
        }
        delete source;
        stream->Release();  // frees the memory too
      } else {
        GlobalFree(memory);
      }
    }
  }
  CloseHandle(file);
  return result;
}

Gdiplus::Bitmap* AchievementIcon(int image) {
  auto found = ach.icons.find(image);
  if (found != ach.icons.end()) return found->second;
  Gdiplus::Bitmap* icon = LoadPicture(AchievementFolder() + L"icons\\" + std::to_wstring(image) + L".png");
  ach.icons[image] = icon;
  return icon;
}

// Reads list.txt again if it has changed. Returns true if the page changed.
bool ReloadAchievements() {
  std::wstring path = AchievementFolder() + L"list.txt";
  WIN32_FILE_ATTRIBUTE_DATA info;
  bool there = !path.empty() && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info);
  if (!there) {
    bool changed = ach.found;
    ach.found = false;
    ach.list.clear();
    return changed;
  }
  if (ach.found && CompareFileTime(&info.ftLastWriteTime, &ach.stamp) == 0) return false;
  std::string text;
  if (!ReadFileUtf8(path, &text)) return false;
  std::vector<Achievement> list;
  size_t at = 0;
  bool first = true;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (first) {
      first = false;
      if (line.compare(0, 16, "RR6-ACHIEVEMENTS") != 0) return false;  // not ours
      continue;
    }
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> field;
    size_t from = 0;
    while (true) {
      size_t tab = line.find('\t', from);
      field.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
      if (tab == std::string::npos) break;
      from = tab + 1;
    }
    if (field.size() < 9) continue;
    Achievement a;
    a.id = atoi(field[0].c_str());
    a.score = atoi(field[1].c_str());
    a.online_only = field[2] == "1";
    a.secret = field[3] == "1";
    a.unlocked = strtoull(field[4].c_str(), nullptr, 10);
    a.image = atoi(field[5].c_str());
    a.name = Widen(field[6]);
    a.done_text = Widen(field[7]);
    a.how_text = Widen(field[8]);
    list.push_back(a);
  }
  std::stable_sort(list.begin(), list.end(),
                   [](const Achievement& a, const Achievement& b) { return a.online_only < b.online_only; });
  ach.list = list;
  ach.stamp = info.ftLastWriteTime;
  ach.found = true;
  return true;
}

std::wstring UnlockDate(uint64_t filetime) {
  if (filetime < 2) return L"";
  FILETIME ft = {(DWORD)(filetime & 0xFFFFFFFFu), (DWORD)(filetime >> 32)};
  SYSTEMTIME utc, local;
  if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"";
  wchar_t text[32];
  swprintf(text, 32, L"%04d-%02d-%02d", local.wYear, local.wMonth, local.wDay);
  return text;
}

// Sets the scroll bar to match the list, and keeps the position in range.
void LayoutAchievements(HWND hwnd) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int view = std::max(0L, rc.bottom - S(kAchHeader));
  bool online = false;
  int content = 0;
  for (const Achievement& a : ach.list) {
    if (a.online_only && !online) {
      online = true;
      content += S(kAchGroup);
    }
    content += S(kAchRow);
  }
  ach.content = content;
  ach.scroll = std::max(0, std::min(ach.scroll, content - view));
  SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL, 0, std::max(0, content - 1),
                   (UINT)view, ach.scroll, 0};
  SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

void ScrollAchievements(HWND hwnd, int to) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int view = std::max(0L, rc.bottom - S(kAchHeader));
  to = std::max(0, std::min(to, ach.content - view));
  if (to == ach.scroll) return;
  ach.scroll = to;
  SetScrollPos(hwnd, SB_VERT, to, TRUE);
  InvalidateRect(hwnd, nullptr, FALSE);
}

void PaintAchievements(HWND hwnd, HDC dc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  const int w = rc.right, h = rc.bottom;
  if (w <= 0 || h <= 0) return;
  Gdiplus::Bitmap canvas(w, h, PixelFormat32bppPARGB);
  Gdiplus::Graphics g(&canvas);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.Clear(Rgb(kPaper));

  Gdiplus::FontFamily segoe(L"Segoe UI");
  const Gdiplus::FontFamily* family =
      segoe.IsAvailable() ? &segoe : Gdiplus::FontFamily::GenericSansSerif();
  Gdiplus::Font heading(family, (float)S(15), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font name_font(family, (float)S(14), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font text_font(family, (float)S(12), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush ink(Rgb(kInk)), steel(Rgb(kSteel)), green(Rgb(kGreen)), faint(Rgb(RGB(0x9A, 0xA3, 0xA9)));
  Gdiplus::StringFormat plain(Gdiplus::StringFormat::GenericTypographic());
  plain.SetFormatFlags(plain.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
  Gdiplus::StringFormat right(&plain);
  right.SetAlignment(Gdiplus::StringAlignmentFar);
  Gdiplus::StringFormat wrapped(Gdiplus::StringFormat::GenericTypographic());
  wrapped.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
  wrapped.SetFormatFlags(wrapped.GetFormatFlags() | Gdiplus::StringFormatFlagsLineLimit);

  if (!ach.found || ach.list.empty()) {
    Gdiplus::StringFormat centre;
    centre.SetAlignment(Gdiplus::StringAlignmentCenter);
    centre.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"Your achievements appear here once you have started the game.\n\n"
                 L"Ridge Racer 6 has 36 of them. While playing, F7 shows the list, and so does Y\n"
                 L"after Esc (or after holding Back + Start on a controller).",
                 -1, &text_font, Gdiplus::RectF(0, 0, (float)w, (float)h), &centre, &steel);
  } else {
    // Totals: counted against what can be earned without online play.
    int earnable = 0, earned = 0, online = 0, score = 0, possible = 0;
    for (const Achievement& a : ach.list) {
      if (a.online_only) {
        ++online;
        continue;
      }
      ++earnable;
      possible += a.score;
      if (a.unlocked) {
        ++earned;
        score += a.score;
      }
    }

    // The rows, under the heading.
    const int top = S(kAchHeader);
    g.SetClip(Gdiplus::Rect(0, top, w, h - top));
    int y = top - ach.scroll;
    bool group_shown = false;
    for (const Achievement& a : ach.list) {
      if (a.online_only && !group_shown) {
        group_shown = true;
        if (y + S(kAchGroup) > top && y < h) {
          wchar_t line[128];
          swprintf(line, 128, L"Need online play, which this version does not have (%d)", online);
          g.DrawString(line, -1, &name_font, Gdiplus::PointF(0, (float)(y + S(16))), &plain, &steel);
        }
        y += S(kAchGroup);
      }
      const int row = S(kAchRow);
      if (y + row > top && y < h) {
        const bool unlocked = a.unlocked != 0;
        const bool hidden = a.secret && !unlocked;
        Gdiplus::RectF box(0, (float)(y + S(3)), (float)(w - S(6)), (float)(row - S(6)));
        Gdiplus::SolidBrush fill(unlocked ? Rgb(RGB(0xF0, 0xF8, 0xDC)) : Rgb(kMist));
        g.FillRectangle(&fill, box);
        if (unlocked) {
          Gdiplus::SolidBrush mark(Rgb(kLime));
          g.FillRectangle(&mark, Gdiplus::RectF(box.X, box.Y, (float)S(4), box.Height));
        }
        const int icon = S(44);
        Gdiplus::RectF icon_box(box.X + S(14), box.Y + (box.Height - icon) / 2, (float)icon, (float)icon);
        Gdiplus::Bitmap* picture = hidden ? nullptr : AchievementIcon(a.image);
        if (picture) {
          Gdiplus::ImageAttributes attributes;
          if (!unlocked) {
            // Locked: grey and pale.
            Gdiplus::ColorMatrix grey = {{{0.30f, 0.30f, 0.30f, 0, 0},
                                          {0.59f, 0.59f, 0.59f, 0, 0},
                                          {0.11f, 0.11f, 0.11f, 0, 0},
                                          {0, 0, 0, 0.55f, 0},
                                          {0, 0, 0, 0, 1}}};
            attributes.SetColorMatrix(&grey);
          }
          g.DrawImage(picture, icon_box, 0, 0, (Gdiplus::REAL)picture->GetWidth(),
                      (Gdiplus::REAL)picture->GetHeight(), Gdiplus::UnitPixel, &attributes);
        } else {
          Gdiplus::SolidBrush tile(Rgb(kLine));
          g.FillRectangle(&tile, icon_box);
          Gdiplus::StringFormat centre;
          centre.SetAlignment(Gdiplus::StringAlignmentCenter);
          centre.SetLineAlignment(Gdiplus::StringAlignmentCenter);
          Gdiplus::Font mark(family, (float)S(20), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
          g.DrawString(hidden ? L"?" : L"G", -1, &mark, icon_box, &centre, &steel);
        }

        wchar_t points[32];
        swprintf(points, 32, L"%d G", a.score);
        const float edge = box.X + box.Width - S(14);
        g.DrawString(points, -1, &name_font, Gdiplus::PointF(edge, box.Y + S(9)), &right, unlocked ? &green : &steel);
        std::wstring state;
        if (unlocked) {
          std::wstring date = UnlockDate(a.unlocked);
          state = date.empty() ? L"Unlocked" : L"Unlocked " + date;
        } else if (a.online_only) {
          state = L"Online only";
        }
        if (!state.empty()) {
          g.DrawString(state.c_str(), -1, &text_font, Gdiplus::PointF(edge, box.Y + S(31)), &right,
                       unlocked ? &green : &faint);
        }

        const float tx = icon_box.X + icon + S(14);
        const float tw = edge - S(118) - tx;
        const std::wstring& name = hidden ? std::wstring(L"Secret achievement") : a.name;
        const std::wstring& text = hidden ? std::wstring(L"Keep playing to find out what this one is.")
                                          : (unlocked || a.how_text.empty()) ? a.done_text : a.how_text;
        g.DrawString(name.c_str(), -1, &name_font, Gdiplus::RectF(tx, box.Y + S(6), tw, (float)S(20)), &wrapped,
                     &ink);
        g.DrawString(text.c_str(), -1, &text_font, Gdiplus::RectF(tx, box.Y + S(25), tw, (float)S(30)), &wrapped,
                     &steel);
      }
      y += row;
    }
    g.ResetClip();

    // The heading stays put: how far along, and a bar.
    Gdiplus::SolidBrush paper(Rgb(kPaper));
    g.FillRectangle(&paper, 0, 0, w, top);
    wchar_t line[96];
    swprintf(line, 96, L"%d of %d earned", earned, earnable);
    g.DrawString(line, -1, &heading, Gdiplus::PointF(0, (float)S(4)), &plain, &ink);
    Gdiplus::RectF measured;
    g.MeasureString(line, -1, &heading, Gdiplus::PointF(0, 0), &plain, &measured);
    swprintf(line, 96, L"%d G of %d G", score, possible);
    g.DrawString(line, -1, &text_font, Gdiplus::PointF(measured.Width + S(16), (float)S(7)), &plain, &steel);
    const float bar_y = (float)S(31), bar_w = (float)(w - S(6));
    Gdiplus::SolidBrush track(Rgb(kLine)), bar(Rgb(kLime));
    g.FillRectangle(&track, 0.0f, bar_y, bar_w, (float)S(6));
    if (earnable > 0 && earned > 0) g.FillRectangle(&bar, 0.0f, bar_y, bar_w * earned / earnable, (float)S(6));
  }

  Gdiplus::Graphics screen(dc);
  screen.DrawImage(&canvas, 0, 0);
}

LRESULT CALLBACK AchievementsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      PaintAchievements(hwnd, dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      return 0;
    case WM_MOUSEWHEEL:
      ScrollAchievements(hwnd, ach.scroll - GET_WHEEL_DELTA_WPARAM(wp) * S(kAchRow) / WHEEL_DELTA);
      return 0;
    case WM_KEYDOWN: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      const int page = std::max(S(kAchRow), (int)rc.bottom - S(kAchHeader) - S(kAchRow));
      switch (wp) {
        case VK_UP: ScrollAchievements(hwnd, ach.scroll - S(kAchRow)); return 0;
        case VK_DOWN: ScrollAchievements(hwnd, ach.scroll + S(kAchRow)); return 0;
        case VK_PRIOR: ScrollAchievements(hwnd, ach.scroll - page); return 0;
        case VK_NEXT: ScrollAchievements(hwnd, ach.scroll + page); return 0;
        case VK_HOME: ScrollAchievements(hwnd, 0); return 0;
        case VK_END: ScrollAchievements(hwnd, ach.content); return 0;
      }
      break;
    }
    case WM_VSCROLL: {
      SCROLLINFO si = {sizeof(si), SIF_ALL, 0, 0, 0, 0, 0};
      GetScrollInfo(hwnd, SB_VERT, &si);
      int to = ach.scroll;
      switch (LOWORD(wp)) {
        case SB_LINEUP: to -= S(kAchRow); break;
        case SB_LINEDOWN: to += S(kAchRow); break;
        case SB_PAGEUP: to -= (int)si.nPage; break;
        case SB_PAGEDOWN: to += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: to = si.nTrackPos; break;
        case SB_TOP: to = 0; break;
        case SB_BOTTOM: to = ach.content; break;
      }
      ScrollAchievements(hwnd, to);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// Called when the page is shown and when the launcher comes to the front
// again (the game may have run in between).
void RefreshAchievements() {
  HWND list = Ctl(IDC_ACH_LIST);
  if (!list) return;
  bool changed = ReloadAchievements();
  LayoutAchievements(list);
  if (changed) InvalidateRect(list, nullptr, FALSE);
}

// The sound the game plays with the pop-up: the player's own achievement.wav
// next to this launcher if there is one, otherwise the one the game comes with.
void PlayAchievementSound() {
  std::wstring game_dir = app.game_exe.substr(0, app.game_exe.find_last_of(L"\\/") + 1);
  std::wstring candidates[] = {app.dir + L"achievement.wav", game_dir + L"achievement.wav",
                               game_dir + L"sounds\\achievement.wav"};
  for (const std::wstring& file : candidates) {
    if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
      PlaySoundW(file.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
      SetStatus(L"Playing " + file.substr(file.find_last_of(L"\\/") + 1) + L".");
      return;
    }
  }
  SetStatus(L"No achievement.wav was found.");
}

// ---------------------------------------------------------------------------
// DLC page
// ---------------------------------------------------------------------------
//
// Downloadable content the player owns can be added to the game. On the
// console it sits on the hard drive as package files (long names without an
// extension, in Content\0000000000000000\4E4D07D3\00000002). The game program
// unpacks them itself when started with --rr6_install_content=<files or
// folders, separated by |> (src/dlc_install.cpp): it checks that each one is
// downloadable content for this game, unpacks it into the save data folder,
// writes what happened to dlc-install-result.txt there, and closes without
// starting the game. It also keeps dlc-installed.txt, the names of what is
// installed. Nothing of this is game data of ours: the files are the player's.

const wchar_t* const kDlcHint =
    L"To remove content, delete its folder (\"Open content folder\"), and take its file out of the "
    L"DLC folder if it is there.";

std::wstring ContentFolder() {
  std::wstring root = UserDataRoot();
  if (root.empty()) return L"";
  return root + L"\\0000000000000000\\4E4D07D3\\00000002";
}

std::vector<std::string> SplitOn(const std::string& text, char separator) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    size_t end = text.find(separator, start);
    if (end == std::string::npos) {
      parts.push_back(text.substr(start));
      return parts;
    }
    parts.push_back(text.substr(start, end - start));
    start = end + 1;
  }
}

// Lists what is installed: every folder in the content folder, under the name
// the game program recorded for it.
void RefreshDlc() {
  HWND list = Ctl(IDC_DLC_LIST);
  if (!list) return;
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  std::map<std::string, std::string> names;
  std::string text;
  std::wstring root = UserDataRoot();
  if (!root.empty() && ReadFileUtf8(root + L"\\dlc-installed.txt", &text)) {
    for (const std::string& line : SplitOn(text, '\n')) {
      std::vector<std::string> fields = SplitOn(Trim(line), '\t');
      if (fields.size() >= 2 && !fields[0].empty()) names[fields[0]] = fields[1];
    }
  }
  int count = 0;
  std::wstring folder = ContentFolder();
  WIN32_FIND_DATAW found;
  HANDLE find = folder.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW((folder + L"\\*").c_str(), &found);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..")) continue;
      auto name = names.find(Narrow(found.cFileName));
      std::wstring shown = name != names.end() ? Widen(name->second) : std::wstring(found.cFileName);
      SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)shown.c_str());
      ++count;
    } while (FindNextFileW(find, &found));
    FindClose(find);
  }
  EnableWindow(list, count > 0);
  if (count == 0) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"No downloadable content has been added.");
}

std::vector<std::wstring> PickContentFiles() {
  std::vector<wchar_t> buffer(65536, 0);
  OPENFILENAMEW dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = app.window;
  dialog.lpstrFilter = L"All files (*.*)\0*.*\0";
  dialog.lpstrFile = buffer.data();
  dialog.nMaxFile = (DWORD)buffer.size();
  dialog.lpstrTitle = L"Choose your Ridge Racer 6 content files";
  dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                 OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) return {};
  // One file: its full path. Several: the folder, then each name, each ended
  // by a zero, with one more zero after the last.
  std::wstring first = buffer.data();
  const wchar_t* next = buffer.data() + first.size() + 1;
  if (*next == 0) return {first};
  if (!first.empty() && first.back() != L'\\') first += L'\\';
  std::vector<std::wstring> files;
  while (*next) {
    std::wstring name = next;
    files.push_back(first + name);
    next += name.size() + 1;
  }
  return files;
}

std::wstring PickContentFolder() {
  BROWSEINFOW info = {};
  info.hwndOwner = app.window;
  info.lpszTitle = L"Choose the folder that holds your Ridge Racer 6 content files";
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
  if (!item) return L"";
  wchar_t path[MAX_PATH * 2] = L"";
  bool ok = SHGetPathFromIDListW(item, path) != FALSE;
  CoTaskMemFree(item);
  return ok ? std::wstring(path) : L"";
}

void AddDlc(std::vector<std::wstring> paths) {
  if (paths.empty()) return;
  if (app.game_exe.empty() || app.setup != Setup::kReady || !GameFilesReady()) {
    MessageBoxW(app.window,
                L"Set the game up first: the game files have to be copied from your disc image before "
                L"content can be added.",
                L"Ridge Racer 6", MB_ICONINFORMATION);
    return;
  }
  std::wstring root = UserDataRoot();
  if (root.empty()) {
    MessageBoxW(app.window, L"The save data folder could not be found.", L"Ridge Racer 6", MB_ICONERROR);
    return;
  }
  std::wstring list;
  for (std::wstring path : paths) {
    // A backslash before the closing quote would swallow the quote.
    while (!path.empty() && path.back() == L'\\') path.pop_back();
    if (!path.empty() && path.back() == L':') path += L"\\.";
    if (path.empty() || path.find(L'"') != std::wstring::npos) continue;
    if (!list.empty()) list += L"|";
    list += path;
  }
  CreateDirectoryW((app.dir + L"logs").c_str(), nullptr);
  std::wstring cmd = L"\"" + app.game_exe + L"\" --game_data_root \"" + app.game_data +
                     L"\" --gpu_plugin=xenos --fullscreen=false --log_file \"" + app.dir +
                     L"logs\\dlc-install.log\" \"--rr6_install_result=" + app.dir +
                     L"logs\\dlc-install-result.txt\" \"--rr6_install_content=" + list + L"\"";
  if (list.empty() || cmd.size() > 30000) {
    MessageBoxW(app.window,
                L"That is too many files to pass on at once. Use \"Add a folder...\" and choose the "
                L"folder they are in.",
                L"Ridge Racer 6", MB_ICONINFORMATION);
    return;
  }
  std::wstring result_path = app.dir + L"logs\\dlc-install-result.txt";
  DeleteFileW(result_path.c_str());

  SetStatus(L"Adding downloadable content...");
  SetWindowTextW(Ctl(IDC_DLC_NOTE), L"Adding... a game window opens for a moment and closes again.");
  for (int id : {IDC_DLC_ADD, IDC_DLC_FOLDER, IDC_PLAY}) EnableWindow(Ctl(id), FALSE);
  DWORD code = 0;
  bool ran = RunAndWait(cmd, app.dir, &code, 0);
  if (!IsWindow(app.window)) return;
  for (int id : {IDC_DLC_ADD, IDC_DLC_FOLDER, IDC_PLAY}) EnableWindow(Ctl(id), TRUE);
  SetWindowTextW(Ctl(IDC_DLC_NOTE), kDlcHint);
  SetForegroundWindow(app.window);

  std::string text;
  if (!ran || !ReadFileUtf8(result_path, &text)) {
    SetStatus(L"The content could not be added.");
    MessageBoxW(app.window,
                L"The content could not be added: the game program did not report back.\n\n"
                L"Check that the game itself starts with Play. The log of this attempt is "
                L"dlc-install.log in the logs folder (Troubleshooting tab, \"Open logs folder\").",
                L"Ridge Racer 6", MB_ICONWARNING);
    RefreshDlc();
    return;
  }
  int added = 0, not_added = 0;
  bool complete = false;
  std::wstring added_names, problems;
  const std::vector<std::string> lines = SplitOn(text, '\n');
  for (const std::string& line : lines) {
    std::vector<std::string> fields = SplitOn(Trim(line), '\t');
    if (fields.size() == 4 && fields[0] == "done") complete = true;
    if (fields.size() < 3) continue;
    if (fields[0] == "installed") {
      ++added;
      added_names += L"\n    " + Widen(fields[1]);
    } else if (fields[0] == "refused" || fields[0] == "failed") {
      ++not_added;
      problems += L"\n    " + Widen(fields[2]) + L": " + Widen(fields[1]);
    }
  }
  // The game writes the whole file at once, ending with a "done" line. A file
  // without one means it was cut off.
  if (lines.empty() || Trim(lines[0]) != "RR6-DLC-INSTALL 1") complete = false;
  wchar_t head[160];
  if (!complete) {
    swprintf(head, 160, L"Adding content did not finish (%d added before it stopped).", added);
    problems += L"\n    The game program stopped before it was done. The log of this attempt is "
                L"dlc-install.log in the logs folder (Troubleshooting tab, \"Open logs folder\").";
    ++not_added;
  } else if (not_added == 0) {
    swprintf(head, 160, added == 1 ? L"%d content package added." : L"%d content packages added.", added);
  } else {
    swprintf(head, 160, L"%d added, %d not added.", added, not_added);
  }
  SetStatus(head);
  RefreshDlc();
  std::wstring message = head;
  if (added) message += L"\n\nAdded:" + added_names;
  if (not_added) message += L"\n\nNot added:" + problems;
  if (added) message += L"\n\nThe game looks for added content each time it starts.";
  MessageBoxW(app.window, message.c_str(), L"Ridge Racer 6",
              not_added ? MB_ICONWARNING : MB_ICONINFORMATION);
}

void OpenContentFolder() {
  std::wstring folder = ContentFolder();
  if (folder.empty() || !DirExists(folder)) {
    SetStatus(L"No downloadable content has been added yet.");
    return;
  }
  ShellExecuteW(app.window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ShowPage(int page) {
  if (page < 0 || page >= kPageCount) return;
  app.current_page = page;
  if (page == kAchievementsPage) RefreshAchievements();
  if (page == kDlcPage) RefreshDlc();
  for (int p = 0; p < kPageCount; ++p) {
    ShowWindow(app.page[p], p == page ? SW_SHOW : SW_HIDE);
    InvalidateRect(Ctl(IDC_TAB0 + p), nullptr, FALSE);
  }
}

void BuildUi() {
  const int W = kWidth;
  const int page_y = app.banner_h;
  const int bar_y = page_y + kPageHeight;

  // Page buttons, on the dark strip at the foot of the banner.
  // Each is as wide as its text plus 12 units either side, so the first one's
  // text lines up with the 24-unit margin used everywhere else.
  const wchar_t* tabs[kPageCount] = {L"Display", L"Controls", L"Achievements", L"DLC", L"Troubleshooting"};
  HDC measure = GetDC(app.window);
  HGDIOBJ measure_font = SelectObject(measure, app.tab_font);
  int tx = S(12);
  for (int p = 0; p < kPageCount; ++p) {
    SIZE size = {0, 0};
    GetTextExtentPoint32W(measure, tabs[p], (int)wcslen(tabs[p]), &size);
    int tw = size.cx + S(24);
    HWND tab = AddButton(app.window, tabs[p], 0, page_y - kStrip, 10, kStrip, IDC_TAB0 + p);
    SetWindowPos(tab, nullptr, tx, S(page_y - kStrip), tw, S(kStrip), SWP_NOZORDER | SWP_NOACTIVATE);
    tx += tw;
  }
  SelectObject(measure, measure_font);
  ReleaseDC(app.window, measure);

  for (int p = 0; p < kPageCount; ++p) {
    app.page[p] = CreateWindowExW(WS_EX_CONTROLPARENT, L"RR6Page", L"", WS_CHILD | WS_CLIPCHILDREN, 0,
                                  S(page_y), S(W), S(kPageHeight), app.window, nullptr, app.instance, nullptr);
  }

  // ---- Display page ----
  HWND pg = app.page[0];
  const int lx = 24, cx = 250, cw = 446, full = 672;
  const int prose = 540;  // explanatory text keeps to a comfortable line length
  int y = 18;
  const int row = 31;  // combo rows
  wchar_t detected[160];
  swprintf(detected, 160, L"Fill my screen (%d x %d detected)", app.screen_w, app.screen_h);

  Add(pg, L"STATIC", L"Screen", 0, lx, y + 4, 210, 20, -1);
  AddCombo(pg, cx, y, cw, IDC_SCREEN, {L"Full screen", L"Window"});
  y += row;
  Add(pg, L"STATIC", L"Picture shape", 0, lx, y + 4, 210, 20, -1);
  AddCombo(pg, cx, y, cw, IDC_SHAPE, {detected, L"Original 16:9 (bars at the sides)"});
  y += row;
  Add(pg, L"STATIC", L"HUD position on wide screens", 0, lx, y + 4, 210, 20, -1);
  AddCombo(pg, cx, y, cw, IDC_HUD, {L"At the screen edges", L"Centred, where 16:9 would put it"});
  y += row;
  Add(pg, L"STATIC", L"Sharpness (render size)", 0, lx, y + 4, 210, 20, -1);
  AddCombo(pg, cx, y, cw, IDC_SCALE,
           {L"Automatic (match my screen)", L"1x - 720 lines, as on Xbox 360 (fastest)",
            L"2x - 1440 lines", L"3x - 2160 lines (4K)", L"4x - 2880 lines (very demanding)"});
  y += row;
  Add(pg, L"STATIC", L"Edge smoothing", 0, lx, y + 4, 210, 20, -1);
  AddCombo(pg, cx, y, cw, IDC_SMOOTH, {L"Off", L"FXAA", L"FXAA Extreme"});
  y += row;
  Add(pg, L"STATIC", L"Texture detail", 0, lx, y + 4, 210, 20, -1);
  AddCombo(pg, cx, y, cw, IDC_ANISO, {L"Standard (4x)", L"High (8x)", L"Highest (16x)"});
  y += row;
  // The disc's six languages, in the console's numbering (user_language 1-6;
  // src/language.cpp in the game answers the game's question with it).
  Add(pg, L"STATIC", L"Language", 0, lx, y + 4, 210, 20, -1);
  AddCombo(pg, cx, y, cw, IDC_LANGUAGE,
           {L"English", L"\u65E5\u672C\u8A9E (Japanese)", L"Deutsch (German)",
            L"Fran\u00E7ais (French)", L"Espa\u00F1ol (Spanish)", L"Italiano (Italian)"});
  y += 33;
  Add(pg, L"BUTTON", L"Fix flickering trees and foliage (recommended, needed on NVIDIA cards)",
      BS_AUTOCHECKBOX | WS_TABSTOP, lx, y, full, 22, IDC_FOLIAGE);
  y += 26;
  // vsync_to_display (our SDK fork): no tearing, and the game's 60 Hz clock
  // follows the screen's on 60 and 120 Hz screens (issue #16).
  Add(pg, L"BUTTON", L"Sync to my screen: no tearing (best on 60 or 120 Hz screens)",
      BS_AUTOCHECKBOX | WS_TABSTOP, lx, y, full, 22, IDC_VSYNC);
  y += 30;
  AddNote(pg,
          L"The game always runs at 60 frames per second, as it did on Xbox 360.\n"
          L"If the game runs slowly, choose a lower Sharpness. F3 in the game shows the frame rate.\n"
          L"Esc quits (hold Back + Start on a controller). F4: more settings (\"Save to config\" keeps them).",
          lx, y, full, 54);

  // ---- Controls page ----
  pg = app.page[1];
  y = 16;
  AddNote(pg,
          L"Xbox and PlayStation controllers work as soon as they are plugged in or paired; nothing "
          L"needs setting up. The game shows Xbox button names: choose PlayStation below to see which "
          L"button is which.",
          lx, y, prose + 60, 34);
  y += 42;
  Add(pg, L"BUTTON", L"Also use the keyboard (works together with a controller)",
      BS_AUTOCHECKBOX | WS_TABSTOP, lx, y, 390, 22, IDC_KEYBOARD);
  Add(pg, L"STATIC", L"Button names", SS_RIGHT, 430, y + 4, 120, 20, -1);
  AddCombo(pg, 560, y, 136, IDC_NAMES, {L"Xbox", L"PlayStation"});
  y += 32;
  HWND list = Add(pg, WC_LISTVIEWW, L"",
                  LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP, lx, y,
                  full, 186, IDC_BINDLIST, WS_EX_CLIENTEDGE);
  SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
               LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
  SetWindowTheme(list, L"Explorer", nullptr);
  LVCOLUMNW col = {};
  col.mask = LVCF_TEXT | LVCF_WIDTH;
  col.cx = S(330);
  col.pszText = const_cast<wchar_t*>(L"Controller button");
  SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
  col.cx = S(316);
  col.pszText = const_cast<wchar_t*>(L"Keyboard key");
  SendMessageW(list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
  y += 194;
  AddButton(pg, L"Set key...", lx, y, 110, 28, IDC_SETKEY);
  AddButton(pg, L"Add second key...", lx + 118, y, 140, 28, IDC_ADDKEY);
  AddButton(pg, L"Clear", lx + 266, y, 80, 28, IDC_CLEARKEY);
  AddButton(pg, L"Reset all keys", lx + full - 120, y, 120, 28, IDC_RESETKEYS);
  y += 36;
  AddNote(pg, L"Double-click a row to change its key. Keys do not respond while Shift, Ctrl or Alt is held.",
          lx, y, full, 18);

  // ---- Achievements page ----
  pg = app.page[kAchievementsPage];
  {
    HWND list = CreateWindowExW(0, L"RR6Achievements", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_CLIPSIBLINGS,
                                S(lx), S(10), S(full), S(kPageHeight - 20), pg, (HMENU)(INT_PTR)IDC_ACH_LIST,
                                app.instance, nullptr);
    app.controls[IDC_ACH_LIST] = list;
    // Over the list's heading, at the right: a way to hear the unlock sound.
    HWND sound = AddButton(pg, L"Play the unlock sound", lx + full - 190, 10, 170, 26, IDC_ACH_SOUND);
    SetWindowPos(sound, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

  // ---- DLC page ----
  pg = app.page[kDlcPage];
  y = 14;
  AddHeading(pg, L"Downloadable content", lx, y, full);
  y += 22;
  AddNote(pg,
          L"If you own downloadable content for Ridge Racer 6, you can add it to the game here. Use the "
          L"content files from your own Xbox 360's storage. They are in the folder "
          L"Content\\0000000000000000\\4E4D07D3\\00000002 and have long names without an extension. "
          L"The files are only read, and nothing is downloaded.\n"
          L"You can also put them in the DLC folder next to this launcher: the game adds new ones "
          L"each time it starts.",
          lx, y, full, 68);
  y += 74;
  Add(pg, L"LISTBOX", L"", LBS_NOINTEGRALHEIGHT | LBS_NOSEL | WS_VSCROLL | WS_TABSTOP, lx, y, full, 134,
      IDC_DLC_LIST, WS_EX_CLIENTEDGE);
  y += 142;
  AddButton(pg, L"Add content files...", lx, y, 170, 28, IDC_DLC_ADD);
  AddButton(pg, L"Add a folder...", lx + 178, y, 130, 28, IDC_DLC_FOLDER);
  AddButton(pg, L"Open content folder", lx + full - 170, y, 170, 28, IDC_DLC_OPEN);
  y += 36;
  {
    HWND note = Add(pg, L"STATIC", kDlcHint, SS_NOPREFIX, lx, y, full, 34, IDC_DLC_NOTE);
    app.muted.insert(note);
  }

  // ---- Troubleshooting page ----
  pg = app.page[kTroublePage];
  y = 14;
  AddHeading(pg, L"Bug reports", lx, y, full);
  y += 22;
  Add(pg, L"BUTTON", L"Record a detailed log the next time I play", BS_AUTOCHECKBOX | WS_TABSTOP, lx, y, full,
      22, IDC_DIAG);
  y += 24;
  AddNote(pg,
          L"When the game closes, the log, the exit code, your settings and a short description of this "
          L"PC (Windows version, processor, memory, graphics card) are packed into bug-report.zip in "
          L"the game folder. Nothing is sent anywhere; you decide who gets the file.",
          lx, y, prose, 50);
  y += 54;
  AddButton(pg, L"Open logs folder", lx, y, 150, 28, IDC_LOGS);
  AddButton(pg, L"Open save data folder", lx + 158, y, 176, 28, IDC_SAVES);
  y += 40;
  AddHeading(pg, L"Start over", lx, y, full);
  y += 22;
  AddButton(pg, L"Restore default settings", lx, y, 190, 28, IDC_DEFAULTS);
  AddButton(pg, L"Copy game files again...", lx + 198, y, 190, 28, IDC_COPY_AGAIN);
  y += 34;
  AddNote(pg,
          L"Restoring puts the Display and Controls choices back as they were at first. Copying reads "
          L"the game files from your disc image once more. Neither touches save data.",
          lx, y, prose, 34);
  y += 42;
  AddHeading(pg, L"About this build", lx, y, full);
  y += 22;
  AddNote(pg,
          L"An unofficial, fan-made PC build of Ridge Racer 6 (Xbox 360) with no game data in it. The "
          L"game files come from your own disc image; once they are in place, the picture at the top of "
          L"this window is taken from the game's opening movie on your PC. Not affiliated with or "
          L"endorsed by Bandai Namco Entertainment, who own Ridge Racer and its trademarks.",
          lx, y, prose, 68);

  // ---- Bottom bar (always visible); LayoutBar() puts things in place ----
  Add(app.window, L"STATIC", L"", SS_NOPREFIX, 24, bar_y + 14, 400, 50, IDC_STATUS);
  AddButton(app.window, L"Save", 462, bar_y + 22, 88, 32, IDC_SAVE);
  AddButton(app.window, L"Play", 562, bar_y + 14, 134, 48, IDC_PLAY);
  AddButton(app.window, L"Stop", 608, bar_y + 22, 88, 32, IDC_STOP_COPY);

  ComposeBanner();
  ShowPage(0);
  RefreshSetup();
}

LRESULT HandleCommand(HWND hwnd, WPARAM wp) {
  int id = LOWORD(wp), code = HIWORD(wp);
  if (code == CBN_SELCHANGE && (id == IDC_SCREEN || id == IDC_SHAPE)) UpdateEnabledStates();
  if (code == CBN_SELCHANGE && id == IDC_NAMES) {
    app.ps_names = ComboGet(IDC_NAMES) == 1;
    RefreshBindList();
  }
  if (code == BN_CLICKED) {
    if (IsTab(id)) {
      ShowPage(id - IDC_TAB0);
      return 0;
    }
    switch (id) {
      case IDC_KEYBOARD: UpdateEnabledStates(); break;
      case IDC_ACH_SOUND: PlayAchievementSound(); break;
      case IDC_DLC_ADD: AddDlc(PickContentFiles()); break;
      case IDC_DLC_FOLDER: {
        std::wstring folder = PickContentFolder();
        if (!folder.empty()) AddDlc({folder});
        break;
      }
      case IDC_DLC_OPEN: OpenContentFolder(); break;
      case IDC_SETKEY: ChangeKey(false); break;
      case IDC_ADDKEY: ChangeKey(true); break;
      case IDC_CLEARKEY: {
        int i = SelectedBinding();
        if (i >= 0) {
          app.keys[i].clear();
          RefreshBindList();
        }
        break;
      }
      case IDC_RESETKEYS:
        for (int i = 0; i < kBindingCount; ++i) app.keys[i] = kBindings[i].default_keys;
        RefreshBindList();
        SetStatus(L"Keys reset. Press Save or Play to keep them.");
        break;
      case IDC_PLAY:
        if (app.setup == Setup::kNeeded) {
          ChooseDiscImage(false);
        } else {
          Play();
        }
        break;
      case IDC_STOP_COPY: StopCopy(); break;
      case IDC_COPY_AGAIN:
        if (MessageBoxW(hwnd,
                        L"This copies all the game files from your disc image again and replaces the ones "
                        L"in the game folder (about 6 GB). Save data and settings are not touched.\n\n"
                        L"Choose the disc image now?",
                        L"Ridge Racer 6", MB_ICONQUESTION | MB_OKCANCEL) == IDOK) {
          ChooseDiscImage(true);
        }
        break;
      case IDC_SAVE: SaveAll(); break;
      case IDC_DEFAULTS: LoadDefaults(); break;
      case IDC_LOGS:
        CreateDirectoryW((app.dir + L"logs").c_str(), nullptr);
        ShellExecuteW(hwnd, L"open", (app.dir + L"logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
      case IDC_SAVES: {
        std::wstring root = UserDataRoot();
        if (root.empty() || !DirExists(root)) {
          SetStatus(L"There is no save data yet. The folder is created the first time the game runs.");
        } else {
          ShellExecuteW(hwnd, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
      }
    }
  }
  return 0;
}

LRESULT HandleNotify(LPARAM lp) {
  NMHDR* hdr = (NMHDR*)lp;
  if (hdr->idFrom == IDC_BINDLIST && hdr->code == NM_DBLCLK && CheckGet(IDC_KEYBOARD)) {
    ChangeKey(false);
  }
  return 0;
}

// A page: forwards its controls' messages to the main window and paints
// itself white, as the game's menus are.
LRESULT CALLBACK PageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND:
      return HandleCommand(app.window, wp);
    case WM_NOTIFY:
      return HandleNotify(lp);
    case WM_DRAWITEM:
      DrawButton((const DRAWITEMSTRUCT*)lp);
      return TRUE;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      SetBkColor((HDC)wp, kPaper);
      SetTextColor((HDC)wp, app.muted.count((HWND)lp) ? kSteel : kInk);
      return (LRESULT)app.paper_brush;
    case WM_ERASEBKGND: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      FillRect((HDC)wp, &rc, app.paper_brush);
      return 1;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_ACTIVATE:
      // Back in front, perhaps after a game: show what was unlocked meanwhile.
      if (LOWORD(wp) != WA_INACTIVE && app.current_page == kAchievementsPage) RefreshAchievements();
      break;
    case WM_MOUSEWHEEL:
      // The wheel goes to whatever has the keyboard; on the achievements page
      // it should scroll the list wherever the keyboard is.
      if (app.current_page == kAchievementsPage && Ctl(IDC_ACH_LIST)) {
        return SendMessageW(Ctl(IDC_ACH_LIST), msg, wp, lp);
      }
      break;
    case WM_COMMAND:
      return HandleCommand(hwnd, wp);
    case WM_NOTIFY:
      return HandleNotify(lp);
    case WM_DRAWITEM:
      DrawButton((const DRAWITEMSTRUCT*)lp);
      return TRUE;
    case DM_GETDEFID:
      return MAKELRESULT(IDC_PLAY, DC_HASDEFID);  // Enter presses Play
    case WM_CTLCOLORSTATIC:
      SetBkColor((HDC)wp, kMist);
      SetTextColor((HDC)wp, kInk);
      return (LRESULT)app.mist_brush;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT client;
      GetClientRect(hwnd, &client);
      const int banner_h = S(app.banner_h);
      if (app.banner_dc && app.banner) {
        BitBlt(dc, 0, 0, client.right, banner_h, app.banner_dc, 0, 0, SRCCOPY);
      } else {
        RECT top = {0, 0, client.right, banner_h};
        FillSolid(dc, top, kStripColor);
      }
      RECT bar = {0, banner_h, client.right, client.bottom};
      FillSolid(dc, bar, kMist);
      RECT line = {0, S(app.banner_h + kPageHeight), client.right, S(app.banner_h + kPageHeight) + std::max(1, S(1))};
      FillSolid(dc, line, kLine);
      if (app.setup == Setup::kCopying) {
        RECT track = app.copy_bar;
        FillSolid(dc, track, kLine);
        track.right = track.left + (LONG)((track.right - track.left) * std::min(1.0, app.copy_fraction));
        FillSolid(dc, track, kGreen);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_APP_ART: {
      art_loading = false;
      Art* art = (Art*)lp;
      if (art) {
        if (!app.custom_art && app.art.hero.empty()) {
          app.art = std::move(*art);
          ComposeBanner();
        }
        delete art;
      }
      return 0;
    }
    case WM_TIMER:
      if (wp == kCopyTimer) ShowCopyProgress();
      return 0;
    case WM_APP_COPY_DONE: {
      std::string* detail = (std::string*)lp;
      CopyFinished((disc::Result)wp, detail ? *detail : std::string());
      delete detail;
      return 0;
    }
    case WM_CLOSE:
      if (app.setup == Setup::kCopying && app.copy) {
        if (MessageBoxW(hwnd,
                        L"The game files are still being copied. Stop and close?\n\n"
                        L"Files copied so far are kept: choosing the disc image again later carries on "
                        L"from there.",
                        L"Ridge Racer 6", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES &&
            app.setup == Setup::kCopying && app.copy) {
          app.close_after_copy = true;
          StopCopy();
        }
        return 0;
      }
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

HFONT MakeFont(const std::wstring& face, int pixels, int weight) {
  return CreateFontW(-S(pixels), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face.c_str());
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int) {
  SetProcessDPIAware();  // also declared in the manifest; needed for the true screen size
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // the folder chooser on the DLC page needs it
  app.instance = instance;
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  Gdiplus::GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus_token = 0;
  Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);

  LocateGame();
  app.screen_w = GetSystemMetrics(SM_CXSCREEN);
  app.screen_h = GetSystemMetrics(SM_CYSCREEN);
  std::wstring args = command_line ? command_line : L"";
  size_t screen_arg = args.find(L"--screen ");
  if (screen_arg != std::wstring::npos) {
    int w = 0, h = 0;
    if (swscanf(args.c_str() + screen_arg + 9, L"%dx%d", &w, &h) == 2 && w >= 640 && h >= 480) {
      app.screen_w = w;
      app.screen_h = h;
    }
  }

  std::string text;
  if (ReadFileUtf8(app.config_path, &text)) app.settings.Parse(text);
  if (ReadFileUtf8(app.prefs_path, &text)) app.prefs.Parse(text);

  HDC screen = GetDC(nullptr);
  app.dpi = GetDeviceCaps(screen, LOGPIXELSX);
  ReleaseDC(nullptr, screen);
  if (app.dpi < 96) app.dpi = 96;

  NONCLIENTMETRICSW ncm = {};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  app.font = CreateFontIndirectW(&ncm.lfMessageFont);
  LOGFONTW bold = ncm.lfMessageFont;
  bold.lfWeight = FW_BOLD;
  app.bold = CreateFontIndirectW(&bold);

  // Bahnschrift comes with Windows 10 and 11; older systems fall back.
  app.display_face = ncm.lfMessageFont.lfFaceName;
  for (const wchar_t* face : {L"Bahnschrift", L"Segoe UI"}) {
    Gdiplus::FontFamily family(face);
    if (family.IsAvailable()) {
      app.display_face = face;
      break;
    }
  }
  app.tab_font = MakeFont(app.display_face, 15, FW_SEMIBOLD);
  app.play_font = MakeFont(app.display_face, 21, FW_SEMIBOLD);
  app.paper_brush = CreateSolidBrush(kPaper);
  app.mist_brush = CreateSolidBrush(kMist);

  HICON icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
  HICON small_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.lpszClassName = L"RR6Launcher";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIconSm = small_icon;
  RegisterClassExW(&wc);
  WNDCLASSEXW cc = wc;
  cc.lpfnWndProc = CaptureProc;
  cc.lpszClassName = L"RR6KeyCapture";
  cc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  RegisterClassExW(&cc);
  WNDCLASSEXW pc = wc;
  pc.lpfnWndProc = PageProc;
  pc.lpszClassName = L"RR6Page";
  RegisterClassExW(&pc);
  WNDCLASSEXW ac = wc;
  ac.lpfnWndProc = AchievementsProc;
  ac.lpszClassName = L"RR6Achievements";
  RegisterClassExW(&ac);

  // Fixed-size window; the layout is in 96-DPI units, scaled to the real DPI.
  // On a screen too small for all of it, the banner gives up height first.
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
  RECT work = {0, 0, app.screen_w, app.screen_h};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT rc = {0, 0, S(kWidth), S(app.banner_h + kPageHeight + kBarHeight)};
  AdjustWindowRect(&rc, style, FALSE);
  int overflow = (int)(rc.bottom - rc.top) - (int)(work.bottom - work.top);
  if (overflow > 0) {
    app.banner_h = std::max(kStrip + 44, app.banner_h - MulDiv(overflow, 96, app.dpi) - 1);
    rc = {0, 0, S(kWidth), S(app.banner_h + kPageHeight + kBarHeight)};
    AdjustWindowRect(&rc, style, FALSE);
  }
  int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
  int wx = work.left + std::max(0, (int)(work.right - work.left - ww) / 2);
  int wy = work.top + std::max(0, (int)(work.bottom - work.top - wh) / 2);
  app.window = CreateWindowExW(WS_EX_CONTROLPARENT, L"RR6Launcher", L"Ridge Racer 6", style, wx, wy, ww, wh,
                               nullptr, nullptr, instance, nullptr);
  LoadArtAtStart();
  BuildUi();
  LoadIntoControls();
  RememberBaseline();

  // "--save-and-exit" writes the settings file with the current choices and
  // quits; used for automated checks.
  if (args.find(L"--save-and-exit") != std::wstring::npos) {
    bool ok = SaveAll();
    return ok ? 0 : 1;
  }

  LoadArtInBackground();
  ShowWindow(app.window, SW_SHOW);
  UpdateWindow(app.window);
  SetFocus(Ctl(IDC_PLAY));
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    // Ctrl+Tab and Ctrl+Shift+Tab change the page.
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0 &&
        GetActiveWindow() == app.window) {
      int step = GetKeyState(VK_SHIFT) < 0 ? kPageCount - 1 : 1;
      ShowPage((app.current_page + step) % kPageCount);
      SetFocus(Ctl(IDC_TAB0 + app.current_page));
      continue;
    }
    if (!IsDialogMessageW(app.window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return 0;
}
