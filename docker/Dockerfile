FROM debian:bullseye

RUN dpkg --add-architecture armhf && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        pkg-config \
        crossbuild-essential-armhf \
        libavcodec-dev:armhf \
        libavformat-dev:armhf \
        libavutil-dev:armhf \
        libswscale-dev:armhf \
        libswresample-dev:armhf \
        libasound2-dev:armhf \
    && rm -rf /var/lib/apt/lists/*

ENV CC=arm-linux-gnueabihf-gcc \
    PKG_CONFIG_PATH=/usr/lib/arm-linux-gnueabihf/pkgconfig \
    PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig

WORKDIR /build
CMD ["make", "-C", "/build", "arm"]
