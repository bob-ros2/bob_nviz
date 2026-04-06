// Copyright 2026 Bob Ros
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include "nlohmann/json.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

#include "bob_nviz/font8x8.h"

using namespace std::chrono_literals;
using json = nlohmann::json;

struct Color
{
  uint8_t r, g, b, a;
};

struct Rect
{
  int x, y, w, h;
};

enum Alignment
{
  ALIGN_LEFT,
  ALIGN_CENTER,
  ALIGN_RIGHT
};

enum TerminalMode
{
  MODE_DEFAULT,
  MODE_CLEAR_ON_NEW,
  MODE_APPEND_NEWLINE
};

class NanoTerminal
{
public:
  NanoTerminal(
    Rect area, Color text_color, Color bg_color, int scale, size_t line_limit,
    Alignment align, std::string title_in = "", int columns = 0,
    TerminalMode mode = MODE_DEFAULT)
  : area_(area), text_color_(text_color), bg_color_(bg_color), scale_(scale), align_(align),
    columns_override_(columns), line_limit_(line_limit), mode_(mode), utf8_lead_(0)
  {
    lines_.push_back("");
    update_wrap_width();
    if (!title_in.empty()) {
      set_title(title_in);
    }
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
    lines_.push_back("");
    current_word_.clear();
  }

  void commit_word()
  {
    if (current_word_.empty()) {
      return;
    }
    if (lines_.back().length() + current_word_.length() > wrap_width_) {
      lines_.push_back("");
    }
    lines_.back() += current_word_;
    current_word_.clear();
  }

  void append(const std::string & text)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == MODE_CLEAR_ON_NEW && !text.empty()) {
      lines_.clear();
      lines_.push_back("");
    }
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
        process_char('\n');
        i++;
      } else {
        unsigned char c = text[i];
        if (utf8_lead_ == 0) {
          if (c == 0xC2 || c == 0xC3) {
            utf8_lead_ = c;
            continue;
          }
          process_char(c);
        } else {
          uint8_t decoded = (utf8_lead_ == 0xC2) ? c : (c + 0x40);
          process_char(decoded);
          utf8_lead_ = 0;
        }
      }
    }
    if (mode_ == MODE_APPEND_NEWLINE && !text.empty()) {
      process_char('\n');
    }
    while (lines_.size() > line_limit_) {
      lines_.pop_front();
    }
  }

  void set_mode(TerminalMode mode)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = mode;
  }
  void set_title(const std::string & title)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    title_ = "";
    uint8_t lead = 0;
    for (unsigned char c : title) {
      if (lead == 0) {
        if (c == 0xC2 || c == 0xC3) {
          lead = c;
          continue;
        }
        if (c >= 32) {
          title_ += static_cast<char>(c);
        }
      } else {
        uint8_t d = (lead == 0xC2) ? c : (c + 0x40);
        if (d >= 32) {
          title_ += static_cast<char>(d);
        }
        lead = 0;
      }
    }
  }

  void draw(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Draw Background
    for (int y = area_.y; y < area_.y + area_.h; ++y) {
      if (y < 0 || y >= buffer_h) {
        continue;
      }
      for (int x = area_.x; x < area_.x + area_.w; ++x) {
        if (x < 0 || x >= buffer_w) {
          continue;
        }
        int idx = (y * buffer_w + x) * 4;
        if (bg_color_.a == 255) {
          buffer[idx] = bg_color_.b;
          buffer[idx + 1] = bg_color_.g;
          buffer[idx + 2] = bg_color_.r;
          buffer[idx + 3] = 255;
        } else if (bg_color_.a > 0) {
          uint32_t a = bg_color_.a;
          uint32_t na = 255 - a;
          buffer[idx] =
            static_cast<uint8_t>((static_cast<uint32_t>(buffer[idx]) * na +
            static_cast<uint32_t>(bg_color_.b) * a) >> 8);
          buffer[idx +
            1] =
            static_cast<uint8_t>((static_cast<uint32_t>(buffer[idx + 1]) * na +
            static_cast<uint32_t>(bg_color_.g) * a) >> 8);
          buffer[idx +
            2] =
            static_cast<uint8_t>((static_cast<uint32_t>(buffer[idx + 2]) * na +
            static_cast<uint32_t>(bg_color_.r) * a) >> 8);
          buffer[idx + 3] = 255;
        }
      }
    }

    int cur_y = area_.y;
    // Draw Title Bar
    if (!title_.empty()) {
      int th = 8 * scale_ + 6;
      for (int y = area_.y; y < area_.y + th; ++y) {
        if (y < 0 || y >= buffer_h) {
          continue;
        }
        for (int x = area_.x; x < area_.x + area_.w; ++x) {
          if (x < 0 || x >= buffer_w) {
            continue;
          }
          int idx = (y * buffer_w + x) * 4;
          buffer[idx] = static_cast<uint8_t>(buffer[idx] * 0.5f);
          buffer[idx + 1] = static_cast<uint8_t>(buffer[idx + 1] * 0.5f);
          buffer[idx + 2] = static_cast<uint8_t>(buffer[idx + 2] * 0.5f);
        }
      }
      int dist = static_cast<int>(title_.length()) * 8 * scale_;
      int tx = area_.x + (area_.w - dist) / 2;
      for (unsigned char c : title_) {
        draw_char(buffer, buffer_w, buffer_h, c, tx, area_.y + 3, {255, 255, 255, 255});
        tx += 8 * scale_;
      }
      cur_y += th + 4;
    } else {
      cur_y += 4;
    }

    // Draw Main Text
    int ch = 8 * scale_, cw = 8 * scale_, ls = 2;
    int fit = (area_.y + area_.h - cur_y - 4) / (ch + ls);
    if (fit < 1) {
      fit = 1;
    }

    // Build a temporary set of lines including the current word for smooth streaming
    std::deque<std::string> draw_lines = lines_;
    if (!current_word_.empty()) {
      if (draw_lines.back().length() + current_word_.length() > wrap_width_) {
        draw_lines.push_back(current_word_);
      } else {
        draw_lines.back() += current_word_;
      }
    }

    size_t start = (draw_lines.size() > static_cast<size_t>(fit)) ?
      (draw_lines.size() - static_cast<size_t>(fit)) : 0;

    for (size_t i = start; i < draw_lines.size(); ++i) {
      int cur_x = area_.x + 4;
      if (align_ == ALIGN_RIGHT) {
        cur_x = area_.x + area_.w - static_cast<int>(draw_lines[i].length()) * cw - 4;
      } else if (align_ == ALIGN_CENTER) {
        cur_x = area_.x + (area_.w - static_cast<int>(draw_lines[i].length()) * cw) / 2;
      }
      for (unsigned char c : draw_lines[i]) {
        draw_char(buffer, buffer_w, buffer_h, c, cur_x, cur_y, text_color_);
        cur_x += cw;
      }
      cur_y += ch + ls;
    }
  }

  void set_area(Rect area)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    area_ = area;
    update_wrap_width();
  }
  void set_colors(Color text, Color bg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    text_color_ = text;
    bg_color_ = bg;
  }
  void set_scale(int scale)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scale_ = scale;
    update_wrap_width();
  }
  void set_align(Alignment align)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    align_ = align;
  }
  void set_columns(int cols)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    columns_override_ = cols;
    update_wrap_width();
  }
  TerminalMode get_mode() const {return mode_;}

