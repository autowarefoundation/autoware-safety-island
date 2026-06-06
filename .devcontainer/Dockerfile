# syntax=docker/dockerfile:1.7
# Copyright (c) 2024-2025, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
FROM ghcr.io/autowarefoundation/autoware:universe-devel-20250207 AS devcontainer
SHELL ["/bin/bash", "-c"]
ARG TARGETARCH
ARG ZSDK_VERSION=0.16.3
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZSDK_VERSION}

# Fix expired ROS GPG key issue comprehensively
# See: https://github.com/osrf/docker_images/issues/535
RUN apt-get update --allow-unauthenticated || true && \
    apt-get install -y --allow-unauthenticated curl gnupg2 && \
    apt-key del F42ED6FBAB17C654 || true && \
    curl -s https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | apt-key add - && \
    curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /tmp/ros.key && \
    cat /tmp/ros.key | gpg --dearmor --batch --yes -o /usr/share/keyrings/ros2-latest-archive-keyring.gpg && \
    rm /tmp/ros.key

# Install dependencies
RUN apt-get update && \
    apt install --no-install-recommends -y \ 
    ninja-build ccache dfu-util device-tree-compiler wget vim inetutils-ping \
    python3-dev python3-pip python3-setuptools python3-tk screen openvpn unzip \ 
    python3-wheel xz-utils file make gcc libsdl2-dev libmagic1 pixz ros-$ROS_DISTRO-domain-bridge \
    meson \
    && pip3 install avh-api avh-api-async websockets python-dateutil aiohttp six urllib3 dotenv \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install west
RUN pip3 install -U west pyelftools gdown==v4.7.3

# Install Zephyr SDK
RUN SDK_ARCH_SUFFIX="unknown" && \
    if [ "${TARGETARCH}" = "amd64" ]; then SDK_ARCH_SUFFIX="x86_64"; \
    elif [ "${TARGETARCH}" = "arm64" ]; then SDK_ARCH_SUFFIX="aarch64"; \
    else echo "Unsupported TARGETARCH: ${TARGETARCH}" && exit 1; fi && \
    wget -q --show-progress --progress=bar:force:noscroll \
    https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZSDK_VERSION}/zephyr-sdk-${ZSDK_VERSION}_linux-${SDK_ARCH_SUFFIX}.tar.xz && \
    tar --use-compress-program="pixz" -xf zephyr-sdk-${ZSDK_VERSION}_linux-${SDK_ARCH_SUFFIX}.tar.xz -C /opt \
        zephyr-sdk-${ZSDK_VERSION}/arm-zephyr-eabi/ \
        zephyr-sdk-${ZSDK_VERSION}/aarch64-zephyr-elf/ \
        zephyr-sdk-${ZSDK_VERSION}/setup.sh \
        zephyr-sdk-${ZSDK_VERSION}/cmake \
        zephyr-sdk-${ZSDK_VERSION}/sdk_toolchains \
        zephyr-sdk-${ZSDK_VERSION}/sdk_version && \
    ${ZEPHYR_SDK_INSTALL_DIR}/setup.sh -c -t arm-zephyr-eabi && \
    ${ZEPHYR_SDK_INSTALL_DIR}/setup.sh -c -t aarch64-zephyr-elf && \
    rm "zephyr-sdk-${ZSDK_VERSION}_linux-${SDK_ARCH_SUFFIX}.tar.xz"

# Install ARM Fixed Virtual Platform (FVP) for Zephyr runtime testing.
# Arm publishes this FVP package as an x86 Linux host binary, so only install it
# in amd64 devcontainer images. The arm64 image can still build Zephyr, but FVP
# runtime validation must run on an amd64 host/container.
RUN if [ "${TARGETARCH}" = "amd64" ]; then \
        FVP_URL="https://developer.arm.com/-/cdn-downloads/permalink/FVPs-Architecture/FM-11.31/FVP_Base_AEMv8R_11.31_28_Linux_x86.tar.gz" && \
        wget -q --show-progress --progress=bar:force:noscroll "${FVP_URL}" -O /tmp/fvp.tar.gz && \
        mkdir -p /opt/fvp && \
        tar -xzf /tmp/fvp.tar.gz -C /opt/fvp --strip-components=1 && \
        ln -s /opt/fvp/bin/FVP_BaseR_AEMv8R /usr/local/bin/FVP_BaseR_AEMv8R && \
        rm /tmp/fvp.tar.gz; \
    else \
        echo "Skipping FVP install for TARGETARCH=${TARGETARCH}; FVP_BaseR_AEMv8R is available as Linux x86 host binary only."; \
    fi

# Build and install picolibc for x86_64 host (needed by Zephyr native_sim runtime CI)
# The Zephyr SDK 0.16.3 does not provide x86_64 host picolibc specs.
# We build picolibc from the exact revision in Zephyr's west manifest, then
# overlay the SDK's picolibc headers which include macros (FDEV_SETUP_STREAM
# etc.) that were removed from the open-source 1.8.x releases.
RUN PICOLIBC_SHA=d07c38ff051386f8e09a143ea0a6c1d6d66dd1d8 && \
    curl -sSL "https://github.com/picolibc/picolibc/archive/${PICOLIBC_SHA}.tar.gz" | \
      tar xz -C /tmp && \
    cd /tmp/picolibc-* && \
    mkdir build && cd build && \
    meson setup . .. \
      --prefix=/opt/picolibc \
      -Dmultilib=false \
      -Dtinystdio=false && \
    ninja && ninja install && \
    cp -a /opt/zephyr-sdk-0.16.3/arm-zephyr-eabi/picolibc/include/. /opt/picolibc/include/ && \
    rm -rf /tmp/picolibc-*

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
