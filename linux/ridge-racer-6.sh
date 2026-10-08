#!/bin/bash
# Ridge Racer 6 - Linux test build. Starts the game.
#
# The first time, it asks for your own Ridge Racer 6 (USA) disc image, copies
# the game files out of it (about 6 GB, a few minutes), and writes settings
# that suit your screen. After that it just starts the game.
#
#   ./ridge-racer-6.sh [options]
#
#   --iso FILE       the disc image to copy the game files from
#   --copy-again     copy all the game files again (after a damaged copy)
#   --new-settings   write the settings file afresh for this screen
#   --windowed       this once, run in a window
#   --diagnostic     this once, record a detailed log (logs/run.log)
#   --wayland        this once, let the game pick Wayland instead of X11
#   --outside-steam  Steam Deck: start even though Steam did not start this
#   --install-dlc PATH   add downloadable content you own: one content file
#                    from an Xbox 360's storage, or a folder of them (can be
#                    given several times); then leave without starting the game
#   --help
#
# Settings are in bin/rr6_recomp.toml (a text file); F4 in the game changes
# them while playing. See README.txt.

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BIN="$HERE/bin"
GAME="$HERE/game"
LOGS="$HERE/logs"
CONFIG="$BIN/rr6_recomp.toml"
TITLE="Ridge Racer 6"

image=""
copy_again=0
new_settings=0
windowed=0
diagnostic=0
wayland=0
outside_steam=0
dlc=()
while [ $# -gt 0 ]; do
  case "$1" in
    --iso) image="${2:-}"; shift ;;
    --iso=*) image="${1#--iso=}" ;;
    --copy-again) copy_again=1 ;;
    --new-settings) new_settings=1 ;;
    --windowed) windowed=1 ;;
    --diagnostic) diagnostic=1 ;;
    --wayland) wayland=1 ;;
    --outside-steam) outside_steam=1 ;;
    --install-dlc|--install-dlc=*)
      if [ "$1" = --install-dlc ]; then item="${2:-}"; shift; else item="${1#--install-dlc=}"; fi
      [ -n "$item" ] || { echo "--install-dlc needs a content file or a folder (try --help)"; exit 2; }
      dlc+=("$(readlink -f -- "$item")") ;;
    --in-own-terminal) RR6_OWN_TERMINAL=1 ;;
    -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *.iso|*.ISO) image="$1" ;;
    *) echo "Unknown option: $1 (try --help)"; exit 2 ;;
  esac
  shift
done

have() { command -v "$1" >/dev/null 2>&1; }
graphical() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; }

# A message the player must see: in the terminal if there is one, and in a
# small window if the script was started from a file manager or from Steam.
tell() {  # tell error|info "text"
  local kind="$1" text="$2"
  echo
  echo "$text"
  if [ ! -t 1 ] && graphical; then
    if have kdialog; then
      if [ "$kind" = error ]; then kdialog --title "$TITLE" --error "$text"; else kdialog --title "$TITLE" --msgbox "$text"; fi
    elif have zenity; then
      zenity "--$kind" --title="$TITLE" --no-wrap --text="$text" 2>/dev/null
    elif have xmessage; then
      xmessage -center "$text"
    fi
  fi
}

# In a terminal window this script opened itself, wait before the window goes.
finish() {
  if [ -n "${RR6_OWN_TERMINAL:-}" ] && [ -t 0 ]; then
    echo
    read -r -p "Press Enter to close this window. " _
  fi
  exit "$1"
}

fail() { tell error "$1"; finish 1; }

# ---------------------------------------------------------------- the package

for f in "$BIN/rr6_recomp" "$BIN/librexruntime.so" "$BIN/librexgpu-xenos.so" "$BIN/rr6-extract"; do
  [ -f "$f" ] || fail "This folder is incomplete (${f#"$HERE/"} is missing). Unpack the whole archive again and start $(basename "$0") from inside the unpacked folder."
