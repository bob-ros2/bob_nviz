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

#include "nlohmann/json.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

#include "bob_nviz/font8x8.h"

using namespace std::chrono_literals;
using json = nlohmann::json;

// --- Minimal Software Renderer Utilities ---

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

/**
 * @brief Simple Software Terminal implementation for nviz.
 */
class NanoTerminal
{
public:
  NanoTerminal(
    Rect area, Color text_color, Color bg_color, int scale, size_t line_limit,
    Alignment align, std::string title_in = "", int columns = 0)
  : area_(area), text_color_(text_color), bg_color_(bg_color), scale_(scale), align_(align),
    columns_override_(columns), line_limit_(line_limit)
  {
    for (unsigned char c : title_in) {
      if (c >= 32 && c <= 126) {
        title_ += static_cast<char>(c);
      }
    }
    update_wrap_width();
    lines_.push_back("");         // Start with one empty line
  }

  void append(const std::string & text)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    for (unsigned char c : text) {
      if (c == '\n') {
        lines_.push_back("");
      } else if (c == '\r') {
        continue;
      } else if (c == '\t') {
        for (int i = 0; i < 4; ++i) {
          add_char_to_current(' ');
        }
      } else if (c >= 32 && c <= 126) {
        add_char_to_current(static_cast<char>(c));
      }
    }

