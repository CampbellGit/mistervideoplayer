#!/bin/bash
# Install to /media/fat/Scripts/VideoPlayer.sh -- launching it from the
# MiSTer Scripts menu gets us the HPS framebuffer (Main_MiSTer's
# MENU_SCRIPTS_FB path, gated on `fb_terminal=1` in MiSTer.ini) with no
# patch to Main_MiSTer needed.
exec /media/fat/video-player/vplayer /media/fat/video