done
if [ ! -w "$HERE" ]; then
  fail "This folder cannot be written to. Move it to a place of your own, such as your home folder, and start the game from there."
fi
mkdir -p "$LOGS" "$GAME"

# ----------------------------------------------------------------- the system

[ "$(uname -m)" = x86_64 ] || fail "This build needs a 64-bit Intel or AMD processor (this system reports $(uname -m))."
if [ -r /proc/cpuinfo ] && ! grep -q -m1 -w sse4_1 /proc/cpuinfo; then
  fail "This build needs a processor with SSE4.1 (any Intel or AMD processor made since about 2011)."
fi
# Libraries. Started from Steam, the game inherits Steam's own library folders,
# which can hold older copies of system libraries than the system itself has;
# if the check only passes without them, the game is started without them.
lib_path="${LD_LIBRARY_PATH:-}"
# Prints what the program needs and the system lacks: library names, and the
# versions of the C and C++ libraries asked for.
missing_libraries() {  # missing_libraries <extra library path>
  LD_LIBRARY_PATH="$BIN${1:+:$1}" ldd "$BIN/rr6_recomp" "$BIN/librexruntime.so" "$BIN/librexgpu-xenos.so" 2>&1 |
    sed -n -e "s/.*version \`\([^']*\)' not found.*/  \1/p" -e 's/^[[:space:]]*\([^ ]*\) => not found.*/  \1/p' | sort -u
}
if have ldd; then
  missing="$(missing_libraries "$lib_path")"
  if [ -n "$missing" ] && [ -n "$lib_path" ] && [ -z "$(missing_libraries "")" ]; then
    missing=""
    lib_path=""
  fi
  if [ -n "$missing" ]; then
    if echo "$missing" | grep -q 'GLIBC'; then
      fail "This system is too old for this build. It needs glibc 2.35 and the C++ library of GCC 13.2 or newer: Ubuntu 24.04, Debian 13, Fedora 39, SteamOS 3.6, or anything newer or rolling (Arch, openSUSE Tumbleweed).

What is missing here:
$missing"
    else
      fail "Some system libraries the game needs are not installed:

$missing

They belong to the X11 and Wayland client libraries (libx11, libxcb, libwayland-client); install them with your distribution's package manager."
    fi
  fi
fi

# The game draws with Vulkan. Whether the library is there cannot be told for
# certain from here, so this is only kept as a hint for the case that the game
# does not start.
vulkan_hint=""
for tool in ldconfig /sbin/ldconfig /usr/sbin/ldconfig; do
  if have "$tool"; then
    if ! "$tool" -p 2>/dev/null | grep -q 'libvulkan\.so\.1'; then
      vulkan_hint="

No Vulkan library (libvulkan.so.1) was found on this system, which is the likely reason. Install your distribution's Vulkan packages: the loader (vulkan-loader, or libvulkan1) and the Vulkan driver for your graphics card (Mesa's Vulkan drivers, or the NVIDIA driver)."
    fi
    break
  fi
done

# ------------------------------------------- opening a terminal for the copy

game_ready() { [ -f "$GAME/default.xex" ] && [ -f "$GAME/.rr6-ready" ] && [ ! -f "$GAME/.rr6-copying" ]; }

