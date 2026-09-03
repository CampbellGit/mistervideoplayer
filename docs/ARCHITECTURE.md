# Architecture

## Why this needs no FPGA/HDL work

Confirmed directly against MiSTer source (`MiSTer-devel/Main_MiSTer`, `MiSTer-devel/Template_MiSTer`):

- **Video**: launching a program from the Scripts menu with `fb_terminal=1` set in `MiSTer.ini` makes `Main_MiSTer` switch a Linux virtual terminal to the framebuffer path (`menu.cpp`, state `MENU_SCRIPTS_FB`: `video_chvt(2)` + `video_fb_enable(1)` before forking the script onto `tty2`). Once that's active, `/dev/fb0` is a normal Linux framebuffer device that any program can `mmap` and draw into (32bpp BGRX8888, matching `FB_EN|FB_FMT_RxB|FB_FMT_8888` in `video.cpp`), and the FPGA's existing scaler/scandoubler pipeline displays it -- the same pipeline used for CRT (15kHz/31kHz) and HDMI output on any core.
- **Audio**: the standard framework (`Template_MiSTer/sys/`) has a dedicated `alsa.sv` module feeding an `alsa_l`/`alsa_r` mixer input into `audio_out.sv`, present in every core automatically. This surfaces as a normal ALSA playback device on Linux. It is a single, non-shareable device -- only one process can hold it open at a time.
- **CRT vs HDMI**: entirely existing `MiSTer.ini` configuration (`direct_video`, `vga_scaler`, `video_mode`, etc.), set by the user for their display. Nothing in this project touches that.

No patch to `Main_MiSTer`, no custom FPGA core, no HDL.

## Program structure

- `src/fb.c` -- opens/mmaps the framebuffer device. Falls back to a plain-file mock (sized via `VPLAYER_FB_WIDTH`/`VPLAYER_FB_HEIGHT`) when the target isn't a real fb device, for desktop testing.
- `src/decode.c` -- ffmpeg (`libavformat`/`libavcodec`/`libswscale`/`libswresample`) demux/decode/scale/resample, one call (`decoder_step`) at a time so the caller can interleave input polling and AV-sync pacing between frames.
- `src/audio.c` -- ALSA output (48kHz stereo S16LE, matching the framework's fixed rate).
- `src/input.c` -- evdev polling across `/dev/input/event*`.
- `src/browser.c` -- a plain-text (ANSI) directory browser drawn to stdout, since the whole program runs on a real Linux VT (same mechanism `less`/`pdfviewer` use for MiSTer's "Documents" viewer) -- no need to render a bitmap-font UI into the pixel framebuffer just to list files. The pixel framebuffer is only used for actual video frames.
- `src/main.c` -- ties it together; audio write blocking on the real ALSA device is what paces the main loop close to realtime, with a small explicit sync check for video frame timing against the audio clock (or wall clock, for video-only files).

## Known simplifications (v1)

- No hardware SPI page-flip trick for tear-free interlaced output -- just `FBIOPAN_DISPLAY`-style vsync-wait. Revisit only if plain vsync-wait proves too tearing-prone on real interlaced CRT modes.
- No decoder flush/drain at EOF -- the last buffered frame or two may be dropped when a file ends. Cosmetic, not a crash.
- The `dist/` bundle currently ships the *full* transitive shared-library closure Debian's ffmpeg packages pull in (~90MB, 119 files), most of which (librsvg, libpango, libx265, libvpx, ICU data, ...) we never actually exercise. Works, but a future pass building a slimmed static ffmpeg (only the demuxers/decoders we need) would be considerably leaner.

## Hardware verification checklist

- [x] `fb_terminal=1` + Scripts-menu launch yields a working `/dev/fb0` -- confirmed on real hardware (first real-hardware run: picture showed up, but as a "windowed" box rather than filling the CRT -- see below).
- [ ] Picture fills the full 15kHz CRT raster (not a smaller centered box). Under investigation -- likely `fb_size` (try `1`, full size, instead of `0`/automatic) and/or the Menu core's active video mode not matching what the analog output path expects. Not yet confirmed fixed.
- [ ] Picture on 31kHz CRT/arcade monitor (`vga_scaler=1`).
- [ ] Picture over HDMI.
- [ ] Audio actually reaches the analog/HDMI output via ALSA `"default"`.
- [ ] Real playback smoothness/tearing across representative mp4 (H.264), mkv, avi (Xvid/DivX), mov files at a few resolutions/bitrates -- establish the actual Cortex-A9 ceiling.
- [x] Controller input mapping (`src/input.c`) -- fixed and verified against synthetic evdev events (a real Linux gamepad reports `BTN_SOUTH`/`BTN_EAST`/`ABS_HAT0X`/`ABS_HAT0Y`, not the `KEY_*` codes the code originally only handled). Verified with a synthetic `uinput` virtual gamepad emitting hat + button events and confirming `input_poll()` returns the right actions, in a container -- **not yet confirmed against a real physical pad on real MiSTer hardware.**
