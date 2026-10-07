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
    run/p1.sh :93 Down 'stick 0,-32767' menu1 45   # one press for very slow frames, then a screenshot
    run/torace4.sh :93 tag   # "Single Race" highlighted -> race, 40 s between presses

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
- After the save has loaded, the main menu has a 3D scene behind it and a
  frame takes 4 to 16 seconds. Input is read once per frame, so a key is
  "held" for a whole frame however briefly it was pressed, and that can count
  as two steps in a menu. Use `p1.sh`, wait for the picture to settle, look,
  then press again. The title screen sometimes ignores the first Start.

`rig_probe.cpp` is an optional extra source file for the rig build only (add it
to `RR6RECOMP_SOURCES` in the rig's copy of `CMakeLists.txt`): every five seconds
it logs how many different slope-scale values were in the game's register copy
when the polygon-offset registers were sent to the GPU. Used to check
`src/depth_bias_fix.cpp`; start the game with `--rr6_depth_bias_stats=true` as
well and drive a race.

## Looking inside a frame (RenderDoc)

For a picture that is wrong rather than a crash. `renderdoc/` holds the
scripts that found the black track of Linux builds 01 and 02 (`SDK-NOTES.md`,
7). They run inside `qrenderdoc --python <script>`, which needs a display
(Xvfb will do) and, once, an answer to its first-run question.

    renderdoccmd vulkanlayer --register --user          # once
    renderdoccmd capture -d <build dir> -c run/rdc/rr6 -w ./rr6_recomp <the usual arguments> \
        --vulkan_sparse_shared_memory=false
    xdotool keydown F12; sleep 9; xdotool keyup F12     # one capture; repeat a few times

The game runs several times slower under RenderDoc. A capture is the time
between two presents, which is not one game frame: take a few and use a
large one.

    RDC=x.rdc OUT=out EVERY=40 qrenderdoc --python renderdoc/list_draws.py
        every draw with its shaders, textures and targets; pictures of the targets as the frame builds up
    RDC=x.rdc OUT=out EID=4604 TEX=690 PTS="720,700" qrenderdoc --python renderdoc/pixel_history.py
        which draws wrote a pixel, and what their shaders put out
    RDC=x.rdc OUT=out EIDS="2420" qrenderdoc --python renderdoc/dump_draw.py
        one draw: its textures as pictures, its constants, its shaders as text
    ALL=1 RDC=x.rdc OUT=out EID=2420 PT="720,700" PRIM=210 qrenderdoc --python renderdoc/debug_pixel.py
        the pixel shader stepped through for one pixel, every value logged

Saved pictures keep their alpha channel, so an area that looks white in a
viewer may be any colour with an alpha of zero.
