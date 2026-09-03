# vplayer

A local video player for the [MiSTer FPGA](https://misterfpga.org) platform. Browse video files stored on the SD card and play them back on the analog CRT output (15kHz or 31kHz, depending on your `MiSTer.ini`) or HDMI, with audio.

Decoding is done in software (ffmpeg) on the ARM/Linux ("HPS") side -- there is no custom FPGA core or HDL involved. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for how this fits into MiSTer's existing framework.

## Status

Early, hardware-untested. The decode/scale/framebuffer pipeline has been verified against real ffmpeg-decoded video inside a Linux container (see "Testing" below), but nothing here has run on an actual MiSTer yet -- see the checklist in `docs/ARCHITECTURE.md`.

## Building

Requires Docker (used only as a disposable armhf cross-compilation environment -- no other host dependency).

```sh
make docker-package
```

This cross-builds `vplayer-arm` and bundles it with the shared libraries it needs (ffmpeg, ALSA) into `dist/`, since MiSTer's Linux image doesn't ship those. The bundle is currently large (~90MB, because Debian's `libavformat`/`libavcodec` packages pull in many optional protocol/codec integrations we don't use) -- fine for an SD card, but a future optimization would be a slimmed static ffmpeg build with only the demuxers/decoders we need.

## Installing on MiSTer

Either grab a prebuilt zip from [Releases](../../releases) (built by `.github/workflows/release.yml` on every `v*` tag) -- it already has the `video-player/` and `Scripts/` folders laid out ready to copy -- or build locally with `make docker-package` (see above), which produces the same `video-player/` contents under `dist/`. You can copy files to the SD card either by pulling the card out and using a reader on your computer, or over the network with `scp` (MiSTer listens on SSH as `root@<mister-ip-or-hostname>`, default password `1`).

1. **Enable the framebuffer for Scripts.** Edit `MiSTer.ini` on the SD card (root of the `fat` partition) and set:
   ```ini
   fb_terminal=1
   ```
   This is a standard, existing MiSTer option -- nothing here patches `Main_MiSTer`. It's what lets a program launched from the Scripts menu take over the display. If your `[MiSTer]` section already has other settings, just add the line inside it.

2. **Copy the app.** From a release zip: extract it and copy its `video-player/` folder as-is to `/media/fat/video-player/`. From a local build: copy `dist/` there instead. Either way you should end up with:
   ```
   /media/fat/video-player/vplayer          (launcher wrapper)
   /media/fat/video-player/vplayer-arm       (the actual binary)
   /media/fat/video-player/lib/*.so*         (bundled ffmpeg/ALSA libraries)
   ```
   Example over SSH from this repo, after building locally:
   ```sh
   scp -r dist root@mister.local:/media/fat/video-player
   ```

3. **Copy the launcher script.** Copy `Scripts/VideoPlayer.sh` (from the release zip) or `scripts/VideoPlayer.sh` (from this repo) to `/media/fat/Scripts/VideoPlayer.sh` (this is what makes it show up in the Scripts menu):
   ```sh
   scp scripts/VideoPlayer.sh root@mister.local:/media/fat/Scripts/VideoPlayer.sh
   ```

4. **Add your videos.** Create `/media/fat/video/` on the SD card and put your video files there (subfolders are fine -- the on-screen browser lets you navigate into them).

5. **Launch it.** From the MiSTer main menu, open **Scripts**, then select **VideoPlayer**. In the browser: up/down to move through the list, Enter to open a folder or play a file, Esc to go back a folder (or quit at the top level). During playback: left/right to seek back/forward 10 seconds, `p` to pause, Esc/`q` to stop and return to the browser.

**Exiting all the way back to the MiSTer menu takes two presses at the top level, not one.** The first Esc/Back exits the player correctly -- but `Main_MiSTer` itself doesn't redraw its own menu until it sees one more key-release *after* that (this is built into `Main_MiSTer`'s Scripts-launcher state machine, the same mechanism the "Documents" PDF/text viewer uses -- not something this app controls). If the MiSTer menu doesn't reappear after quitting, press Back (or any key) once more.

If nothing shows up on screen, double check `fb_terminal=1` is actually set and the SD card was ejected/saved properly -- this is the single most likely thing to get wrong, and it's a symptom that looks identical to "the app crashed" from the outside.

## Testing without hardware

There's no MiSTer hardware or FPGA toolchain in a typical dev sandbox, so most iteration happens against a native Linux build instead of the ARM cross-build:

```sh
docker build -t vplayer-test -f docker/Dockerfile.test docker
docker run --rm -v $(pwd):/build -w /build vplayer-test sh -c '
  make clean && CC=gcc make all
  ffmpeg -y -f lavfi -i "testsrc=size=320x240:rate=25:duration=3" \
         -f lavfi -i "sine=frequency=440:duration=3" \
         -c:v libx264 -pix_fmt yuv420p -c:a aac /tmp/test.mp4
  VPLAYER_FB_WIDTH=320 VPLAYER_FB_HEIGHT=240 VPLAYER_FB_DEVICE=/tmp/fb.raw \
    VPLAYER_AUDIO_DEVICE=null ./vplayer /tmp/test.mp4
'
```

`VPLAYER_FB_DEVICE`/`VPLAYER_FB_WIDTH`/`VPLAYER_FB_HEIGHT` make `fb_open()` fall back to a plain mmap'd file instead of a real `/dev/fb0` when the target isn't an actual framebuffer device, and `VPLAYER_AUDIO_DEVICE=null` uses ALSA's null sink instead of real hardware. This validates the decode/scale/present and decode/resample/output logic, but obviously not real tearing, real audio, or real CRT timing -- those need the real hardware checklist in `docs/ARCHITECTURE.md`.

## Supported formats

Decoding goes through ffmpeg (`libavformat`/`libavcodec`), so it's broad by construction rather than a hand-picked list -- basically anything the installed ffmpeg build can demux/decode will play, including H.264, HEVC, MPEG-1/2/4, VP8/VP9, VC-1, and common audio codecs (AAC, MP3, AC3, Vorbis, etc.) inside containers like MP4, MKV, AVI, MOV, MPG/MPEG-TS, WMV, FLV, WebM, OGV, VOB, 3GP, and ASF. Real playback smoothness for any of these on the MiSTer's Cortex-A9 is a separate question -- see "Known limitations."

The on-screen file browser only *lists* files whose extension it recognizes (`src/browser.c`, `video_exts`): `mp4 mkv avi mov mpg mpeg m4v ts wmv flv webm ogv vob 3gp asf m2ts`. This is just a display filter, not a decode limit -- if you have a file with an unusual extension, either rename it to one of the above or add it to that list and rebuild.

## Known limitations

- No server-side transcoding: playback smoothness is bounded by the MiSTer's Cortex-A9 CPU. Expect SD-resolution, modest-bitrate files to work; 1080p H.264 or HEVC is unlikely to play smoothly.
- MiSTer's Linux audio output is a single, non-shareable device -- only one process can hold it at a time.
- No hardware page-flip / tear-free interlaced output yet (`FBIOPAN_DISPLAY` + vsync-wait only).

## Credits

See [`CREDITS.md`](CREDITS.md) for the prior art, platform research, and libraries this builds on.