    while (lines_.size() > line_limit_) {
      lines_.pop_front();
    }
  }

  void draw(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Draw Background (BGRA)
    for (int y = area_.y; y < area_.y + area_.h; ++y) {
      if (y < 0 || y >= buffer_h) {continue;}
      for (int x = area_.x; x < area_.x + area_.w; ++x) {
        if (x < 0 || x >= buffer_w) {continue;}
        int idx = (y * buffer_w + x) * 4;
        if (bg_color_.a == 255) {
          buffer[idx] = bg_color_.b;
          buffer[idx + 1] = bg_color_.g;
          buffer[idx + 2] = bg_color_.r;
          buffer[idx + 3] = 255;
        } else if (bg_color_.a > 0) {
          float alpha = bg_color_.a / 255.0f;
          buffer[idx] = static_cast<uint8_t>(buffer[idx] * (1.0f - alpha) + bg_color_.b * alpha);
          buffer[idx + 1] = static_cast<uint8_t>(
            buffer[idx + 1] * (1.0f - alpha) + bg_color_.g * alpha);
          buffer[idx + 2] = static_cast<uint8_t>(
            buffer[idx + 2] * (1.0f - alpha) + bg_color_.r * alpha);
          buffer[idx + 3] = 255;
        }
      }
    }

    int cur_y = area_.y;

    // 2. Draw Title Bar (if title present)
    if (!title_.empty()) {
      int title_h = 8 * scale_ + 6;
      for (int y = area_.y; y < area_.y + title_h; ++y) {
        if (y < 0 || y >= buffer_h) {continue;}
        for (int x = area_.x; x < area_.x + area_.w; ++x) {
          if (x < 0 || x >= buffer_w) {continue;}
          int idx = (y * buffer_w + x) * 4;
          buffer[idx] = static_cast<uint8_t>(buffer[idx] * 0.5f);
          buffer[idx + 1] = static_cast<uint8_t>(buffer[idx + 1] * 0.5f);
          buffer[idx + 2] = static_cast<uint8_t>(buffer[idx + 2] * 0.5f);
        }
      }
      int title_px_w = static_cast<int>(title_.length()) * 8 * scale_;
      int title_x = area_.x + (area_.w - title_px_w) / 2;
      for (char c : title_) {
        draw_char(buffer, buffer_w, buffer_h, c, title_x, area_.y + 3, {255, 255, 255, 255});
        title_x += 8 * scale_;
      }
      cur_y += title_h + 4;
    } else {
      cur_y += 4;
    }

    // 3. Draw Text (8x8 font scaled) with Autoscroll
    int char_h = 8 * scale_;
    int char_w = 8 * scale_;
    int line_spacing = 2;
    int max_text_h = area_.y + area_.h - cur_y - 4;
    int lines_that_fit = max_text_h / (char_h + line_spacing);
    if (lines_that_fit < 1) {
      lines_that_fit = 1;
    }

    size_t start_idx = 0;
    if (lines_.size() > static_cast<size_t>(lines_that_fit)) {
      start_idx = lines_.size() - static_cast<size_t>(lines_that_fit);
    }

    for (size_t i = start_idx; i < lines_.size(); ++i) {
      const auto & line = lines_[i];
      int line_px_w = static_cast<int>(line.length()) * char_w;
      int cur_x = area_.x + 4;
      if (align_ == ALIGN_RIGHT) {
        cur_x = area_.x + area_.w - line_px_w - 4;
      } else if (align_ == ALIGN_CENTER) {
        cur_x = area_.x + (area_.w - line_px_w) / 2;
      }

      for (char c : line) {
        draw_char(buffer, buffer_w, buffer_h, c, cur_x, cur_y, text_color_);
        cur_x += char_w;
      }
      cur_y += char_h + line_spacing;
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
  void set_title(const std::string & title)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    title_ = "";
    for (unsigned char c : title) {
      if (c >= 32 && c <= 126) {
        title_ += static_cast<char>(c);
      }
    }
  }

private:
  void update_wrap_width()
  {
    if (columns_override_ > 0) {
      wrap_width_ = static_cast<size_t>(columns_override_);
    } else {
      int char_w = 8 * scale_;
      wrap_width_ = static_cast<size_t>((area_.w - 10) / char_w);
    }
    if (wrap_width_ < 1) {
      wrap_width_ = 1;
    }
  }

  void add_char_to_current(char c)
  {
    if (lines_.back().length() >= wrap_width_) {
      lines_.push_back("");
    }
    lines_.back() += c;
  }

  void draw_char(
    std::vector<uint8_t> & buffer, int buf_w, int buf_h, char c, int start_x,
    int start_y, Color color)
  {
    if (c < 32 || c > 126) {return;}
    const uint8_t * bitmap = font8x8_basic[c - 32];
    for (int row = 0; row < 8; ++row) {
      for (int col = 0; col < 8; ++col) {
        if (bitmap[row] & (1 << (7 - col))) {
          for (int py = 0; py < scale_; ++py) {
            for (int px = 0; px < scale_; ++px) {
              int tx = start_x + col * scale_ + px;
              int ty = start_y + row * scale_ + py;
              if (tx >= 0 && tx < buf_w && ty >= 0 && ty < buf_h) {
                int idx = (ty * buf_w + tx) * 4;
                buffer[idx] = color.b;
                buffer[idx + 1] = color.g;
                buffer[idx + 2] = color.r;
                buffer[idx + 3] = 255;
              }
            }
          }
        }
      }
    }
  }

  Rect area_;
  Color text_color_;
  Color bg_color_;
  int scale_;
  Alignment align_;
  int columns_override_;
  size_t line_limit_;
  size_t wrap_width_;
  std::string title_;
  std::deque<std::string> lines_;
  std::mutex mutex_;
};

/**
 * @brief Simple Bitmap/Canvas implementation for nviz.
 * Supports 1-bit (bitmask) and 8-bit (grayscale) rendering.
 */
class NanoCanvas
{
public:
  NanoCanvas(Rect area, Color fg_color, int depth)
  : area_(area), fg_color_(fg_color), depth_(depth)
  {
    size_t sz = (depth_ == 8) ?
      static_cast<size_t>(area.w * area.h) :
      static_cast<size_t>((area.w * area.h + 7) / 8);
    data_.resize(sz, 0);
  }