private:
  void update_wrap_width()
  {
    if (columns_override_ > 0) {
      wrap_width_ = static_cast<size_t>(columns_override_);
    } else {
      wrap_width_ = static_cast<size_t>((area_.w - 10) / (8 * scale_));
    }
    if (wrap_width_ < 1) {
      wrap_width_ = 1;
    }
  }
  void process_char(uint8_t c)
  {
    if (c == '\n') {
      commit_word();
      lines_.push_back("");
    } else if (c == ' ' || c == '\t') {
      commit_word();
      if (!lines_.back().empty() && lines_.back().length() < wrap_width_) {
        lines_.back() += ' ';
      }
    } else if (c >= 32) {
      current_word_ += static_cast<char>(c);
      if (current_word_.length() >= wrap_width_) {
        commit_word();
      }
    }
  }
  void draw_char(std::vector<uint8_t> & b, int bw, int bh, uint8_t c, int sx, int sy, Color col)
  {
    const uint8_t * bm = font8x8_data[c];
    for (int r = 0; r < 8; ++r) {
      for (int cl = 0; cl < 8; ++cl) {
        if (bm[r] & (1 << (7 - cl))) {
          for (int py = 0; py < scale_; ++py) {
            for (int px = 0; px < scale_; ++px) {
              int tx = sx + cl * scale_ + px;
              int ty = sy + r * scale_ + py;
              if (tx >= 0 && tx < bw && ty >= 0 && ty < bh) {
                int idx = (ty * bw + tx) * 4;
                if (col.a == 255) {
                  b[idx] = col.b;
                  b[idx + 1] = col.g;
                  b[idx + 2] = col.r;
                  b[idx + 3] = 255;
                } else if (col.a > 0) {
                  uint32_t a = col.a;
                  uint32_t na = 255 - a;
                  b[idx] =
                    static_cast<uint8_t>((static_cast<uint32_t>(b[idx]) * na +
                    static_cast<uint32_t>(col.b) * a) >> 8);
                  b[idx +
                    1] =
                    static_cast<uint8_t>((static_cast<uint32_t>(b[idx + 1]) * na +
                    static_cast<uint32_t>(col.g) * a) >> 8);
                  b[idx +
                    2] =
                    static_cast<uint8_t>((static_cast<uint32_t>(b[idx + 2]) * na +
                    static_cast<uint32_t>(col.r) * a) >> 8);
                  b[idx + 3] = 255;
                }
              }
            }
          }
        }
      }
    }
  }
  Rect area_;
  Color text_color_, bg_color_;
  int scale_;
  Alignment align_;
  int columns_override_;
  size_t line_limit_, wrap_width_;
  std::string title_, current_word_;
  std::deque<std::string> lines_;
  std::mutex mutex_;
  TerminalMode mode_;
  uint8_t utf8_lead_;
};

