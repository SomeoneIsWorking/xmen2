# The Linux AppImage build host: Ubuntu 22.04, the oldest LTS the AppImage
# catalog requires, so the release's glibc floor is 2.35.
# tools/appimage_container.py builds and runs this image.
FROM docker.io/library/ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        binutils ca-certificates curl file git gnupg make pkg-config xz-utils \
        libc6-dev libstdc++-11-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
        libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
        libdecor-0-dev libegl-dev libgl-dev libglu1-mesa-dev libopengl-dev libvulkan-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev \
        libdrm-dev libgbm-dev libibus-1.0-dev \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-20 main" \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update && apt-get install -y --no-install-recommends clang-20 lld-20 \
    && rm -rf /var/lib/apt/lists/*
ARG UV_VERSION=0.10.12
ARG UV_SHA256=adccf40b5d1939a5e0093081ec2307ea24235adf7c2d96b122c561fa37711c46
RUN curl -fsSL -o /tmp/uv.tar.gz \
        "https://github.com/astral-sh/uv/releases/download/${UV_VERSION}/uv-x86_64-unknown-linux-musl.tar.gz" \
    && echo "${UV_SHA256}  /tmp/uv.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/uv.tar.gz -C /usr/local/bin --strip-components=1 \
    && rm /tmp/uv.tar.gz
ENV CC=clang-20 CXX=clang++-20 UV_FROZEN=1 PYTHONDONTWRITEBYTECODE=1
