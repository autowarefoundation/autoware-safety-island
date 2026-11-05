# cspell:ignore openbox, VNC, tigervnc, novnc, websockify, newkey, xstartup, keyout
# Keep in step with the safety-island-autoware image in ../docker-compose.yaml.
FROM ghcr.io/autowarefoundation/autoware:universe-humble-2.0.0 AS safety-island-bridge
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
ARG ROS_DISTRO
WORKDIR /autoware

# Install domain bridge
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ros-$ROS_DISTRO-domain-bridge \
    && rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