class NanoCanvas
{
public:
  NanoCanvas(Rect area, Color fg, int depth)
  : area_(area), fg_(fg), depth_(depth)
  {
    data_.resize((depth == 8) ? (area.w * area.h) : ((area.w * area.h + 7) / 8), 0);
  }
  void update_data(const std::vector<uint8_t> & d)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t sz = (depth_ == 8) ? (area_.w * area_.h) : ((area_.w * area_.h + 7) / 8);
    if (d.size() >= sz) {
      std::copy(d.begin(), d.begin() + sz, data_.begin());
    }
  }
  void draw(std::vector<uint8_t> & b, int bw, int bh)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data_.empty()) {
      return;
    }
    for (int y = 0; y < area_.h; ++y) {
      for (int x = 0; x < area_.w; ++x) {
        int tx = area_.x + x;
        int ty = area_.y + y;
        if (tx >= 0 && tx < bw && ty >= 0 && ty < bh) {
          bool set = false;
          uint8_t val = 255;
          if (depth_ == 8) {
            val = data_[y * area_.w + x];
            set = (val > 0);
          } else {
            int bi = y * area_.w + x;
            set = (data_[bi / 8] & (1 << (7 - (bi % 8))));
          }

          if (set) {
            int idx = (ty * bw + tx) * 4;
            uint32_t combined_alpha = (static_cast<uint32_t>(fg_.a) * val) / 255;
            if (combined_alpha >= 255) {
              b[idx] = fg_.b;
              b[idx + 1] = fg_.g;
              b[idx + 2] = fg_.r;
              b[idx + 3] = 255;
            } else if (combined_alpha > 0) {
              uint32_t a = combined_alpha;
              uint32_t na = 255 - a;
              b[idx] =
                static_cast<uint8_t>((static_cast<uint32_t>(b[idx]) * na +
                static_cast<uint32_t>(fg_.b) * a) >> 8);
              b[idx +
                1] =
                static_cast<uint8_t>((static_cast<uint32_t>(b[idx + 1]) * na +
                static_cast<uint32_t>(fg_.g) * a) >> 8);
              b[idx +
                2] =
                static_cast<uint8_t>((static_cast<uint32_t>(b[idx + 2]) * na +
                static_cast<uint32_t>(fg_.r) * a) >> 8);
              b[idx + 3] = 255;
            }
          }
        }
      }
    }
  }
  void set_area(Rect a)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    area_ = a;
  }
  void set_color(Color fg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    fg_ = fg;
  }

private:
  Rect area_; Color fg_; int depth_; std::vector<uint8_t> data_; std::mutex mutex_;
};

class NanoVideo
{
public:
  NanoVideo(
    const std::string & pipe_path, Rect area, int src_w, int src_h,
    const std::string & encoding = "rgb")
  : pipe_path_(pipe_path), area_(area), src_w_(src_w), src_h_(src_h), encoding_(encoding),
    running_(true), frame_ready_(false)
  {
    frame_buffer_.resize(src_w_ * src_h_ * 3, 0);  // RGB24 or BGR24
    worker_ = std::thread(&NanoVideo::pipe_reader, this);
  }

