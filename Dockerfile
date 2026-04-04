# Minimal Dockerfile for bob_nviz + bob_audio
ARG ROS_DISTRO=humble
FROM ros:${ROS_DISTRO}-ros-base

# Platform control (filled by Docker Buildx automatically)
ARG TARGETARCH

# Use bash for sourcing
SHELL ["/bin/bash", "-c"]

# Environmental setup
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    nlohmann-json3-dev \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

# Workspace setup
WORKDIR /ros2_ws/src

# 1. Install bob_audio# Clone dependencies (force refresh with a build arg if needed)
ARG BOB_AUDIO_REVISION=unknown
RUN git clone --depth 1 https://github.com/bob-ros2/bob_audio.git

# 2. Copy current bob_nviz source
COPY . bob_nviz/

WORKDIR /ros2_ws

# Build workspace
RUN source /opt/ros/${ROS_DISTRO}/setup.bash && \
    colcon build --packages-select bob_audio bob_nviz \
    --cmake-args -DCMAKE_BUILD_TYPE=Release

# Platform feedback
RUN echo "Building for $(uname -m) platform..."

# Entrypoint handles workspace sourcing
ENTRYPOINT ["/bin/bash", "-c", "source /ros2_ws/install/setup.bash && \"$@\"", "--"]

# Default command
CMD ["ros2 run bob_nviz nviz"]
