# ROS Package [bob_nviz](https://github.com/bob-ros2/bob_nviz)

[![CI – Build & Test](https://github.com/bob-ros2/bob_nviz/actions/workflows/ros2_ci.yaml/badge.svg)](https://github.com/bob-ros2/bob_nviz/actions/workflows/ros2_ci.yaml)

**Ultra-lightweight, resource-efficient Software Renderer for ROS 2.**

`bob_nviz` provides minimalist visual overlays for low-power devices (like ARM boards/Synology NAS) without requiring a GPU or heavy dependencies like SDL2 or FreeType. It renders directly to a raw BGRA buffer and outputs it to a FIFO pipe.

## Key Features
- **CPU-Only Rendering**: Zero GPU usage, ideal for headless servers or containers.
- **Minimal Dependencies**: Requires only `rclcpp` and `nlohmann_json`.
- **Amiga-Style Aesthetic**: 8x8 bitmap font with full UTF-8 to Latin-1 decoding.
- **Dynamic Layers**: Manage multiple text terminals and bitmap canvases via JSON events.
- **Auto-Expiration**: Elements can automatically disappear after a set time.
- **Latched State**: The current layout is always available via `events_changed`.


## Installation & Building

```bash
cd ~/ros2_ws/src
git clone https://github.com/bob-ros2/bob_nviz.git
cd ..
colcon build --packages-select bob_nviz
source install/setup.bash
```


## Usage

Run the node (default output: `/tmp/nano_fifo`):
```bash
ros2 run bob_nviz nviz
```

### Display the stream
Use `ffplay` to view the raw BGRA stream:
```bash
ffplay -f rawvideo -pixel_format bgra -video_size 854x480 -i /tmp/nano_fifo
```


## ROS 2 API

### Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| `width` | `854` | Rendering width in pixels. (Env: `NVIZ_WIDTH`) |
| `height` | `480` | Rendering height in pixels. (Env: `NVIZ_HEIGHT`) |
| `fps` | `30.0` | Target frames per second. (Env: `NVIZ_FPS`) |
| `fifo_path`| `/tmp/nano_fifo`| Path to output raw BGRA pipe. (Env: `NVIZ_FIFO_PATH`) |
| `queue_size`| `1000` | ROS subscriber queue size. (Env: `NVIZ_QUEUE_SIZE`) |

### Topics
| Topic | Type | Mode | Description |
|-------|------|------|-------------|
| `events` | `std_msgs/String` | Sub | JSON commands to manage layers. |
| `events_changed` | `std_msgs/String` | Pub | **Latched** state of all active layers. |
| **Dynamic** | `std_msgs/String` | Sub | Topics for terminal content or bitmap data. |


## Dynamic Configuration

The node is controlled by sending JSON arrays to the `events` topic.

### Common Fields
Every layer supports:
- `id` (string, optional): Unique identifier.
- `action` (string, optional): `add` (default) or `remove`.
- `expire` (float, optional): Auto-remove after $N$ seconds. `0` for infinite.
- `area` (array): `[x, y, width, height]`. Mandatory for creation.

### 1. `String` (Terminal)
Renders a rolling text terminal.
- `topic` (string): ROS topic for incoming strings.
- `text` (string, optional): Static text to display immediately.
- `title` (string, optional): Title bar text.
- `mode` (string): 
  - `default`: Append incoming tokens (LLM style).
  - `clear_on_new`: Clear terminal before showing new message.
  - `append_newline`: Automatically add `\n` to every message.
- `font_size` (int): Scaling factor (e.g., 16, 24, 32).
- `align` (string): `left` (default), `center`, `right`.
- `text_color` / `bg_color`: `[R, G, B, A]`.

### 2. `Bitmap` (Canvas)
Displays raw image data (1-bit or 8-bit).
- `topic` (string): Binary topic (`std_msgs/UInt8MultiArray`).
- `topic + "/hex"`: Hex-string topic (`std_msgs/String`).
- `depth` (int): `1` (1-bit mask) or `8` (8-bit grayscale).
- `color`: `[R, G, B, A]` for the foreground.


## Examples (`ros2 topic pub`)

### Add a scrolling LLM Terminal
```bash
ros2 topic pub --once /eva/events std_msgs/msg/String 'data: "[{\"action\":\"add\", \"type\":\"String\", \"id\":\"main\", \"title\":\"LLM Stream\", \"topic\":\"/eva/llm\", \"area\":[50, 50, 400, 300], \"mode\": \"default\"}]"'
```

### Show an Auto-Expiring Alert
```bash
ros2 topic pub --once /eva/events std_msgs/msg/String 'data: "[{\"type\":\"String\", \"id\":\"alert\", \"text\":\"System Warning!\", \"expire\": 5.0, \"area\":[227, 200, 400, 50], \"align\":\"center\", \"text_color\":[255, 0, 0, 255]}]"'
```

### Add a Bitmap Icon (Heart) via Hex
```bash
# Register bitmap
ros2 topic pub --once /eva/events std_msgs/msg/String 'data: "[{\"type\":\"Bitmap\", \"id\":\"heart\", \"area\":[20, 20, 16, 16], \"topic\":\"/ui/heart\", \"depth\": 1, \"color\":[255, 50, 50, 255]}]"'

# Send data
ros2 topic pub --once /ui/heart/hex std_msgs/msg/String "data: '0C301E783FfC7FfE7FfE7FfE3FfC1Ff80fF007E003C0018'"
```

### Remove a Layer
```bash
ros2 topic pub --once /eva/events std_msgs/msg/String 'data: "[{\"action\":\"remove\", \"id\":\"alert\"}]"'
```

---

## Audio System

`bob_nviz` is designed to work in conjunction with the [**`bob_audio`**](https://github.com/bob-ros2/bob_audio) package. To ensure a stable stream without disconnects, a constant audio "heartbeat" is required.

### 1. The Mixer Pipeline
The `bob_audio` mixer node combines multiple audio sources (TTS, System Sounds, Music) into a single master pipe (`/tmp/audio_master_pipe`). This pipe is then consumed by FFmpeg.

*   **Silence Heartbeat**: Ensures FFmpeg always receives audio frames, even if no ROS nodes are currently speaking.
*   **Dynamic Mixing**: Use `ros2 topic pub` to send audio data to the mixer's input topics.

### 2. Feeding External Audio
To inject audio files into the running stream (e.g., background music), use the provided helper script:

```bash
./scripts/feed_audio.sh my_song.mp3
```

---

## Twitch Streaming

`bob_nviz` excels at streaming from headless, low-power ARM devices (RPi, Synology, etc.) by using efficient raw-video pipes and FFmpeg.

### 1. Configuration
Streaming is configured via environment variables. Ensure `TWITCH_STREAM_KEY` is set in your environment.

| Variable | Default | Description |
|----------|---------|-------------|
| `NVIZ_WIDTH` | `854` | Video width (e.g., 480p, 720p). |
| `NVIZ_HEIGHT` | `480` | Video height. |
| `NVIZ_FPS` | `30` | Target framerate. |
| `NVIZ_FIFO_PATH` | `/tmp/nano_fifo` | Raw video pipe location. |
| `AUDIO_MASTER_PATH` | `/tmp/audio_master_pipe` | Mixed audio pipe location. |

### 2. Starting the Stream
The `start_stream.sh` script orchestrates the **nviz node**, the **audio mixer**, and **FFmpeg** in one go.

```bash
export TWITCH_STREAM_KEY="your_key_here"
./scripts/start_stream.sh
```

The script will:
1. Create the necessary Unix pipes.
2. Start the `bob_audio` mixer with a silence heartbeat.
3. Start the `nviz` visualization node.
4. Run an FFmpeg loop that encodes and pushes the combined stream to Twitch.

### 3. Cleanup
To stop everything, simply use `Ctrl+C`. The script includes a trap to cleanly shut down all background ROS processes.