  void update_data(const std::vector<uint8_t> & new_data)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t expected_size = (depth_ == 8) ?
      static_cast<size_t>(area_.w * area_.h) :
      static_cast<size_t>((area_.w * area_.h + 7) / 8);
    if (new_data.size() < expected_size) {
      return;
    }
    std::copy(new_data.begin(), new_data.begin() + expected_size, data_.begin());
  }

  void draw(std::vector<uint8_t> & buffer, int buf_w, int buf_h)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data_.empty()) {
      return;
    }

    if (depth_ == 8) {
      for (int y = 0; y < area_.h; ++y) {
        for (int x = 0; x < area_.w; ++x) {
          int tx = area_.x + x;
          int ty = area_.y + y;
          if (tx >= 0 && tx < buf_w && ty >= 0 && ty < buf_h) {
            uint8_t val = data_[y * area_.w + x];
            if (val == 0) {
              continue;
            }
            int idx = (ty * buf_w + tx) * 4;
            buffer[idx] = static_cast<uint8_t>((fg_color_.b * val) / 255);
            buffer[idx + 1] = static_cast<uint8_t>((fg_color_.g * val) / 255);
            buffer[idx + 2] = static_cast<uint8_t>((fg_color_.r * val) / 255);
            buffer[idx + 3] = 255;
          }
        }
      }
    } else {
      for (int y = 0; y < area_.h; ++y) {
        for (int x = 0; x < area_.w; ++x) {
          int tx = area_.x + x;
          int ty = area_.y + y;
          if (tx >= 0 && tx < buf_w && ty >= 0 && ty < buf_h) {
            int bit_idx = y * area_.w + x;
            if (data_[bit_idx / 8] & (1 << (7 - (bit_idx % 8)))) {
              int idx = (ty * buf_w + tx) * 4;
              buffer[idx] = fg_color_.b;
              buffer[idx + 1] = fg_color_.g;
              buffer[idx + 2] = fg_color_.r;
              buffer[idx + 3] = 255;
            }
          }
        }
      }
    }
  }

  void set_area(Rect area)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    area_ = area;
  }
  void set_color(Color fg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    fg_color_ = fg;
  }

private:
  Rect area_;
  Color fg_color_;
  int depth_;
  std::vector<uint8_t> data_;
  std::mutex mutex_;
};

// --- ROS 2 Node ---

class NanoVizNode : public rclcpp::Node
{
public:
  NanoVizNode()
  : Node("nviz")
  {
    signal(SIGPIPE, SIG_IGN);

    // Parameters
    this->declare_parameter("width", 854);
    this->declare_parameter("height", 480);
    this->declare_parameter("fps", 30.0);
    this->declare_parameter("fifo_path", "/tmp/nano_fifo");

    width_ = this->get_parameter("width").as_int();
    height_ = this->get_parameter("height").as_int();
    fps_ = this->get_parameter("fps").as_double();
    fifo_path_ = this->get_parameter("fifo_path").as_string();

    buffer_.resize(width_ * height_ * 4, 0);     // BGRA

    // Topics
    events_changed_pub_ = this->create_publisher<std_msgs::msg::String>(
      "events_changed", rclcpp::QoS(10).transient_local());

    event_sub_ = this->create_subscription<std_msgs::msg::String>(
      "events", 10, std::bind(&NanoVizNode::event_callback, this, std::placeholders::_1));

    // Timer for rendering
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(static_cast<int>(1000.0 / fps_)),
      std::bind(&NanoVizNode::render_loop, this));

    RCLCPP_INFO(
      this->get_logger(), "Nano-Viz initialized (%dx%d @ %.1f fps)", width_, height_,
      fps_);
  }

  ~NanoVizNode()
  {
    if (fifo_fd_ != -1) {
      close(fifo_fd_);
    }
  }