  ~NanoVideo()
  {
    running_ = false;
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  void draw(std::vector<uint8_t> & buffer, int width, int height);

  void set_area(Rect area) {area_ = area;}

private:
  void pipe_reader()
  {
    int fd = -1;
    while (running_) {
      if (fd == -1) {
        fd = open(pipe_path_.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd == -1) {
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          continue;
        }
      }

      size_t frame_size = src_w_ * src_h_ * 3;
      std::vector<uint8_t> tmp(frame_size);
      size_t read_bytes = 0;
      bool ok = true;
      while (read_bytes < tmp.size() && running_) {
        ssize_t n = read(fd, tmp.data() + read_bytes, tmp.size() - read_bytes);
        if (n > 0) {
          read_bytes += n;
        } else if (n == 0) {
          close(fd); fd = -1; ok = false; break;
        } else {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
          }
          close(fd); fd = -1; ok = false; break;
        }
      }

      if (ok && read_bytes == tmp.size()) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_buffer_ = std::move(tmp);
        frame_ready_ = true;
      }
    }
    if (fd != -1) {
      close(fd);
    }
  }

  std::string pipe_path_;
  Rect area_;
  int src_w_, src_h_;
  std::string encoding_;
  std::vector<uint8_t> frame_buffer_;
  std::mutex mutex_;
  std::thread worker_;
  std::atomic<bool> running_;
  std::atomic<bool> frame_ready_;
};

void NanoVideo::draw(std::vector<uint8_t> & buffer, int width, int height)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!frame_ready_ || frame_buffer_.empty()) {
    return;
  }

  bool swap = (encoding_ == "bgr");

  for (int y = 0; y < src_h_; ++y) {
    int dy = area_.y + y;
    if (dy < 0 || dy >= height) {
      continue;
    }

    int start_x_in_src = 0;
    int end_x_in_src = src_w_;

    // Handle horizontal clipping
    int render_x = area_.x;
    int current_row_w = src_w_;

    if (render_x < 0) {
      start_x_in_src = -render_x;
      current_row_w += render_x;
      render_x = 0;
    }

    if (render_x + current_row_w > width) {
      current_row_w = width - render_x;
    }

    if (current_row_w <= 0) {
      continue;
    }

    size_t src_row_off = static_cast<size_t>(y) * src_w_ * 3;
    size_t dst_row_off = (static_cast<size_t>(dy) * width + render_x) * 4;

    for (int x = 0; x < current_row_w; ++x) {
      size_t s = src_row_off + static_cast<size_t>(start_x_in_src + x) * 3;
      size_t d = dst_row_off + static_cast<size_t>(x) * 4;

      if (swap) {
        buffer[d] = frame_buffer_[s + 2];      // BGR Input: S+2 is R
        buffer[d + 1] = frame_buffer_[s + 1];  // G
        buffer[d + 2] = frame_buffer_[s];      // B
        buffer[d + 3] = 255;                   // A
      } else {
        buffer[d] = frame_buffer_[s];          // RGB Input: S is R
        buffer[d + 1] = frame_buffer_[s + 1];  // G
        buffer[d + 2] = frame_buffer_[s + 2];  // B
        buffer[d + 3] = 255;                   // A
      }
    }
  }
}

class NanoVizNode : public rclcpp::Node
{
public:
  NanoVizNode()
  : Node("nviz")
  {
    signal(SIGPIPE, SIG_IGN);
    auto wd = rcl_interfaces::msg::ParameterDescriptor{};
    wd.description = "Width. Env: NVIZ_WIDTH";
    this->declare_parameter("width", 854, wd);
    auto hd = rcl_interfaces::msg::ParameterDescriptor{};
    hd.description = "Height. Env: NVIZ_HEIGHT";
    this->declare_parameter("height", 480, hd);
    auto fd = rcl_interfaces::msg::ParameterDescriptor{};
    fd.description = "FPS. Env: NVIZ_FPS";
    this->declare_parameter("fps", 30.0, fd);
    auto pd = rcl_interfaces::msg::ParameterDescriptor{};
    pd.description = "FIFO. Env: NVIZ_FIFO_PATH";
    this->declare_parameter("fifo_path", "/tmp/nano_fifo", pd);
    auto qd = rcl_interfaces::msg::ParameterDescriptor{};
    qd.description = "ROS Queue Size. Env: NVIZ_QUEUE_SIZE";
    this->declare_parameter("queue_size", 1000, qd);

    width_ = get_env_or_param("NVIZ_WIDTH", "width");
    height_ = get_env_or_param("NVIZ_HEIGHT", "height");
    fps_ = get_env_or_param_double("NVIZ_FPS", "fps");
    fifo_path_ = get_env_or_param_str("NVIZ_FIFO_PATH", "fifo_path");
    queue_size_ = get_env_or_param("NVIZ_QUEUE_SIZE", "queue_size");

    buffer_.resize(width_ * height_ * 4, 0);
    cb_group_reentrant_ = this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

    auto sub_options = rclcpp::SubscriptionOptions();
    sub_options.callback_group = cb_group_reentrant_;

    events_changed_pub_ = this->create_publisher<std_msgs::msg::String>(
      "events_changed", rclcpp::QoS(1).transient_local());

    event_sub_ = this->create_subscription<std_msgs::msg::String>(
      "events", 10, std::bind(&NanoVizNode::event_callback, this, std::placeholders::_1),
      sub_options);

    running_ = true;
    render_thread_ = std::thread(&NanoVizNode::render_loop, this);

    RCLCPP_INFO(this->get_logger(), "Nano-Viz: %dx%d @ %.1f fps", width_, height_, fps_);
    publish_state();
  }

