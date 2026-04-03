# ROS Package [bob_nviz](https://github.com/bob-ros2/bob_nviz)

[![CI – Build & Test](https://github.com/bob-ros2/bob_nviz/actions/workflows/ros2_ci.yaml/badge.svg)](https://github.com/bob-ros2/bob_nviz/actions/workflows/ros2_ci.yaml)

**Ultra-lightweight, resource-efficient Software Renderer for ROS 2.**  
`bob_nviz` provides minimalist visual overlays for low-power devices (like ARM boards) without requiring a GPU or heavy dependencies like SDL2 or FreeType. It renders directly to a raw BGRA buffer and outputs it to a FIFO pipe.

---

## Key Features
- **CPU-Only Rendering**: Zero GPU usage, ideal for headless or low-power ARM servers.
- **Minimal Dependencies**: Only `rclcpp` and `nlohmann_json`.
- **Amiga-Style Aesthetic**: 8x8 bitmap font for that retro terminal feel.
- **Smart Text Handling**: Supports text token streams (LLM-style), autoscrolling, and manual wrapping.
- **Canvas/Bitmap Support**: Displays 1-bit Masks or 8-bit Grayscale images via raw topics or Hex-strings.
- **High Performance**: Optimized non-blocking FIFO streaming.

---

## Installation
```bash
cd ~/ros2_ws/src
git clone git@github.com:bob-ros2/bob_nviz.git
cd ..
colcon build --packages-select bob_nviz
```

---

## Usage
Run the node (it will create `/tmp/nano_fifo` by default):
```bash
ros2 run bob_nviz nviz
```

### Configuration (Parameters & Env Vars)
You can configure the node via ROS parameters or environment variables. **Environment variables take precedence.**

| Parameter | Default | Description |
|-----------|---------|-------------|
| `width` | `854` | Rendering width (px). Environment variable: `NVIZ_WIDTH` |
| `height` | `480` | Rendering height (px). Environment variable: `NVIZ_HEIGHT` |
| `fps` | `30.0` | Frames per second. Environment variable: `NVIZ_FPS` |
| `fifo_path` | `/tmp/nano_fifo` | Path to output raw BGRA pipe. Environment variable: `NVIZ_FIFO_PATH` |

### Display the stream
Use `ffplay` to view the raw BGRA stream:
```bash
ffplay -f rawvideo -pixel_format bgra -video_size 854x480 -i /tmp/nano_fifo
```

### Adding a Terminal Element (JSON Event)
Publish to `/nviz/events` (or your configured event topic):
```bash
ros2 topic pub --once /eva/events std_msgs/msg/String "{data: '[{\"action\":\"add\", \"type\":\"String\", \"id\":\"main\", \"title\":\"Bob-nviz\", \"topic\":\"/eva/llm\", \"area\":[50, 50, 400, 300], \"font_size\": 16}]'}"
```

### Adding a Bitmap Element (1-bit / 8-bit)
You can feed bitmaps via binary topics or hex-strings:
```bash
# Register a 16x16 red bitmap
ros2 topic pub --once /eva/events std_msgs/msg/String "{data: '[{\"action\":\"add\", \"type\":\"Bitmap\", \"id\":\"ico\", \"area\":[20, 20, 16, 16], \"topic\":\"/eva/heart\", \"depth\": 1, \"color\":[255, 0, 0, 255]}]'}"

# Send data as Hex (Heart shape)
ros2 topic pub --once /eva/heart/hex std_msgs/msg/String "{data: '18183C3C7E7EFFFFFFFFFFFF7FFE7FFE3FFC3FFC1FF81FF80FF007E003C00180'}"
```

---

## License
Apache-2.0
