# Linux test rig

Scripts used to run the Linux build of the game without a GPU (Mesa software
Vulkan), drive it with the keyboard and take screenshots. Slow (a few frames
per second) but enough to check menus, saves, input and 2D layout.

Expected layout under `$RR6_RIG`:

    sdk/linux-amd64/                 ReXGlue SDK v0.10.0, Linux package
    rr6-recomp/                      this project, built into out/build/linux-check
    game/                            the extracted disc contents
    run/                             these scripts, logs, screenshots

One-time setup (Ubuntu 24.04, as root):

    apt-get install xvfb openbox xdotool imagemagick mesa-vulkan-drivers \
                    pulseaudio pulseaudio-utils libasound2-plugins
    Xvfb :92 -screen 0 1280x720x24 &   DISPLAY=:92 openbox &
    Xvfb :93 -screen 0 1720x720x24 &   DISPLAY=:93 openbox &     # "ultrawide"
    export XDG_RUNTIME_DIR=/tmp/xdg-rr6; mkdir -p $XDG_RUNTIME_DIR; chmod 700 $XDG_RUNTIME_DIR
    pulseaudio --start --exit-idle-time=-1 -n --load=module-native-protocol-unix \
        --load="module-null-sink sink_name=nullsink rate=48000 channels=6" --load=module-always-sink

Use:

    run/start.sh :93 settings.toml --rr6_log_input=true --rr6_hud_stats=true
    run/flow1.sh :93 uw      # boot -> title screen
    run/press.sh :93 start   # one confirmed button press (a, b, x, y, start, up, down, left, right, lb, rb)
    run/shot.sh  :93 name 5  # wait 5 s, screenshot to run/seq/name.png
    run/stop.sh

Things that cost time to find out:

- A window manager is required. Without one the game window's keyboard focus
  comes and goes and key presses are silently dropped.
- ALSA's `null` device does not pace playback; the audio thread then burns a
  whole core. Route ALSA to a PulseAudio null sink (`asound-pulse.conf`).
- At these frame rates a short key press can fall between two input polls.
  `press.sh` holds the key until the game's own log (`rr6_log_input`) shows it.
- Do not renice the game's threads to favour rendering: the loader thread
  starves and loading screens never end.
- `strace` on the process slows it to a crawl (the runtime re-reads
  /sys/devices/system/cpu/online constantly); attach it only for a few seconds.
- Boot to the title screen takes 3-6 minutes; a race takes another 4-10.

`rig_probe.cpp` is an optional extra source file for the rig build only (add it
to `RR6RECOMP_SOURCES` in the rig's copy of `CMakeLists.txt`): every five seconds
it logs how many different slope-scale values were in the game's register copy
when the polygon-offset registers were sent to the GPU. Used to check
`src/depth_bias_fix.cpp`; start the game with `--rr6_depth_bias_stats=true` as
well and drive a race.