  ~NanoVizNode()
  {
    running_ = false;
    if (render_thread_.joinable()) {
      render_thread_.join();
    }
    if (fifo_fd_ != -1) {
      close(fifo_fd_);
    }
  }

private:
  int get_env_or_param(const char * e, const std::string & p)
  {
    const char * v = std::getenv(e);
    return v ? std::atoi(v) : this->get_parameter(p).as_int();
  }
  double get_env_or_param_double(const char * e, const std::string & p)
  {
    const char * v = std::getenv(e);
    return v ? std::atof(v) : this->get_parameter(p).as_double();
  }
  std::string get_env_or_param_str(const char * e, const std::string & p)
  {
    const char * v = std::getenv(e);
    return v ? std::string(v) : this->get_parameter(p).as_string();
  }

  void publish_state()
  {
    nlohmann::json j = nlohmann::json::array();
    std::lock_guard<std::mutex> lock(mtx_);

    for (auto const & [id, t] : terminals_) {
      j.push_back(
        {{"type", "String"},
          {"id", t->id},
          {"topic", t->topic},
          {"area", {t->area.x, t->area.y, t->area.w, t->area.h}},
          {"font_size", t->font_size},
          {"text_color",
            {t->text_color.r, t->text_color.g, t->text_color.b, t->text_color.a}},
          {"bg_color",
            {t->bg_color.r, t->bg_color.g, t->bg_color.b, t->bg_color.a}},
          {"title", t->title}});
    }
    for (auto const & [id, b] : bitmaps_) {
      j.push_back(
        {{"type", "Bitmap"},
          {"id", b->id},
          {"topic", b->topic},
          {"area", {b->area.x, b->area.y, b->area.w, b->area.h}},
          {"color", {b->color.r, b->color.g, b->color.b, b->color.a}}});
    }
    for (auto const & id : video_order_) {
      if (videos_.count(id)) {
        auto v = videos_[id];
        j.push_back(
          {{"type", "VideoStream"},
            {"id", v->id},
            {"topic", v->pipe_path},
            {"area", {v->area.x, v->area.y, v->area.w, v->area.h}},
            {"source_width", v->sw},
            {"source_height", v->sh},
            {"encoding", v->enc}});
      }
    }

    std_msgs::msg::String msg;
    msg.data = j.dump();
    events_changed_pub_->publish(msg);
  }

