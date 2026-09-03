#!/bin/sh
# Runs inside the vplayer-cross container. Computes the transitive closure
# of shared libraries vplayer-arm needs (beyond what MiSTer's own Linux
# image already ships, i.e. plain glibc/libm/libpthread/etc.) and bundles
# them into dist/, since MiSTer's root filesystem has no ffmpeg or ALSA
# userspace libraries of its own.
set -e

BUILD_DIR=/build
DIST_DIR="$BUILD_DIR/dist"
LIB_DIRS="/usr/lib/arm-linux-gnueabihf /lib/arm-linux-gnueabihf"

# Libraries MiSTer's Linux image already provides -- never bundle these.
SKIP_PATTERN='^(libc|libm|libpthread|libdl|librt|ld-linux|libresolv|libutil|libnsl|libgcc_s)\.so'

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/lib"
cp "$BUILD_DIR/vplayer-arm" "$DIST_DIR/vplayer-arm"

find_lib() {
	name="$1"
	for d in $LIB_DIRS; do
		if [ -e "$d/$name" ]; then
			echo "$d/$name"
			return 0
		fi
	done
	return 1
}

needed_of() {
	arm-linux-gnueabihf-objdump -p "$1" 2>/dev/null | awk '/NEEDED/ {print $2}'
}

seen_file=$(mktemp)
queue_file=$(mktemp)
needed_of "$BUILD_DIR/vplayer-arm" > "$queue_file"

while [ -s "$queue_file" ]; do
	name=$(head -n1 "$queue_file")
	sed -i '1d' "$queue_file"

	grep -qx "$name" "$seen_file" 2>/dev/null && continue
	echo "$name" >> "$seen_file"

	echo "$name" | grep -qE "$SKIP_PATTERN" && continue

	path=$(find_lib "$name") || { echo "warning: could not locate $name" >&2; continue; }

	cp -L "$path" "$DIST_DIR/lib/$name"
	needed_of "$path" >> "$queue_file"
done

rm -f "$seen_file" "$queue_file"

cat > "$DIST_DIR/vplayer" <<'EOF'
#!/bin/sh
# Wrapper so vplayer-arm finds its bundled libs without needing them
# installed system-wide on MiSTer's Linux image.
DIR="$(cd "$(dirname "$0")" && pwd)"
exec env LD_LIBRARY_PATH="$DIR/lib" "$DIR/vplayer-arm" "$@"
EOF
chmod +x "$DIST_DIR/vplayer"

echo "packaged: $DIST_DIR"
ls -la "$DIST_DIR" "$DIST_DIR/lib"
