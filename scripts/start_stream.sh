#!/bin/bash
# Copyright 2026 Bob Ros
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# start_stream.sh
# Optimized for ARM/Low-Power and Twitch streaming using NVIZ_* env vars.

# --- Path Detection ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# --- Configuration (Standardized NVIZ_*) ---
export NVIZ_WIDTH="${NVIZ_WIDTH:-854}"
export NVIZ_HEIGHT="${NVIZ_HEIGHT:-480}"
export NVIZ_FPS="${NVIZ_FPS:-30}"
export NVIZ_FIFO_PATH="${NVIZ_FIFO_PATH:-/tmp/nano_fifo}"

# --- ROS Namespace Handling ---
# Explicitly use remapping if ROS_NAMESPACE is present.
export ROS_NAMESPACE="${NAMESPACE:-${ROS_NAMESPACE:-}}"
NS_REMAP=""
if [[ -n "$ROS_NAMESPACE" ]]; then
    echo "[$(date)] Applying Namespace Remapping: $ROS_NAMESPACE"
    NS_REMAP="-r __ns:=$ROS_NAMESPACE"
fi

# Audio pipes (consistent with sdlviz/bob_audio)
export AUDIO_PIPE="${AUDIO_PIPE:-/tmp/audio_pipe}"
export AUDIO_MASTER_PATH="${AUDIO_MASTER_PATH:-/tmp/audio_master_pipe}"

# --- Secret Handling (Environment or File) ---
if [[ -n "$TWITCH_STREAM_KEY_FILE" ]] && [[ -f "$TWITCH_STREAM_KEY_FILE" ]]; then
    export STREAM_KEY=$(cat "$TWITCH_STREAM_KEY_FILE" | xargs)
    echo "[$(date)] Using stream key from file: $TWITCH_STREAM_KEY_FILE"
else
    export STREAM_KEY="${TWITCH_STREAM_KEY}"
fi

export INGEST_SERVER="${INGEST_SERVER:-rtmp://live-fra.twitch.tv/app/}"

# Path to the ROS setup file.
if [ -f "/ros2_ws/install/setup.bash" ]; then
    DEFAULT_ROS_SETUP="/ros2_ws/install/setup.bash"
elif [ -f "/blue/dev/streamer/ros2_ws/install/setup.bash" ]; then
    DEFAULT_ROS_SETUP="/blue/dev/streamer/ros2_ws/install/setup.bash"
else
    # Fallback/Default relative
    DEFAULT_ROS_SETUP="../../install/setup.bash"
fi
ROS_SETUP_PATH="${ROS_SETUP_PATH:-$DEFAULT_ROS_SETUP}"

# --- Sanity Checks ---
if [[ -z "$STREAM_KEY" ]]; then
    echo "Error: Neither TWITCH_STREAM_KEY nor TWITCH_STREAM_KEY_FILE is set or valid."
    exit 1
fi

if [ ! -f "$ROS_SETUP_PATH" ]; then
    echo "Error: ROS 2 setup file not found at '$ROS_SETUP_PATH'."
    exit 1
fi

# --- Sourcing and Setup ---
source "$ROS_SETUP_PATH"
echo "[$(date)] ROS 2 workspace sourced from $ROS_SETUP_PATH."

# Ensure pipes exist
if [[ ! -p $NVIZ_FIFO_PATH ]]; then
    mkfifo $NVIZ_FIFO_PATH
fi
if [[ ! -p $AUDIO_PIPE ]]; then
    mkfifo $AUDIO_PIPE
fi
if [[ ! -p $AUDIO_MASTER_PATH ]]; then
    mkfifo $AUDIO_MASTER_PATH
fi

cleanup() {
    echo "Shutting down..."
    # Kill all background jobs (mixer and nviz)
    kill $(jobs -p || echo "")
}
trap cleanup EXIT

# --- Launch Mixer (bob_audio) ---
echo "Starting audio mixer node..."
# Use internal master pipe for FFmpeg, input pipe for external feeding
# $MIXER_REMAPS allows connecting topics like /eva/tts/audio_raw to in0
ros2 run bob_audio mixer --ros-args $NS_REMAP $MIXER_REMAPS \
    -p output_fifo:=$AUDIO_MASTER_PATH \
    -p input_fifo:=$AUDIO_PIPE \
    -p enable_fifo_input:=true \
    -p heartbeat:=true &

# --- Launch Nviz Application ---
echo "Starting nviz node..."
ros2 run bob_nviz nviz "$@" --ros-args $NS_REMAP &

NODE_PID=$!

sleep 2

while true
do
    if ! kill -0 $NODE_PID 2>/dev/null; then
        echo "Error: nviz node (PID: $NODE_PID) is not running. Restarting node..."
        ros2 run bob_nviz nviz "$@" &
        NODE_PID=$!
        sleep 2
    fi

    # Standard Twitch Ingest
    INGEST_SERVER="rtmp://live-fra.twitch.tv/app/"
    
    # Cleanup STREAM_KEY (strictly sanitizing whitespace/newlines)
    STREAM_KEY=$(cat /run/secrets/twitch_key | tr -d '\r\n ')
    
    MASKED_KEY="${STREAM_KEY:0:4}****${STREAM_KEY: -4}"
    echo "[$(date)] Starting stream to Twitch (Key: $MASKED_KEY)..."
    
    # ffmpeg Command - Original Style (Simple & Clean)
    FFMPEG_LOG=$(mktemp)
    if ffmpeg -hide_banner -loglevel info -nostats \
        -f rawvideo -pixel_format bgra -video_size ${NVIZ_WIDTH}x${NVIZ_HEIGHT} \
        -framerate ${NVIZ_FPS} -i $NVIZ_FIFO_PATH \
        -f s16le -ar 44100 -ac 2 -i "$AUDIO_MASTER_PATH" \
        -c:v libx264 -pix_fmt yuv420p -preset ultrafast -tune zerolatency \
        -b:v 3000k -maxrate 3000k -bufsize 6000k \
        -c:a aac -b:a 128k -ar 44100 -ac 2 \
        -g 60 -f flv "${INGEST_SERVER}${STREAM_KEY}" 2> "$FFMPEG_LOG"; then
        echo "ffmpeg exited normally."
    else
        # Mask the key in the error log as well
        cat "$FFMPEG_LOG" | sed "s/$STREAM_KEY/**********/g"
        echo "ffmpeg exited with error. Restarting in 5 seconds..."
    fi
    rm -f "$FFMPEG_LOG"
    sleep 5
done