  void event_callback(const std_msgs::msg::String::SharedPtr msg)
  {
    try {
      auto data = nlohmann::json::parse(msg->data);
      if (!data.is_array()) {return;}

      bool changed = false;
      for (auto & cfg : data) {
        std::string act = cfg.value("action", "add");
        std::string typ = cfg.value("type", "String");
        std::string id = cfg.value("id", "");

        if (act == "clear_all") {
          std::lock_guard<std::mutex> lock_clr(mtx_);
          terminals_.clear(); bitmaps_.clear(); videos_.clear();
          video_order_.clear();
          changed = true; continue;
        }

        if (id.empty()) {
          RCLCPP_ERROR(this->get_logger(), "Missing mandatory 'id' field");
          continue;
        }

        if (act == "remove") {
          std::lock_guard<std::mutex> lock_rem(mtx_);
          terminals_.erase(id); bitmaps_.erase(id);
          videos_.erase(id);
          video_order_.erase(
            std::remove(
              video_order_.begin(), video_order_.end(),
              id), video_order_.end());
          changed = true; continue;
        }

        if (act != "add") {
          RCLCPP_ERROR(
            this->get_logger(), "Unknown action '%s' for id '%s'", act.c_str(),
            id.c_str());
          continue;
        }

        if (!cfg.contains("area") || !cfg["area"].is_array() || cfg["area"].size() != 4) {
          RCLCPP_ERROR(
            this->get_logger(), "Missing or invalid 'area' [x,y,w,h] for id '%s'", id.c_str());
          continue;
        }
        auto aj = cfg["area"];
        Rect area = {aj[0], aj[1], aj[2], aj[3]};

        if (typ == "String") {
          // Check for forbidden aliases
          if (cfg.contains("backgroundColor") || cfg.contains("textColor") ||
            cfg.contains("fontSize"))
          {
            RCLCPP_ERROR(
              this->get_logger(),
              "Forbidden field names detected (backgroundColor/textColor/fontSize). "
              "Use README conventions!");
          }

          auto tcj = cfg.value("text_color", json::array({255, 255, 255, 255}));
          auto bcj = cfg.value("bg_color", json::array({0, 0, 0, 150}));
          if (!tcj.is_array() || !bcj.is_array()) {
            RCLCPP_ERROR(
              this->get_logger(), "Colors must be arrays [r,g,b,a] for id '%s'",
              id.c_str());
            continue;
          }

          Color tc = {(uint8_t)tcj[0], (uint8_t)tcj[1], (uint8_t)tcj[2],
            static_cast<uint8_t>(tcj.size() > 3 ? (uint8_t)tcj[3] : 255)};
          Color bc = {(uint8_t)bcj[0], (uint8_t)bcj[1], (uint8_t)bcj[2],
            static_cast<uint8_t>(bcj.size() > 3 ? (uint8_t)bcj[3] : 255)};
          int fs = cfg.value("font_size", 16);
          int sc = std::max(1, fs / 8);
          int columns = cfg.value("columns", 0);
          double expire = cfg.value("expire", 0.0);
          rclcpp::Duration lifetime = rclcpp::Duration::from_seconds(expire);
          Alignment al = ALIGN_LEFT;
          std::string als = cfg.value("align", "left");
          if (als == "center") {
            al = ALIGN_CENTER;
          } else if (als == "right") {
            al = ALIGN_RIGHT;
          }
          std::string tit = cfg.value("title", "");
          std::string top = cfg.value("topic", "");
          std::string ms = cfg.value("mode", "default");
          TerminalMode tm = MODE_DEFAULT;
          if (ms == "clear_on_new") {
            tm = MODE_CLEAR_ON_NEW;
          } else if (ms == "append_newline") {
            tm = MODE_APPEND_NEWLINE;
          }
          std::string initial_text = cfg.value("text", "");

          {
            std::lock_guard<std::mutex> lock_str(mtx_);
            if (terminals_.count(id)) {
              terminals_[id]->terminal->set_area(area);
              terminals_[id]->terminal->set_colors(tc, bc);
              terminals_[id]->terminal->set_scale(sc);
              terminals_[id]->terminal->set_align(al);
              terminals_[id]->terminal->set_title(tit);
              terminals_[id]->terminal->set_columns(columns);
              terminals_[id]->terminal->set_mode(tm);
              terminals_[id]->lifetime = lifetime;
              terminals_[id]->area = area;
              terminals_[id]->text_color = tc;
              terminals_[id]->bg_color = bc;
              terminals_[id]->font_size = fs;
              terminals_[id]->title = tit;
              if (!initial_text.empty()) {
                terminals_[id]->terminal->append(initial_text);
              }
            } else {
              auto nt = std::make_shared<TermInst>();
              nt->id = id; nt->topic = top;
              nt->creation_time = this->now();
              nt->lifetime = lifetime;
              nt->area = area;
              nt->text_color = tc;
              nt->bg_color = bc;
              nt->font_size = fs;
              nt->title = tit;
              nt->terminal =
                std::make_unique<NanoTerminal>(area, tc, bc, sc, 100, al, tit, columns, tm);
              if (!initial_text.empty()) {
                nt->terminal->append(initial_text);
              }
              if (!top.empty()) {
                auto sub_options = rclcpp::SubscriptionOptions();
                sub_options.callback_group = cb_group_reentrant_;
                nt->sub = this->create_subscription<std_msgs::msg::String>(
                  top, queue_size_,
                  [this, id](const std_msgs::msg::String::SharedPtr m) {
                    std::lock_guard<std::mutex> lock_cb(mtx_);
                    if (terminals_.count(id)) {
                      terminals_[id]->terminal->append(m->data);
                    }
                  }, sub_options);
              }
              terminals_[id] = nt;
            }
          }
        } else if (typ == "Bitmap") {
          int dep = cfg.value("depth", 1);
          double expire = cfg.value("expire", 0.0);
          rclcpp::Duration lifetime = rclcpp::Duration::from_seconds(expire);
          auto fjc = cfg.value("color", json::array({255, 255, 255, 255}));

          if (!fjc.is_array()) {
            RCLCPP_ERROR(
              this->get_logger(), "Color must be an array [r,g,b,a] for id '%s'",
              id.c_str());
            continue;
          }

          Color fg = {(uint8_t)fjc[0], (uint8_t)fjc[1], (uint8_t)fjc[2],
            static_cast<uint8_t>(fjc.size() > 3 ? (uint8_t)fjc[3] : 255)};
          std::string top = cfg.value("topic", "");
          {
            std::lock_guard<std::mutex> lock_bmp(mtx_);
            if (bitmaps_.count(id)) {
              bitmaps_[id]->canvas->set_area(area);
              bitmaps_[id]->canvas->set_color(fg);
              bitmaps_[id]->lifetime = lifetime;
              bitmaps_[id]->area = area;
              bitmaps_[id]->color = fg;
            } else {
              auto bc = std::make_shared<BitInst>();
              bc->id = id; bc->topic = top;
              bc->creation_time = this->now();
              bc->lifetime = lifetime;
              bc->area = area;
              bc->color = fg;
              bc->canvas = std::make_unique<NanoCanvas>(area, fg, dep);
              if (!top.empty()) {
                auto sub_options = rclcpp::SubscriptionOptions();
                sub_options.callback_group = cb_group_reentrant_;
                bc->sub_bin = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
                  top,
                  queue_size_,
                  [this, id](const std_msgs::msg::UInt8MultiArray::SharedPtr m) {
                    std::lock_guard<std::mutex> lock_sub_bin(mtx_);
                    if (bitmaps_.count(id)) {
                      bitmaps_[id]->canvas->update_data(m->data);
                    }
                  }, sub_options);
                bc->sub_hex = this->create_subscription<std_msgs::msg::String>(
                  top + "/hex",
                  queue_size_,
                  [this, id](const std_msgs::msg::String::SharedPtr m) {
                    std::vector<uint8_t> b;
                    for (size_t i = 0; i + 1 < m->data.length(); i += 2) {
                      try {
                        std::string hex_sub = m->data.substr(i, 2);
                        uint8_t val = static_cast<uint8_t>(
                          std::stoul(hex_sub, nullptr, 16));
                        b.push_back(val);
                      } catch (...) {
                        break;
                      }
                    }
                    std::lock_guard<std::mutex> lock_sub_hex(mtx_);
                    if (bitmaps_.count(id)) {
                      bitmaps_[id]->canvas->update_data(b);
                    }
                  }, sub_options);
              }
              bitmaps_[id] = bc;
            }
          }
        } else if (typ == "VideoStream") {
          if (!cfg.contains("topic") || !cfg.contains("source_width") ||
            !cfg.contains("source_height"))
          {
            RCLCPP_ERROR(
              this->get_logger(),
              "VideoStream missing topic/source_width/height for id '%s'", id.c_str());
            continue;
          }
          std::string pipe = cfg["topic"];
          int sw = cfg["source_width"];
          int sh = cfg["source_height"];
          std::string enc = cfg.value("encoding", "rgb");
          if (enc != "rgb" && enc != "bgr") {
            RCLCPP_ERROR(this->get_logger(), "Unknown encoding '%s'. Using 'rgb'", enc.c_str());
            enc = "rgb";
          }
          double expire = cfg.value("expire", 0.0);
          rclcpp::Duration lifetime = rclcpp::Duration::from_seconds(expire);

          {
            std::lock_guard<std::mutex> lock_vid(mtx_);
            if (videos_.count(id)) {
              videos_[id]->video->set_area(area);
              videos_[id]->lifetime = lifetime;
              videos_[id]->area = area;
            } else {
              auto vi = std::make_shared<VideoInst>();
              vi->id = id; vi->pipe_path = pipe;
              vi->creation_time = this->now();
              vi->lifetime = lifetime;
              vi->area = area;
              vi->sw = sw;
              vi->sh = sh;
              vi->enc = enc;
              vi->video = std::make_unique<NanoVideo>(pipe, area, sw, sh, enc);
              videos_[id] = vi;
              video_order_.push_back(id);
              RCLCPP_INFO(
                this->get_logger(), "VideoStream '%s' matching pipe '%s' created.",
                id.c_str(), pipe.c_str());
            }
          }
        } else {
          RCLCPP_ERROR(
            this->get_logger(), "Unknown type '%s' for id '%s'", typ.c_str(),
            id.c_str());
        }
        changed = true;
      }
      if (changed) {
        publish_state();
      }
    } catch (const std::exception & e) {
      RCLCPP_ERROR(
        this->get_logger(), "JSON Parsing Error: %s in msg: %s",
        e.what(), msg->data.c_str());
    }
  }

  void render_loop()
  {
    while (running_ && rclcpp::ok()) {
      if (fifo_fd_ < 0) {
        fifo_fd_ = open(fifo_path_.c_str(), O_WRONLY | O_NONBLOCK);
        if (fifo_fd_ >= 0) {
          int flags = fcntl(fifo_fd_, F_GETFL, 0);
          fcntl(fifo_fd_, F_SETFL, flags & ~O_NONBLOCK);
          RCLCPP_INFO(this->get_logger(), "Nano-Viz: Output FIFO opened: %s", fifo_path_.c_str());
        }
      }

      auto frame_start = std::chrono::steady_clock::now();
      auto frame_duration = std::chrono::milliseconds(static_cast<int>(1000.0 / fps_));

      auto now = this->now();
      {
        std::lock_guard<std::mutex> lock(mtx_);
        std::fill(buffer_.begin(), buffer_.end(), 0);

        // Auto-expiration
        auto it_t = terminals_.begin();
        while (it_t != terminals_.end()) {
          if (it_t->second->lifetime.nanoseconds() > 0 &&
            (now - it_t->second->creation_time) > it_t->second->lifetime)
          {
            it_t = terminals_.erase(it_t);
          } else {++it_t;}
        }
        auto it_b = bitmaps_.begin();
        while (it_b != bitmaps_.end()) {
          if (it_b->second->lifetime.nanoseconds() > 0 &&
            (now - it_b->second->creation_time) > it_b->second->lifetime)
          {
            it_b = bitmaps_.erase(it_b);
          } else {++it_b;}
        }
        auto it_v = videos_.begin();
        while (it_v != videos_.end()) {
          if (it_v->second->lifetime.nanoseconds() > 0 &&
            (now - it_v->second->creation_time) > it_v->second->lifetime)
          {
            video_order_.erase(
              std::remove(
                video_order_.begin(),
                video_order_.end(), it_v->first), video_order_.end());
            it_v = videos_.erase(it_v);
          } else {++it_v;}
        }

        for (auto const & id : video_order_) {
          if (videos_.count(id)) {videos_[id]->video->draw(buffer_, width_, height_);}
        }
        for (auto const & [id, b] : bitmaps_) {b->canvas->draw(buffer_, width_, height_);}
        for (auto const & [id, t] : terminals_) {t->terminal->draw(buffer_, width_, height_);}
      }

      if (fifo_fd_ >= 0) {
        size_t total = buffer_.size();
        size_t written = 0;
        while (written < total && running_ && rclcpp::ok()) {
          ssize_t w = write(fifo_fd_, buffer_.data() + written, total - written);
          if (w > 0) {written += w;} else if (w == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
              continue;
            } else if (errno == EPIPE) {
              RCLCPP_WARN(
                this->get_logger(), "Output FIFO (FFmpeg) disconnected. Waiting for new reader...");
              close(fifo_fd_);
              fifo_fd_ = -1;
              break;
            } else {
              RCLCPP_ERROR(
                this->get_logger(), "Error writing to FIFO (errno: %d). Reopening...", errno);
              close(fifo_fd_);
              fifo_fd_ = -1;
              break;
            }
          }
        }
      }

      std::this_thread::sleep_until(frame_start + frame_duration);
    }
  }

  struct TermInst
  {
    std::string id, topic, title;
    Rect area;
    Color text_color, bg_color;
    int font_size;
    std::unique_ptr<NanoTerminal> terminal;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub;
    rclcpp::Time creation_time;
    rclcpp::Duration lifetime{0, 0};
  };
  struct BitInst
  {
    std::string id, topic;
    Rect area;
    Color color;
    std::unique_ptr<NanoCanvas> canvas;
    rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr sub_bin;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_hex;
    rclcpp::Time creation_time;
    rclcpp::Duration lifetime{0, 0};
  };
  struct VideoInst
  {
    std::string id, pipe_path, enc;
    Rect area;
    int sw, sh;
    std::unique_ptr<NanoVideo> video;
    rclcpp::Time creation_time;
    rclcpp::Duration lifetime{0, 0};
  };

  int width_, height_, queue_size_;
  double fps_;
  std::string fifo_path_;
  int fifo_fd_ = -1;
  std::vector<uint8_t> buffer_;
  std::map<std::string, std::shared_ptr<TermInst>> terminals_;
  std::map<std::string, std::shared_ptr<BitInst>> bitmaps_;
  std::map<std::string, std::shared_ptr<VideoInst>> videos_;
  std::vector<std::string> video_order_;
  std::mutex mtx_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr events_changed_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr event_sub_;
  rclcpp::CallbackGroup::SharedPtr cb_group_reentrant_;
  std::atomic<bool> running_{false};
  std::thread render_thread_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<NanoVizNode>();
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
