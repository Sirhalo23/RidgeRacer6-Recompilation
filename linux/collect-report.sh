#!/bin/bash
# Packs what is needed to look into a problem into bug-report.tar.gz, next to
# the start script: the game's logs, the settings, and a description of the
# system. The user name, the home folder and the computer's name are replaced
# in the text files. Nothing is sent anywhere; you send the file yourself.

HERE="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
OUT="$HERE/bug-report.tar.gz"
WORK="$(mktemp -d)" || exit 1
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/bug-report"
R="$WORK/bug-report"

have() { command -v "$1" >/dev/null 2>&1; }
user="$(id -un 2>/dev/null)"
host="$(hostname 2>/dev/null || cat /etc/hostname 2>/dev/null)"
# Very short names would hit ordinary words; those are only taken out of paths.
[ "${#user}" -lt 3 ] && user=""
[ "${#host}" -lt 3 ] && host=""

# Copies a text file with the personal names taken out.
scrub() {
  sed -e "s#${HOME:-/nonexistent-home}#~#g" \
      -e 's#/home/[^/ "'"'"':]*#/home/<user>#g' \
      ${user:+-e "s#\\b$user\\b#<user>#g"} \
      ${host:+-e "s#\\b$host\\b#<computer>#g"} "$1"
}

{
  echo "Ridge Racer 6 Linux test build - system description"
  echo "made: $(date -u '+%Y-%m-%d %H:%M UTC')"
  [ -f "$HERE/BUILD.txt" ] && cat "$HERE/BUILD.txt"
  echo
  echo "== system"
  if [ -r /etc/os-release ]; then grep -E '^(PRETTY_NAME|VERSION_ID|ID|VARIANT_ID|BUILD_ID)=' /etc/os-release; fi
  echo "kernel: $(uname -sr) $(uname -m)"
  echo "glibc: $(getconf GNU_LIBC_VERSION 2>/dev/null)"
  [ -r /sys/devices/virtual/dmi/id/product_name ] && echo "machine: $(cat /sys/devices/virtual/dmi/id/sys_vendor 2>/dev/null) $(cat /sys/devices/virtual/dmi/id/product_name 2>/dev/null)"
  echo "session: ${XDG_SESSION_TYPE:-?} / ${XDG_CURRENT_DESKTOP:-?}  DISPLAY=${DISPLAY:+set} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:+set}"
  [ -n "${SteamDeck:-}${SteamGameId:-}" ] && echo "started from Steam: yes"
  echo
  echo "== processor and memory"
  grep -m1 'model name' /proc/cpuinfo 2>/dev/null
  echo "logical processors: $(grep -c '^processor' /proc/cpuinfo 2>/dev/null)"
  grep -m1 -o -w 'sse4_1' /proc/cpuinfo 2>/dev/null; grep -m1 -o -w 'avx2' /proc/cpuinfo 2>/dev/null
  grep -E '^(MemTotal|MemAvailable|SwapTotal)' /proc/meminfo 2>/dev/null
  echo
  echo "== graphics"
  if have lspci; then lspci 2>/dev/null | grep -i -E 'vga|3d|display'; fi
  if have vulkaninfo; then
    vulkaninfo --summary 2>&1 | grep -E 'apiVersion|driverVersion|deviceName|deviceType|driverName|driverInfo|GPU[0-9]|Vulkan Instance' | head -60
  else
    echo "vulkaninfo is not installed (package vulkan-tools); the Vulkan driver is not described here."
  fi
  for tool in ldconfig /sbin/ldconfig /usr/sbin/ldconfig; do
    if have "$tool"; then "$tool" -p 2>/dev/null | grep -E 'libvulkan\.so\.1|libstdc\+\+\.so\.6' | sed 's/^[[:space:]]*//'; break; fi
  done
  echo
  echo "== screens"
  if [ -n "${DISPLAY:-}" ] && have xrandr; then xrandr --current 2>/dev/null | grep -E ' connected|^Screen'; else echo "(xrandr not available)"; fi
  echo
  echo "== the program"
  if have ldd; then LD_LIBRARY_PATH="$HERE/bin" ldd "$HERE/bin/rr6_recomp" 2>&1 | grep -E 'not found|rex|stdc|libc\.so|vulkan' ; fi
  ( cd "$HERE/bin" 2>/dev/null && stat -c '%s %n' rr6_recomp librexruntime.so librexgpu-xenos.so rr6-extract 2>&1 )
  echo
  echo "== files"
  echo "game files: $(find "$HERE/game" -maxdepth 1 -type f 2>/dev/null | wc -l) files, ready marker: $([ -f "$HERE/game/.rr6-ready" ] && echo yes || echo no)"
  df -h "$HERE" 2>/dev/null | tail -1 | awk '{print "free space here: " $4}'
  echo "save data folder: $([ -d "$HOME/.local/share/rr6_recomp" ] && echo present || echo absent)"
} 2>&1 | scrub /dev/stdin > "$R/system.txt"

for f in "$HERE"/logs/last-exit.txt "$HERE"/logs/console.txt "$HERE"/logs/fault.txt "$HERE"/bin/rr6_recomp.toml; do
  [ -f "$f" ] && scrub "$f" > "$R/$(basename "$f")"
done
# The game's log, and the one before it if the game kept one. Long logs are cut
# to their first and last parts.
for f in "$HERE"/logs/run.log "$HERE"/logs/run.1.log "$HERE"/logs/run.log.1; do
  [ -f "$f" ] || continue
  name="$(basename "$f").txt"
  if [ "$(wc -c < "$f")" -gt 20000000 ]; then
    { head -c 4000000 "$f"; printf '\n\n[... the middle of this log was left out ...]\n\n'; tail -c 12000000 "$f"; } | scrub /dev/stdin > "$R/$name"
  else
    scrub "$f" > "$R/$name"
  fi
done

if tar -C "$WORK" -czf "$OUT" bug-report; then
  echo "Made: $OUT ($(du -h "$OUT" | cut -f1))"
  echo "Send this file with a few words on what happened and where in the game."
else
  echo "The report could not be written to $OUT."
  exit 1
fi
if [ ! -t 0 ] || [ -n "${RR6_OWN_TERMINAL:-}" ]; then
  if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    if have kdialog; then kdialog --title "Ridge Racer 6" --msgbox "The report is ready: bug-report.tar.gz in the game's folder. Send that file with a few words on what happened."
    elif have zenity; then zenity --info --title="Ridge Racer 6" --text="The report is ready: bug-report.tar.gz in the game's folder. Send that file with a few words on what happened." 2>/dev/null
    fi
  fi
fi
