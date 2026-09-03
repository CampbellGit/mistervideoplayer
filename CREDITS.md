# Credits

No code from any of these projects is included here (see the licensing note on MiSTerFin below) -- this is a from-scratch implementation. Credit is for the platform mechanisms, prior art, and libraries this project depends on or was informed by.

## Inspiration / prior art

- **[MiSTerFin](https://github.com/puddingstudio/MiSTerFin)** (Pudding Studio) -- a Jellyfin client for MiSTer. Studied read-only as prior art to confirm the overall architecture (Scripts-menu launch -> HPS framebuffer -> `/dev/fb0`, ALSA for audio, evdev for input) actually works on real hardware before building this from scratch. MiSTerFin is licensed CC BY-NC 4.0 (non-commercial); none of its source was copied or adapted into this project for that reason. If you're looking for a full-featured, hardware-proven MiSTer media client (including a Jellyfin-backed one with subtitles, music visualizers, etc.), it's worth a look.

## Platform

- **[MiSTer FPGA](https://misterfpga.org)** and the **[MiSTer-devel](https://github.com/MiSTer-devel)** project -- `Main_MiSTer`, `Template_MiSTer`, and `Menu_MiSTer` were read directly (source, not binaries) to confirm the HPS-framebuffer (`UIO_SET_FBUF`, `MENU_SCRIPTS_FB`, `fb_terminal`) and framework audio (`sys/alsa.sv`, `audio_out.sv`) mechanisms this project relies on. See `docs/ARCHITECTURE.md` for specifics.

## Libraries

- **[FFmpeg](https://ffmpeg.org)** (`libavformat`, `libavcodec`, `libswscale`, `libswresample`) -- all container/codec decoding and scaling/resampling.
- **[ALSA](https://www.alsa-project.org)** -- audio output.