# The disc-image copy shows its progress as text. Started from a file manager
# there is no terminal, so the script starts itself again inside one.
if { ! game_ready || [ "$copy_again" = 1 ]; } && [ ! -t 1 ] && [ -z "${RR6_OWN_TERMINAL:-}" ] && graphical; then
  again=("$0" --in-own-terminal)
  [ -n "$image" ] && again+=(--iso "$image")
  [ "$copy_again" = 1 ] && again+=(--copy-again)
  [ "$new_settings" = 1 ] && again+=(--new-settings)
  [ "$windowed" = 1 ] && again+=(--windowed)
  [ "$diagnostic" = 1 ] && again+=(--diagnostic)
  [ "$wayland" = 1 ] && again+=(--wayland)
  [ "$outside_steam" = 1 ] && again+=(--outside-steam)
  for item in "${dlc[@]}"; do again+=(--install-dlc "$item"); done
  if have konsole; then exec konsole -e "${again[@]}"
  elif have gnome-terminal; then exec gnome-terminal --wait -- "${again[@]}"
  elif have kgx; then exec kgx -- "${again[@]}"
  elif have ptyxis; then exec ptyxis -- "${again[@]}"
  elif have xfce4-terminal; then exec xfce4-terminal --disable-server -x "${again[@]}"
  elif have mate-terminal; then exec mate-terminal --disable-factory -x "${again[@]}"
  elif have lxterminal; then exec lxterminal -e "${again[@]}"
  elif have alacritty; then exec alacritty -e "${again[@]}"
  elif have kitty; then exec kitty "${again[@]}"
  elif have foot; then exec foot "${again[@]}"
  elif have xterm; then exec xterm -e "${again[@]}"
  fi
  fail "The game files have not been copied yet, and no terminal program was found to show the copy in. Open a terminal in this folder and run:  ./$(basename "$0")"
fi

# ---------------------------------------------------------------- game files

