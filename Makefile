CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
LDFLAGS ?=

PKGS := libavformat libavcodec libavutil libswscale libswresample alsa

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
BIN := vplayer

.PHONY: all arm clean docker-image docker-arm docker-package

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(shell pkg-config --libs $(PKGS) 2>/dev/null) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags $(PKGS) 2>/dev/null) -c -o $@ $<

# Cross build target, run *inside* the docker/Dockerfile container where
# CC is already set to arm-linux-gnueabihf-gcc and PKG_CONFIG_* point at
# the armhf sysroot.
arm:
	$(MAKE) CC=$${CC:-arm-linux-gnueabihf-gcc} BIN=vplayer-arm all

docker-image:
	docker build -t vplayer-cross -f docker/Dockerfile docker

docker-arm: docker-image
	docker run --rm -v $(CURDIR):/build vplayer-cross sh -c "make -C /build clean && make -C /build arm"

# Cross-builds vplayer-arm and bundles it with its transitive ffmpeg/ALSA
# shared libraries into dist/ (MiSTer's Linux image has neither).
docker-package: docker-image
	docker run --rm -v $(CURDIR):/build vplayer-cross sh -c \
		"make -C /build clean && make -C /build arm && sh /build/docker/package.sh"

clean:
	rm -f $(OBJ) $(BIN) vplayer-arm
