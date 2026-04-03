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

  void append(const std::string & text)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == MODE_CLEAR_ON_NEW && !text.empty()) {
      lines_.clear();
      lines_.push_back("");
    }
    for (unsigned char c : text) {
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

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
    lines_.push_back("");
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
    size_t start = (lines_.size() > static_cast<size_t>(fit)) ?
      (lines_.size() - static_cast<size_t>(fit)) : 0;

    for (size_t i = start; i < lines_.size(); ++i) {
      int cur_x = area_.x + 4;
      if (align_ == ALIGN_RIGHT) {
        cur_x = area_.x + area_.w - static_cast<int>(lines_[i].length()) * cw - 4;
      } else if (align_ == ALIGN_CENTER) {
        cur_x = area_.x + (area_.w - static_cast<int>(lines_[i].length()) * cw) / 2;
      }
      for (unsigned char c : lines_[i]) {
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
      lines_.push_back("");
    } else if (c == '\t') {
      for (int i = 0; i < 4; ++i) {
        add_raw_char(' ');
      }
    } else if (c >= 32) {
      add_raw_char(c);
    }
  }
  void add_raw_char(uint8_t c)
  {
    if (lines_.back().length() >= wrap_width_) {
      lines_.push_back("");
    }
    lines_.back() += static_cast<char>(c);
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
                b[idx] = col.b;
                b[idx + 1] = col.g;
                b[idx + 2] = col.r;
                b[idx + 3] = 255;
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
  std::string title_;
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
            b[idx] = (fg_.b * val) / 255;
            b[idx + 1] = (fg_.g * val) / 255;
            b[idx + 2] = (fg_.r * val) / 255;
            b[idx + 3] = 255;
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
    events_changed_pub_ = this->create_publisher<std_msgs::msg::String>(
      "events_changed", rclcpp::QoS(10).transient_local());
    event_sub_ = this->create_subscription<std_msgs::msg::String>(
      "events", 10, std::bind(&NanoVizNode::event_callback, this, std::placeholders::_1));
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(
        static_cast<int>(1000.0 / fps_)), std::bind(&NanoVizNode::render_loop, this));
    RCLCPP_INFO(this->get_logger(), "Nano-Viz: %dx%d @ %.1f fps", width_, height_, fps_);
    publish_state();
  }
  ~NanoVizNode()
  {
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

  void event_callback(const std_msgs::msg::String::SharedPtr msg)
  {
    try {
      auto arr = json::parse(msg->data);
      if (!arr.is_array()) {
        return;
      }
      bool changed = false;
      for (const auto & cfg : arr) {
        std::string act = cfg.value("action", "add");
        std::string typ = cfg.value("type", "String");
        std::string id = cfg.value("id", "");
        std::string top = cfg.value("topic", "");
        auto aj = cfg.value("area", json::array({10, 10, 300, 200}));
        Rect area = {aj[0], aj[1], aj[2], aj[3]};
        if (act == "remove") {
          std::lock_guard<std::mutex> lock_rem(mtx_);
          terminals_.erase(id); bitmaps_.erase(id); changed = true; continue;
        }
        if (id.empty()) {
          id = "e" + std::to_string(terminals_.size() + bitmaps_.size());
        }
        if (typ == "String") {
          auto tcj = cfg.value("text_color", json::array({255, 255, 255, 255}));
          Color tc = {(uint8_t)tcj[0], (uint8_t)tcj[1], (uint8_t)tcj[2], (uint8_t)tcj[3]};
          auto bcj = cfg.value("bg_color", json::array({0, 0, 0, 150}));
          Color bc = {(uint8_t)bcj[0], (uint8_t)bcj[1], (uint8_t)bcj[2], (uint8_t)bcj[3]};
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
              if (!initial_text.empty()) {
                terminals_[id]->terminal->append(initial_text);
              }
            } else {
              auto nt = std::make_shared<TermInst>();
              nt->id = id; nt->topic = top;
              nt->creation_time = this->now();
              nt->lifetime = lifetime;
              nt->terminal =
                std::make_unique<NanoTerminal>(area, tc, bc, sc, 100, al, tit, columns, tm);
              if (!initial_text.empty()) {
                nt->terminal->append(initial_text);
              }
              if (!top.empty()) {
                nt->sub = this->create_subscription<std_msgs::msg::String>(
                  top, queue_size_,
                  [this, id](const std_msgs::msg::String::SharedPtr m) {
                    std::lock_guard<std::mutex> lock_cb(mtx_);
                    if (terminals_.count(id)) {
                      terminals_[id]->terminal->append(m->data);
                    }
                  });
              }
              terminals_[id] = nt;
            }
          }
        } else if (typ == "Bitmap") {
          int dep = cfg.value("depth", 1);
          double expire = cfg.value("expire", 0.0);
          rclcpp::Duration lifetime = rclcpp::Duration::from_seconds(expire);
          auto fjc = cfg.value("color", cfg.value("text_color", json::array({255, 255, 255, 255})));
          Color fg = {(uint8_t)fjc[0], (uint8_t)fjc[1], (uint8_t)fjc[2], (uint8_t)fjc[3]};
          {
            std::lock_guard<std::mutex> lock_bmp(mtx_);
            if (bitmaps_.count(id)) {
              bitmaps_[id]->canvas->set_area(area);
              bitmaps_[id]->canvas->set_color(fg);
              bitmaps_[id]->lifetime = lifetime;
            } else {
              auto bc = std::make_shared<BitInst>();
              bc->id = id; bc->topic = top;
              bc->creation_time = this->now();
              bc->lifetime = lifetime;
              bc->canvas = std::make_unique<NanoCanvas>(area, fg, dep);
              if (!top.empty()) {
                bc->sub_bin = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
                  top,
                  queue_size_,
                  [this, id](const std_msgs::msg::UInt8MultiArray::SharedPtr m) {
                    std::lock_guard<std::mutex> lock_sub_bin(mtx_);
                    if (bitmaps_.count(id)) {
                      bitmaps_[id]->canvas->update_data(m->data);
                    }
                  });
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
                  });
              }
              bitmaps_[id] = bc;
            }
          }
        }
        changed = true;
      }
      if (changed) {
        publish_state();
      }
    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "JSON: %s", e.what());
    }
  }

  void publish_state()
  {
    json j = json::array();
    std::lock_guard<std::mutex> lock_change(mtx_);
    for (auto const & [i, t] : terminals_) {
      j.push_back(
        {{"id", i}, {"type", "String"}, {"topic", t->topic}, {"expire", t->lifetime.seconds()}});
    }
    for (auto const & [i, b] : bitmaps_) {
      j.push_back(
        {{"id", i}, {"type", "Bitmap"}, {"topic", b->topic}, {"expire", b->lifetime.seconds()}});
    }
    std_msgs::msg::String m;
    m.data = j.dump();
    events_changed_pub_->publish(m);
  }

  void render_loop()
  {
    bool changed = false;
    {
      auto now = this->now();
      std::lock_guard<std::mutex> lock_expire(mtx_);
      for (auto it = terminals_.begin(); it != terminals_.end(); ) {
        if (it->second->lifetime.nanoseconds() > 0 &&
          (it->second->creation_time + it->second->lifetime) < now)
        {
          it = terminals_.erase(it);
          changed = true;
        } else {
          ++it;
        }
      }
      for (auto it = bitmaps_.begin(); it != bitmaps_.end(); ) {
        if (it->second->lifetime.nanoseconds() > 0 &&
          (it->second->creation_time + it->second->lifetime) < now)
        {
          it = bitmaps_.erase(it);
          changed = true;
        } else {
          ++it;
        }
      }
    }
    if (changed) {
      publish_state();
    }

    std::fill(buffer_.begin(), buffer_.end(), 0);
    {
      std::lock_guard<std::mutex> lock_render(mtx_);
      for (auto const & [id, t] : terminals_) {
        t->terminal->draw(buffer_, width_, height_);
      }
      for (auto const & [id, b] : bitmaps_) {
        b->canvas->draw(buffer_, width_, height_);
      }
    }
    if (fifo_fd_ == -1) {
      if (access(fifo_path_.c_str(), F_OK) == -1) {
        mkfifo(fifo_path_.c_str(), 0666);
      }
      fifo_fd_ = open(fifo_path_.c_str(), O_WRONLY | O_NONBLOCK);
    }
    if (fifo_fd_ != -1) {
      size_t written = 0;
      while (written < buffer_.size()) {
        ssize_t w = write(fifo_fd_, buffer_.data() + written, buffer_.size() - written);
        if (w > 0) {
          written += static_cast<size_t>(w);
        } else if (w == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            usleep(1000);
            if (written > 0) {
              continue;
            } else {
              return;
            }
          } else if (errno == EPIPE) {
            close(fifo_fd_);
            fifo_fd_ = -1;
            break;
          } else {
            break;
          }
        }
      }
    }
  }

  struct TermInst
  {
    std::string id, topic;
    std::unique_ptr<NanoTerminal> terminal;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub;
    rclcpp::Time creation_time;
    rclcpp::Duration lifetime{0, 0};
  };
  struct BitInst
  {
    std::string id, topic;
    std::unique_ptr<NanoCanvas> canvas;
    rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr sub_bin;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_hex;
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
  std::mutex mtx_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr events_changed_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr event_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NanoVizNode>());
  rclcpp::shutdown();
  return 0;
}
