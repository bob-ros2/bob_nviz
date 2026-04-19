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
#include <poll.h>
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

#include "bob_nviz/types.hpp"
#include "bob_nviz/render_utils.hpp"
#include "bob_nviz/nano_terminal.hpp"
#include "bob_nviz/nano_canvas.hpp"
#include "bob_nviz/nano_video.hpp"
#include "bob_nviz/nano_marker_layer.hpp"

using namespace std::chrono_literals;
using bob_nviz::NanoTerminal;
using bob_nviz::NanoCanvas;
using bob_nviz::NanoVideo;
using bob_nviz::NanoMarkerLayer;
using bob_nviz::TerminalMode;
using bob_nviz::Color;
using bob_nviz::Rect;
using bob_nviz::ALIGN_LEFT;
using bob_nviz::MODE_DEFAULT;
using bob_nviz::MODE_CLEAR_ON_NEW;
using bob_nviz::MODE_APPEND_NEWLINE;
using json = nlohmann::json;

class NanoVizNode : public rclcpp::Node
{
public:
  NanoVizNode()
  : Node("nviz")
  {
    signal(SIGPIPE, SIG_IGN);

    this->declare_parameter("width", 854);
    this->declare_parameter("height", 480);
    this->declare_parameter("fps", 30.0);
    this->declare_parameter("fifo_path", "/tmp/nano_fifo");
    this->declare_parameter("queue_size", 1000);

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

  std::string mode_to_str(TerminalMode m)
  {
    if (m == MODE_CLEAR_ON_NEW) {return "clear_on_new";}
    if (m == MODE_APPEND_NEWLINE) {return "append_newline";}
    return "default";
  }

  void publish_state()
  {
    std::lock_guard<std::mutex> lock(mtx_);
    publish_state_impl();
  }

  void publish_state_impl()
  {
    json j = json::array();

    for (auto const & [id, t] : terminals_) {
      j.push_back(
      {
        {"type", "String"}, {"id", t->id}, {"topic", t->topic},
        {"area", {t->area.x, t->area.y, t->area.w, t->area.h}},
        {"font_size", t->font_size},
        {"text_color", {t->text_color.r, t->text_color.g, t->text_color.b, t->text_color.a}},
        {"bg_color", {t->bg_color.r, t->bg_color.g, t->bg_color.b, t->bg_color.a}},
        {"mode", mode_to_str(t->terminal->get_mode())},
        {"title", t->title}
      });
    }
    for (auto const & [id, b] : bitmaps_) {
      j.push_back(
      {
        {"type", "Bitmap"}, {"id", b->id}, {"topic", b->topic},
        {"area", {b->area.x, b->area.y, b->area.w, b->area.h}},
        {"color", {b->color.r, b->color.g, b->color.b, b->color.a}}
      });
    }
    for (auto const & id : video_order_) {
      if (videos_.count(id)) {
        auto v = videos_[id];
        j.push_back(
        {
          {"type", "VideoStream"}, {"id", v->id}, {"topic", v->pipe_path},
          {"area", {v->area.x, v->area.y, v->area.w, v->area.h}},
          {"source_width", v->sw}, {"source_height", v->sh}, {"encoding", v->enc}
        });
      }
    }

    for (auto const & [id, m] : markers_) {
      j.push_back(
      {
        {"type", "MarkerLayer"}, {"id", m->id}, {"topic", m->topic},
        {"area", {m->area.x, m->area.y, m->area.w, m->area.h}},
        {"scale", m->scale}, {"title", m->title}
      });
    }

    std_msgs::msg::String msg;
    msg.data = j.dump();
    events_changed_pub_->publish(msg);
  }

  void event_callback(const std_msgs::msg::String::SharedPtr msg)
  {
    try {
      auto data = json::parse(msg->data);
      if (!data.is_array()) {return;}

      std::lock_guard<std::mutex> lock(mtx_);
      bool changed = false;
      for (auto & cfg : data) {
        std::string act = cfg.value("action", "add");
        std::string typ = cfg.value("type", "String");
        std::string id = cfg.value("id", "");

        if (act == "clear_all") {
          terminals_.clear(); bitmaps_.clear(); videos_.clear(); markers_.clear();
          video_order_.clear();
          changed = true; continue;
        }

        if (id.empty()) {
          RCLCPP_ERROR(this->get_logger(), "Missing mandatory 'id' field");
          continue;
        }

        if (act == "remove") {
          terminals_.erase(id); bitmaps_.erase(id); videos_.erase(id); markers_.erase(id);
          video_order_.erase(
            std::remove(
              video_order_.begin(),
              video_order_.end(), id), video_order_.end());
          changed = true; continue;
        }

        auto aj = cfg["area"];
        Rect area = {aj[0], aj[1], aj[2], aj[3]};

        if (typ == "String") {
          auto tc_j = cfg.value("text_color", json::array({200, 200, 200, 255}));
          Color tc = {tc_j[0], tc_j[1], tc_j[2], tc_j[3]};
          auto bg_j = cfg.value("bg_color", json::array({30, 30, 30, 180}));
          Color bg = {bg_j[0], bg_j[1], bg_j[2], bg_j[3]};
          int fs = cfg.value("font_size", 1);
          std::string topic = cfg.value("topic", "");
          std::string mode_s = cfg.value("mode", "default");
          TerminalMode mode = MODE_DEFAULT;
          if (mode_s == "clear_on_new") {
            mode = MODE_CLEAR_ON_NEW;
          } else if (mode_s == "append_newline") {mode = MODE_APPEND_NEWLINE;}

          auto inst = std::make_shared<TermInst>();
          inst->id = id; inst->topic = topic; inst->area = area;
          inst->text_color = tc; inst->bg_color = bg; inst->font_size = fs;
          inst->title = cfg.value("title", "");
          inst->terminal = std::make_unique<NanoTerminal>(
            area, tc, bg, fs, 50, ALIGN_LEFT,
            inst->title, 0, mode);
          inst->creation_time = this->now();
          inst->lifetime = rclcpp::Duration::from_seconds(cfg.value("expire", 0.0));

          if (!topic.empty()) {
            inst->sub = this->create_subscription<std_msgs::msg::String>(
              topic, 10, [this, id](const std_msgs::msg::String::SharedPtr smsg) {
                std::lock_guard<std::mutex> l(mtx_);
                if (terminals_.count(id)) {terminals_[id]->terminal->append(smsg->data);}
              });
          }
          terminals_[id] = inst;
        } else if (typ == "Bitmap") {
          auto col_j = cfg.value("color", json::array({255, 255, 255, 255}));
          Color col = {col_j[0], col_j[1], col_j[2], col_j[3]};
          int depth = cfg.value("depth", 1);
          std::string topic = cfg.value("topic", "");

          auto inst = std::make_shared<BitInst>();
          inst->id = id; inst->topic = topic; inst->area = area; inst->color = col;
          inst->canvas = std::make_unique<NanoCanvas>(area, col, depth);
          inst->creation_time = this->now();
          inst->lifetime = rclcpp::Duration::from_seconds(cfg.value("expire", 0.0));

          if (!topic.empty()) {
            if (depth == 1) {
              inst->sub_hex = this->create_subscription<std_msgs::msg::String>(
                topic, 10, [this, id](const std_msgs::msg::String::SharedPtr smsg) {
                  std::lock_guard<std::mutex> l(mtx_);
                  if (bitmaps_.count(id)) {
                    // Simple hex to bin
                    std::vector<uint8_t> bin;
                    for (size_t i = 0; i < smsg->data.length(); i += 2) {
                      bin.push_back(
                        static_cast<uint8_t>(std::stoul(
                          smsg->data.substr(i, 2),
                          nullptr, 16)));
                    }
                    bitmaps_[id]->canvas->update_data(bin);
                  }
                });
            } else {
              inst->sub_bin = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
                topic, 10, [this, id](const std_msgs::msg::UInt8MultiArray::SharedPtr amsg) {
                  std::lock_guard<std::mutex> l(mtx_);
                  if (bitmaps_.count(id)) {bitmaps_[id]->canvas->update_data(amsg->data);}
                });
            }
          }
          bitmaps_[id] = inst;
        } else if (typ == "VideoStream") {
          std::string pipe = cfg.value("topic", "");
          int sw = cfg.value("source_width", 640);
          int sh = cfg.value("source_height", 480);
          std::string enc = cfg.value("encoding", "rgb");

          auto inst = std::make_shared<VideoInst>();
          inst->id = id; inst->pipe_path = pipe; inst->area = area;
          inst->sw = sw; inst->sh = sh; inst->enc = enc;
          inst->video = std::make_unique<NanoVideo>(pipe, area, sw, sh, enc);
          inst->creation_time = this->now();
          inst->lifetime = rclcpp::Duration::from_seconds(cfg.value("expire", 0.0));

          videos_[id] = inst;
          if (std::find(video_order_.begin(), video_order_.end(), id) == video_order_.end()) {
            video_order_.push_back(id);
          }
        } else if (typ == "MarkerLayer") {
          std::string topic = cfg.value("topic", "");
          double scale = cfg.value("scale", 1000.0);
          double ox = cfg.value("offset_x", 0.0);
          double oy = cfg.value("offset_y", 0.0);

          auto inst = std::make_shared<MarkInst>();
          inst->id = id; inst->topic = topic; inst->area = area;
          inst->scale = scale; inst->title = cfg.value("title", "");
          inst->layer = std::make_unique<NanoMarkerLayer>(area, scale, ox, oy);
          inst->layer->set_title(inst->title);

          if (cfg.contains("exclude_ns")) {
            std::string ns_str = cfg["exclude_ns"];
            std::vector<std::string> ens;
            std::stringstream ss(ns_str); std::string item;
            while (std::getline(ss, item, ',')) {if (!item.empty()) {ens.push_back(item);}}
            inst->layer->set_excluded_ns(ens);
          }

          inst->creation_time = this->now();
          inst->lifetime = rclcpp::Duration::from_seconds(cfg.value("expire", 0.0));

          if (!topic.empty()) {
            inst->sub = this->create_subscription<visualization_msgs::msg::MarkerArray>(
              topic, 10, [this, id](const visualization_msgs::msg::MarkerArray::SharedPtr mmsg) {
                std::lock_guard<std::mutex> l(mtx_);
                if (markers_.count(id)) {markers_[id]->layer->update_markers(mmsg);}
              });
          }
          markers_[id] = inst;
        }
        changed = true;
      }
      if (changed) {publish_state_impl();}
    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "JSON Error: %s in msg: %s", e.what(), msg->data.c_str());
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
          RCLCPP_INFO(this->get_logger(), "Output FIFO opened: %s", fifo_path_.c_str());
        }
      }

      auto frame_start = std::chrono::steady_clock::now();
      auto frame_duration = std::chrono::milliseconds(static_cast<int>(1000.0 / fps_));
      auto now = this->now();

      {
        std::lock_guard<std::mutex> lock(mtx_);

        // Auto-expiration
        auto expire = [&](auto & map, auto && on_erase) {
            auto it = map.begin();
            while (it != map.end()) {
              if (it->second->lifetime.nanoseconds() > 0 &&
                (now - it->second->creation_time) > it->second->lifetime)
              {
                on_erase(it->first);
                it = map.erase(it);
              } else {++it;}
            }
          };
        expire(terminals_, [](const std::string &) {});
        expire(bitmaps_, [](const std::string &) {});
        expire(
          videos_, [&](const std::string & id) {
            video_order_.erase(
              std::remove(video_order_.begin(), video_order_.end(), id),
              video_order_.end());
          });
        expire(markers_, [](const std::string &) {});

        if (fifo_fd_ >= 0) {
          std::fill(buffer_.begin(), buffer_.end(), 0);
          for (auto const & id : video_order_) {
            if (videos_.count(id)) {
              videos_[id]->video->draw(buffer_, width_, height_);
            }
          }
          for (auto const & [id, m] : markers_) {m->layer->draw(buffer_, width_, height_);}
          for (auto const & [id, b] : bitmaps_) {b->canvas->draw(buffer_, width_, height_);}
          for (auto const & [id, t] : terminals_) {t->terminal->draw(buffer_, width_, height_);}
        }
      }

      // Write to FIFO outside the lock!
      if (fifo_fd_ >= 0) {
        size_t total = buffer_.size();
        size_t written = 0;
        while (written < total && running_) {
          ssize_t w = write(fifo_fd_, buffer_.data() + written, total - written);
          if (w > 0) {
            written += w;
          } else if (w == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              struct pollfd pfd = {fifo_fd_, POLLOUT, 0};
              poll(&pfd, 1, 10);
              continue;
            } else {
              RCLCPP_WARN(this->get_logger(), "FIFO disconnected or error (errno: %d)", errno);
              close(fifo_fd_); fifo_fd_ = -1; break;
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
    Rect area; Color text_color, bg_color; int font_size;
    std::unique_ptr<NanoTerminal> terminal;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub;
    rclcpp::Time creation_time; rclcpp::Duration lifetime{0, 0};
  };
  struct BitInst
  {
    std::string id, topic; Rect area; Color color;
    std::unique_ptr<NanoCanvas> canvas;
    rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr sub_bin;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_hex;
    rclcpp::Time creation_time; rclcpp::Duration lifetime{0, 0};
  };
  struct VideoInst
  {
    std::string id, pipe_path, enc; Rect area; int sw, sh;
    std::unique_ptr<NanoVideo> video;
    rclcpp::Time creation_time; rclcpp::Duration lifetime{0, 0};
  };
  struct MarkInst
  {
    std::string id, topic, title; Rect area; double scale;
    std::unique_ptr<NanoMarkerLayer> layer;
    rclcpp::Subscription<visualization_msgs::msg::MarkerArray>::SharedPtr sub;
    rclcpp::Time creation_time; rclcpp::Duration lifetime{0, 0};
  };

  int width_, height_, queue_size_;
  double fps_;
  std::string fifo_path_;
  int fifo_fd_ = -1;
  std::vector<uint8_t> buffer_;
  std::map<std::string, std::shared_ptr<TermInst>> terminals_;
  std::map<std::string, std::shared_ptr<BitInst>> bitmaps_;
  std::map<std::string, std::shared_ptr<VideoInst>> videos_;
  std::map<std::string, std::shared_ptr<MarkInst>> markers_;
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