# What a path typed, pasted or dragged into a terminal can look like: in
# quotes, with backslashes before spaces, or as a file:// address.
clean_path() {
  local p="$1"
  p="${p#"${p%%[![:space:]]*}"}"
  p="${p%"${p##*[![:space:]]}"}"
  case "$p" in
    \'*\') p="${p#\'}"; p="${p%\'}" ;;
    \"*\") p="${p#\"}"; p="${p%\"}" ;;
  esac
  case "$p" in
    file://*) p="${p#file://}"; p="$(printf '%b' "${p//%/\\x}")" ;;
  esac
  if [ ! -e "$p" ] && [ -e "${p//\\/}" ]; then p="${p//\\/}"; fi
  # shellcheck disable=SC2088  # a tilde that was typed, not one for the shell
  case "$p" in
    "~/"*) p="$HOME/${p#"~/"}" ;;
  esac
  printf '%s' "$p"
}

choose_image() {
  local found="" picked=""
  # A disc image put into this folder is used without asking.
  for found in "$HERE"/*.iso "$HERE"/*.ISO; do
    if [ -f "$found" ]; then image="$found"; echo "Using the disc image in this folder: $(basename "$found")"; return; fi
  done
  if graphical; then
    echo "Choose your Ridge Racer 6 (USA) disc image (.iso) in the window that opens."
    if have kdialog && { [[ "${XDG_CURRENT_DESKTOP:-}" == *KDE* ]] || ! have zenity; }; then
      picked="$(kdialog --title "Choose your Ridge Racer 6 (USA) disc image" --getopenfilename "$HOME" "*.iso *.ISO|Disc image (*.iso)" 2>/dev/null)"
    elif have zenity; then
      picked="$(zenity --file-selection --title="Choose your Ridge Racer 6 (USA) disc image" --file-filter="Disc image (*.iso) | *.iso *.ISO" --file-filter="All files | *" 2>/dev/null)"
    fi
  fi
  if [ -z "$picked" ] && [ -t 0 ]; then
    echo "Type the path of your Ridge Racer 6 (USA) disc image (.iso), or drag the file into this"
    echo "window, and press Enter. (Just Enter gives up.)"
    read -r -e picked
  fi
  image="$(clean_path "$picked")"
}

if ! game_ready || [ "$copy_again" = 1 ]; then
  echo "$TITLE: the game files have to be copied out of your disc image first."
  echo "This build contains no game data; it works with the USA disc only."
  echo
  if [ -n "$image" ]; then image="$(clean_path "$image")"; else choose_image; fi
  [ -n "$image" ] || fail "No disc image was chosen, so the game cannot start yet. Start $(basename "$0") again when you have your Ridge Racer 6 (USA) disc image (.iso)."
  [ -f "$image" ] || fail "There is no such file: $image"
  extra=()
  [ "$copy_again" = 1 ] && extra+=(--overwrite)
  "$BIN/rr6-extract" "$image" "$GAME" "${extra[@]}"
  status=$?
  if [ "$status" -ne 0 ]; then
    [ "$status" = 130 ] && finish 130
    tell error "The game files were not copied (the reason is in the terminal window). The game has not been started."
    finish 1
  fi
  echo
  echo "The disc image is not needed any more; you can delete it or keep it somewhere else."
  echo
fi

# ------------------------------------------------------------------ settings

# Prints the size of the main screen as "width height", or nothing.
screen_size() {
  local out=""
  if [ -n "${DISPLAY:-}" ] && have xrandr; then
    out="$(xrandr --current 2>/dev/null | awk '
      / connected/ {
        for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+x[0-9]+\+[0-9]+\+[0-9]+$/) {
          split($i, a, /[x+]/); size = a[1] " " a[2]
          if (first == "") first = size
          if ($0 ~ / primary /) { print size; found = 1; exit }
        }
      }
      END { if (!found && first != "") print first }')"
  fi
  if [ -z "$out" ] && [ -n "${DISPLAY:-}" ] && have xdpyinfo; then
    out="$(xdpyinfo 2>/dev/null | awk '/dimensions:/ { split($2, a, "x"); print a[1] " " a[2]; exit }')"
  fi
  printf '%s' "$out"
}

# The machine itself is a Steam Deck (LCD: Jupiter, OLED: Galileo).
deck_hardware() {
  local file="${RR6_PRODUCT_NAME_FILE:-/sys/devices/virtual/dmi/id/product_name}" name=""
  [ -r "$file" ] && name="$(cat "$file" 2>/dev/null)"
  [ "$name" = Jupiter ] || [ "$name" = Galileo ]
}

on_steam_deck() {
  [ -f "$HERE/steam-deck" ] && return 0
  deck_hardware
}

write_settings() {
  local w=1920 h=1080 size="" note="" numbers="" aspect letterbox scale_x scale_y
  if on_steam_deck; then
    # The Deck's own screen is 1280x800. The game is drawn once at its original
    # 1280x720, which the Deck shows pixel for pixel with thin bars above and
    # below. A sharper picture costs speed and battery: see README.txt.
    note="a Steam Deck"
    aspect=0; letterbox=true; scale_x=1; scale_y=1
  else
    size="$(screen_size)"
    if [ -n "$size" ]; then w="${size% *}"; h="${size#* }"; note="a $w x $h screen"; else note="an unknown screen, taken as 1920 x 1080"; fi
    # The same rules as the Windows launcher's "Automatic": the game draws
    # 1280x720 times a whole number, the next one up from the screen's height
    # (screens only a little taller than a multiple are not worth the extra
    # work), and a screen wider than 16:9 is filled, with the width drawn
    # proportionally larger.
    numbers="$(awk -v w="$w" -v h="$h" 'function ceil(v) { return v == int(v) ? v : int(v) + 1 }
      BEGIN {
        aspect = w / h
        sy = ceil(h / 720 - 0.25); if (sy < 1) sy = 1; if (sy > 4) sy = 4
        sx = sy
        if (aspect > 1.80) {
          sx = ceil(sy * aspect / (16 / 9) - 0.01); if (sx < sy) sx = sy; if (sx > 8) sx = 8
          printf "%.4f false %d %d", aspect, sx, sy
        } else {
          printf "0 true %d %d", sx, sy
        }
      }')"
    read -r aspect letterbox scale_x scale_y <<<"$numbers"
  fi
  cat > "$CONFIG" <<TOML
# Ridge Racer 6 settings. Written by ridge-racer-6.sh for $note.
# Edit this file with any text editor, or press F4 in the game. To have it
# written afresh (after changing the screen, say): ./ridge-racer-6.sh --new-settings
# The game always runs at 60 frames per second; these only change the picture.

fullscreen = true                   # false = run in a window

# Picture shape. 0 with present_letterbox = true keeps the original 16:9 picture
# with bars where the screen has another shape. To fill a wider screen, put the
# screen's width divided by its height here (3440 / 1440 = 2.3889) and set
# present_letterbox = false.
rr6_aspect_ratio = $aspect
present_letterbox = $letterbox
rr6_hud_fix = true                  # keeps menus and the race display in shape on a wide picture
rr6_hud_edges = true                # wide picture: race display at the screen edges (false = where 16:9 had it)

# How large the game is drawn inside: 1 = 1280x720, as on the Xbox 360,
# 2 = 2560x1440, 3 = 3840x2160. Larger is sharper and needs a faster graphics
# card; lower both if the game runs slowly. On a filled wide screen the width
# (x) is larger than the height (y).
draw_resolution_scale_x = $scale_x
draw_resolution_scale_y = $scale_y

swap_post_effect = "none"           # edge smoothing: "none", "fxaa" or "fxaa_extreme"
anisotropic_override = 5            # texture detail on the road: 3 = 4x, 4 = 8x, 5 = 16x
use_fuzzy_alpha_epsilon = true      # stops trees and foliage flickering

# Keyboard: works together with a controller. Several keys can be given for one
# button, separated by commas. Keys do not respond while Shift, Ctrl or Alt is
# held. Controllers need no settings.
mnk_mode = true                     # false = ignore the keyboard
mnk_mouse = false                   # leave the mouse pointer alone
keybind_lstick_left = "Left,A"      # steer left
keybind_lstick_right = "Right,D"    # steer right
keybind_lstick_up = "Up,W"          # menu up
keybind_lstick_down = "Down,S"      # menu down
keybind_right_trigger = "Up,W"      # RT / R2: accelerate
keybind_left_trigger = "Down,S"     # LT / L2: brake
keybind_a = "Space"                 # A / Cross: confirm
keybind_b = "Backspace,B"           # B / Circle: cancel
keybind_x = "X"                     # X / Square
keybind_y = "Y"                     # Y / Triangle
keybind_right_shoulder = "E"        # RB / R1
keybind_left_shoulder = "Q"         # LB / L1
keybind_start = "Return,P"          # Start / Options: pause
keybind_back = "Tab"                # Back / Create
keybind_dpad_up = "Numpad8"
keybind_dpad_down = "Numpad2"
keybind_dpad_left = "Numpad4"
keybind_dpad_right = "Numpad6"
keybind_rstick_up = "I"
keybind_rstick_down = "K"
keybind_rstick_left = "J"
keybind_rstick_right = "L"
keybind_lstick_press = "N"          # left stick click / L3
keybind_rstick_press = "M"          # right stick click / R3
TOML
  echo "Settings written for $note: bin/rr6_recomp.toml"
}

if [ ! -f "$CONFIG" ] || [ "$new_settings" = 1 ]; then
  write_settings
fi

# ------------------------------------------------------------------ the game

args=(--game_data_root "$GAME" --gpu_plugin=xenos --log_file "$LOGS/run.log")
[ "$windowed" = 1 ] && args+=(--fullscreen=false)
[ "$diagnostic" = 1 ] && args+=(--log_level debug --log_flush_interval 1 --log_max_file_size_mb 50 --log_max_files 3)

# X11 (through XWayland on a Wayland desktop, and in the Steam Deck's Game
# Mode) is the way this build has been run; --wayland leaves the choice open.
if [ "$wayland" = 0 ]; then
  export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-x11}" SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
fi
export LD_LIBRARY_PATH="$BIN${lib_path:+:$lib_path}"

# On Linux a fault inside the game's own code does not end the program: it
# repeats the same fault without end, with a frozen picture, and its log rolls
# over so quickly that the cause is gone within a minute. This watches the log
# for that, keeps the part up to the first fault (logs/fault.txt) and stops
# the game.
watch_for_fault() {  # watch_for_fault <process id of the game>
  local f faults
  while kill -0 "$1" 2>/dev/null; do
    sleep 3
    [ -f "$LOGS/run.log" ] || continue
    faults="$(grep -a -c 'Unhandled guest' "$LOGS/run.log" 2>/dev/null)"
    if [ "${faults:-0}" -ge 20 ] || grep -a -q -m1 'Unhandled guest' "$LOGS/run.1.log" 2>/dev/null; then
      # The oldest log that has the fault in it has its beginning.
      while IFS= read -r f; do
        if grep -a -q -m1 'Unhandled guest' "$f"; then
          grep -a -m1 -B 400 'Unhandled guest' "$f" > "$LOGS/fault.txt"
          break
        fi
      done < <(find "$LOGS" -maxdepth 1 -name 'run*.log' -printf '%T@ %p\n' 2>/dev/null | sort -n | cut -d' ' -f2-)
      kill -9 "$1" 2>/dev/null
      return
    fi
  done
}

# The game keeps its memory in a file in /dev/shm, which is left behind (a few
# hundred MB of memory, until the next restart) whenever the game is stopped
# hard rather than quitting. Clear such leftovers when no game is running.
clear_leftovers() {
  if ! pgrep -x rr6_recomp >/dev/null 2>&1; then
    rm -f /dev/shm/xenia_memory_* 2>/dev/null
  fi
}
clear_leftovers

# ------------------------------------------------------ downloadable content
#
# --install-dlc: the game program unpacks the player's own content packages
# itself (src/dlc_install.cpp in the game's source), writes what it did to
# logs/dlc-install-result.txt, and closes again.
if [ ${#dlc[@]} -gt 0 ]; then
  list=""
  for item in "${dlc[@]}"; do
    list="${list:+$list|}$item"
  done
  result="$LOGS/dlc-install-result.txt"
  rm -f "$result"
  echo "Adding downloadable content. A game window opens for a moment and closes again."
  cd "$BIN" || fail "The folder $BIN cannot be entered."
  ./rr6_recomp --game_data_root "$GAME" --gpu_plugin=xenos --fullscreen=false \
    --log_file "$LOGS/dlc-install.log" "--rr6_install_result=$result" \
    "--rr6_install_content=$list" > "$LOGS/dlc-install-console.txt" 2>&1
  clear_leftovers
  if [ ! -f "$result" ]; then
    fail "The content could not be added: the game program did not report back.

Check that the game itself starts. The log of this attempt is logs/dlc-install.log."
  fi
  # The game writes the whole file at once, ending with a "done" line; a file
  # without one was cut off.
  summary="$(awk -F'\t' '
    NR == 1 { complete = ($0 == "RR6-DLC-INSTALL 1") ? 1 : 0; next }
    $1 == "done" && NF == 4 { done = 1 }
    $1 == "installed" { added = added "\n    " $2; n++ }
    $1 == "refused" || $1 == "failed" { bad = bad "\n    " $3 ": " $2; m++ }
    END {
      if (!(complete && done)) {
        bad = bad "\n    The game program stopped before it was done (see logs/dlc-install.log)."
        m++
        printf "Adding content did not finish (%d added before it stopped).", n
      } else {
        printf "%d added, %d not added.", n, m
      }
      if (n) printf "\n\nAdded:%s", added
      if (m) printf "\n\nNot added:%s", bad
      if (n) printf "\n\nThe game looks for added content each time it starts."
      exit (m > 0) ? 1 : 0
    }' "$result")"
  status=$?
  tell info "$summary"
  finish "$status"
fi

# ------------------------------------- Steam Deck: started from outside Steam
#
# The Deck's own buttons reach a program as an Xbox controller only when Steam
# starts that program. Started any other way in Desktop Mode (a double click,
# a terminal), they act as a keyboard and mouse: Y sends Space, A sends Enter,
# the triggers send mouse clicks. The game takes Space for its A button and has
# nothing to accelerate with. So before starting, say so and offer to put the
# game into Steam. "Start anyway" is remembered (for a controller plugged in,
# or a keyboard).
started_by_steam() {
  [ -n "${SteamGameId:-}${SteamOverlayGameId:-}${SteamClientLaunch:-}${GAMESCOPE_WAYLAND_DISPLAY:-}" ] && return 0
  case "${LD_PRELOAD:-}" in *gameoverlayrenderer*) return 0 ;; esac
  [ "${XDG_CURRENT_DESKTOP:-}" = gamescope ]
}

add_to_steam() {
  local me
  me="$HERE/$(basename "$0")"
  if have steamos-add-to-steam && steamos-add-to-steam "$me" >/dev/null 2>&1; then
    tell info "Added. In Steam the game is under Library > Non-Steam, as $(basename "$0"); the gear icon > Properties renames it.

Start it from there. It plays best in Gaming Mode (\"Return to Gaming Mode\" on the desktop)."
  else
    tell info "It could not be added automatically. To add it by hand: in the Steam window choose Games > \"Add a Non-Steam Game to My Library\", then Browse, and pick
$me

Then start it from Steam (Library > Non-Steam), best in Gaming Mode."
  fi
}

if deck_hardware && ! started_by_steam && [ "$outside_steam" = 0 ] && [ ! -f "$BIN/.outside-steam-ok" ]; then
  notice="This was started from outside Steam. Here the Steam Deck's own buttons work as a keyboard and mouse, not as a controller, so the game's controls will be wrong (Y confirms, nothing accelerates).

Start the game from Steam instead: Library > Non-Steam, best in Gaming Mode. The game files are copied and ready."
  choice=""
  if [ -t 0 ] && [ -t 1 ]; then
    echo
    echo "$notice"
    echo
    read -r -p "Type a to add the game to Steam, s to start it anyway, or just Enter to close: " choice
  elif graphical && have kdialog; then
    kdialog --title "$TITLE" --warningyesnocancel "$notice

Add the game to Steam now?" --yes-label "Add to Steam" --no-label "Start anyway" --cancel-label "Close"
    case $? in 0) choice=a ;; 1) choice=s ;; *) choice="" ;; esac
  elif graphical && have zenity; then
    if zenity --question --title="$TITLE" --no-wrap --ok-label="Add to Steam" --cancel-label="Start anyway" --text="$notice

Add the game to Steam now?" 2>/dev/null; then choice=a; else choice=s; fi
  else
    echo
    echo "$notice"
    choice=s
  fi
  case "$choice" in
    a|A) add_to_steam; finish 0 ;;
    s|S) : > "$BIN/.outside-steam-ok" ;;
    *) finish 0 ;;
  esac
fi

echo "Starting $TITLE. To leave the game: Esc, or hold Back + Start on a controller."
cd "$BIN" || fail "The folder $BIN cannot be entered."
rm -f "$LOGS"/run*.log "$LOGS/fault.txt" "$LOGS/console.txt" "$LOGS/last-exit.txt"
./rr6_recomp "${args[@]}" > "$LOGS/console.txt" 2>&1 &
game=$!
trap 'kill "$game" 2>/dev/null' INT TERM HUP
watch_for_fault "$game" &
watcher=$!
{ wait "$game"; } 2>/dev/null
status=$?
kill "$watcher" 2>/dev/null
trap - INT TERM HUP
clear_leftovers
printf 'exit status %s\n' "$status" > "$LOGS/last-exit.txt"
report="To report it, run tools/collect-report.sh in this folder and send the file it makes. Starting the game with --diagnostic first records more detail."
if [ -f "$LOGS/fault.txt" ]; then
  tell error "The game ran into an error it could not get past, and has been stopped.

$report"
  finish 1
fi
if [ "$status" -ne 0 ]; then
  tell error "The game stopped with a problem (exit status $status).$vulkan_hint

$report"
  finish "$status"
fi
finish 0
