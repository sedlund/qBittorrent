FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        build-essential \
        ca-certificates \
        cmake \
        libboost-dev \
        libssl-dev \
        libtorrent-rasterbar-dev \
        ninja-build \
        pkg-config \
        qt6-base-dev \
        qt6-base-private-dev \
        qt6-tools-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
        -DGUI=OFF \
        -DWEBUI=ON \
        -DPLUGINS=OFF \
        -DSTACKTRACE=OFF \
        -DTESTING=OFF \
        -DDBUS=OFF \
    && cmake --build build --parallel 2

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        ca-certificates \
        bash \
        coreutils \
        curl \
        libboost-system1.83.0 \
        libssl3t64 \
        libtorrent-rasterbar2.0 \
        qt6-base-dev \
        zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 1000 qbittorrent \
    && mkdir -p /config /downloads \
    && chown -R 1000:1000 /config /downloads

COPY --from=build /src/build/qbittorrent-nox /usr/local/bin/qbittorrent-nox

USER qbittorrent
WORKDIR /home/qbittorrent
ENV HOME=/config XDG_CONFIG_HOME=/config PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
VOLUME ["/config", "/downloads"]
EXPOSE 8080 6881 6881/udp

ENTRYPOINT ["/usr/local/bin/qbittorrent-nox"]
