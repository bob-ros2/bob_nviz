#!/bin/bash
# feed_audio.sh
# Streams local audio file to the nviz audio mixer pipe.

AUDIO_PIPE="${AUDIO_PIPE:-/tmp/audio_pipe}"

if [ -z "$1" ]; then
    echo "Usage: $0 <audio_file>"
    exit 1
fi

if [[ ! -p $AUDIO_PIPE ]]; then
    echo "Error: Audio pipe $AUDIO_PIPE does not exist. Start the stream script first."
    exit 1
fi

echo "Feeding $1 to $AUDIO_PIPE..."

# Convert to S16LE 44100Hz Stereo (expected by mixer)
# Added -re to stream in real-time speed.
ffmpeg -re -i "$1" -f s16le -ar 44100 -ac 2 -y "$AUDIO_PIPE"