private:
  void event_callback(const std_msgs::msg::String::SharedPtr msg)
  {
    try {
      auto config_array = json::parse(msg->data);
      if (!config_array.is_array()) {
        return;
      }

      bool state_changed = false;
      for (const auto & config : config_array) {
        std::string action = config.value("action", "add");
        std::string type = config.value("type", "String");
        std::string id = config.value("id", "");
        std::string topic = config.value("topic", "");

        auto area_j = config.value("area", json::array({10, 10, 300, 200}));
        Rect area = {area_j[0], area_j[1], area_j[2], area_j[3]};

        if (action == "remove") {
          std::lock_guard<std::mutex> lock(terminals_mutex_);
          terminals_.erase(id);
          bitmaps_.erase(id);
          state_changed = true;
          continue;
        }

        if (id.empty()) {
          id = "e" + std::to_string(terminals_.size() + bitmaps_.size());
        }

        if (type == "String") {
          auto t_color_j = config.value("text_color", json::array({255, 255, 255, 255}));
          Color text_color = {
            static_cast<uint8_t>(t_color_j[0]),
            static_cast<uint8_t>(t_color_j[1]),
            static_cast<uint8_t>(t_color_j[2]),
            static_cast<uint8_t>(t_color_j[3])};
          auto b_color_j = config.value("bg_color", json::array({0, 0, 0, 150}));
          Color bg_color = {
            static_cast<uint8_t>(b_color_j[0]),
            static_cast<uint8_t>(b_color_j[1]),
            static_cast<uint8_t>(b_color_j[2]),
            static_cast<uint8_t>(b_color_j[3])};
          int font_size = config.value("font_size", 16);
          int scale = font_size / 8;
          if (scale < 1) {
            scale = 1;
          }
          int columns = config.value("columns", 0);
          Alignment align = ALIGN_LEFT;
          std::string align_s = config.value("align", "left");
          if (align_s == "center") {
            align = ALIGN_CENTER;
          } else if (align_s == "right") {
            align = ALIGN_RIGHT;
          }
          std::string title = config.value("title", "");

          std::lock_guard<std::mutex> lock(terminals_mutex_);
          if (terminals_.count(id)) {
            terminals_[id]->terminal->set_area(area);
            terminals_[id]->terminal->set_colors(text_color, bg_color);
            terminals_[id]->terminal->set_scale(scale);
            terminals_[id]->terminal->set_align(align);
            terminals_[id]->terminal->set_title(title);
            terminals_[id]->terminal->set_columns(columns);
          } else {
            auto nt = std::make_shared<TerminalInstance>();
            nt->id = id; nt->topic = topic;
            nt->terminal = std::make_unique<NanoTerminal>(
              area, text_color, bg_color, scale, 100, align, title, columns);
            if (!topic.empty()) {
              nt->sub = this->create_subscription<std_msgs::msg::String>(
                topic, 10, [this, id](const std_msgs::msg::String::SharedPtr t_msg) {
                  std::lock_guard<std::mutex> t_lock(terminals_mutex_);
                  if (terminals_.count(id)) {
                    terminals_[id]->terminal->append(t_msg->data);
                  }
                });
            }
            terminals_[id] = nt;
          }
        } else if (type == "Bitmap") {
          int depth = config.value("depth", 1);
          auto fg_color_j = config.value(
            "color", config.value("text_color", json::array({255, 255, 255, 255})));
          Color fg_color = {
            static_cast<uint8_t>(fg_color_j[0]), static_cast<uint8_t>(fg_color_j[1]),
            static_cast<uint8_t>(fg_color_j[2]), static_cast<uint8_t>(fg_color_j[3])};

          std::lock_guard<std::mutex> lock(terminals_mutex_);
          if (bitmaps_.count(id)) {
            bitmaps_[id]->canvas->set_area(area);
            bitmaps_[id]->canvas->set_color(fg_color);
          } else {
            auto bc = std::make_shared<BitmapInstance>();
            bc->id = id; bc->topic = topic;
            bc->canvas = std::make_unique<NanoCanvas>(area, fg_color, depth);
            if (!topic.empty()) {
              bc->sub_bin = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
                topic, 10, [this, id](const std_msgs::msg::UInt8MultiArray::SharedPtr b_msg) {
                  std::lock_guard<std::mutex> t_lock(terminals_mutex_);
                  if (bitmaps_.count(id)) {
                    bitmaps_[id]->canvas->update_data(b_msg->data);
                  }
                });
              bc->sub_hex = this->create_subscription<std_msgs::msg::String>(
                topic + "/hex", 10, [this, id](const std_msgs::msg::String::SharedPtr s_msg) {
                  std::vector<uint8_t> bytes;
                  for (size_t i = 0; i + 1 < s_msg->data.length(); i += 2) {
                    try {
                      bytes.push_back(
                        static_cast<uint8_t>(std::stoul(s_msg->data.substr(i, 2), nullptr, 16)));
                    } catch (...) {break;}
                  }
                  std::lock_guard<std::mutex> t_lock(terminals_mutex_);
                  if (bitmaps_.count(id)) {
                    bitmaps_[id]->canvas->update_data(bytes);
                  }
                });
            }
            bitmaps_[id] = bc;
          }
        }
        state_changed = true;
      }
      if (state_changed) {
        publish_state();
      }
    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "JSON Error: %s", e.what());
    }
  }

  void publish_state()
  {
    json j = json::array();
    std::lock_guard<std::mutex> lock(terminals_mutex_);
    for (auto const & [id, ti] : terminals_) {
      j.push_back({{"id", id}, {"type", "String"}, {"topic", ti->topic}});
    }
    for (auto const & [id, bi] : bitmaps_) {
      j.push_back({{"id", id}, {"type", "Bitmap"}, {"topic", bi->topic}});
    }
    std_msgs::msg::String msg; msg.data = j.dump();
    events_changed_pub_->publish(msg);
  }

  void render_loop()
  {
    std::fill(buffer_.begin(), buffer_.end(), 0);
    {
      std::lock_guard<std::mutex> lock(terminals_mutex_);
      for (auto const & [id, ti] : terminals_) {
        ti->terminal->draw(buffer_, width_, height_);
      }
      for (auto const & [id, bi] : bitmaps_) {
        bi->canvas->draw(buffer_, width_, height_);
      }
    }
    if (fifo_fd_ == -1) {
      if (access(fifo_path_.c_str(), F_OK) == -1) {
        mkfifo(fifo_path_.c_str(), 0666);
      }
      fifo_fd_ = open(fifo_path_.c_str(), O_WRONLY | O_NONBLOCK);
    }
    if (fifo_fd_ != -1) {
      size_t total_written = 0;
      const uint8_t * data_ptr = buffer_.data();
      size_t data_size = buffer_.size();
      while (total_written < data_size) {
        ssize_t written = write(fifo_fd_, data_ptr + total_written, data_size - total_written);
        if (written > 0) {
          total_written += static_cast<size_t>(written);
        } else if (written == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            usleep(1000);
            if (total_written > 0) {continue;} else {return;}
          } else if (errno == EPIPE) {
            RCLCPP_WARN_THROTTLE(
              this->get_logger(),
              *this->get_clock(), 5000, "FIFO reader disconnected.");
            close(fifo_fd_); fifo_fd_ = -1; break;
          } else {break;}
        }
      }
    }
  }

  struct TerminalInstance
  {
    std::string id; std::string topic;
    std::unique_ptr<NanoTerminal> terminal;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub;
  };
  struct BitmapInstance
  {
    std::string id; std::string topic;
    std::unique_ptr<NanoCanvas> canvas;
    rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr sub_bin;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_hex;
  };

  int width_, height_;
  double fps_;
  std::string fifo_path_;
  int fifo_fd_ = -1;
  std::vector<uint8_t> buffer_;
  std::map<std::string, std::shared_ptr<TerminalInstance>> terminals_;
  std::map<std::string, std::shared_ptr<BitmapInstance>> bitmaps_;
  std::mutex terminals_mutex_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr events_changed_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr event_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<NanoVizNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
