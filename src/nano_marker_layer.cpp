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

#include <algorithm>

#include "bob_nviz/nano_marker_layer.hpp"
#include "bob_nviz/render_utils.hpp"

namespace bob_nviz
{

NanoMarkerLayer::NanoMarkerLayer(Rect area, double scale, double offset_x, double offset_y)
: area_(area), scale_(scale), offset_x_(offset_x), offset_y_(offset_y)
{
}

void NanoMarkerLayer::update_markers(const visualization_msgs::msg::MarkerArray::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  // Create a local copy to sort
  auto sorted_msg = std::make_shared<visualization_msgs::msg::MarkerArray>(*msg);
  std::sort(
    sorted_msg->markers.begin(),
    sorted_msg->markers.end(),
    [](const visualization_msgs::msg::Marker & a,
    const visualization_msgs::msg::Marker & b) {
      return a.pose.position.x > b.pose.position.x;
    });
  last_markers_ = sorted_msg;
}

void NanoMarkerLayer::draw(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!last_markers_ || last_markers_->markers.empty()) {return;}

  Rect content_area = area_;
  // Draw Title Bar if needed (similar to NanoTerminal)
  if (!title_.empty()) {
    int th = 8;  // Small scale title
    Rect title_rect = {area_.x, area_.y, area_.w, th + 6};
    fill_rect(buffer, buffer_w, buffer_h, title_rect, {0, 0, 0, 128});

    int dist = static_cast<int>(title_.length()) * 8;
    int tx = area_.x + (area_.w - dist) / 2;
    for (unsigned char c : title_) {
      draw_char(buffer, buffer_w, buffer_h, c, tx, area_.y + 3, {255, 255, 255, 255}, 1);
      tx += 8;
    }
    content_area.y += th + 6;
    content_area.h -= th + 6;
  }

  for (const auto & marker : last_markers_->markers) {
    if (!excluded_ns_.empty() &&
      std::find(excluded_ns_.begin(), excluded_ns_.end(), marker.ns) != excluded_ns_.end())
    {
      continue;
    }

    Color col = {
      static_cast<uint8_t>(marker.color.r * 255),
      static_cast<uint8_t>(marker.color.g * 255),
      static_cast<uint8_t>(marker.color.b * 255),
      static_cast<uint8_t>(marker.color.a * 255)
    };

    switch (marker.type) {
      case visualization_msgs::msg::Marker::LINE_STRIP:
      case visualization_msgs::msg::Marker::LINE_LIST: {
          if (marker.points.size() < 2) {break;}
          bool is_list = (marker.type == visualization_msgs::msg::Marker::LINE_LIST);
          size_t step = is_list ? 2 : 1;

          for (size_t i = 0; i < marker.points.size() - 1; i += step) {
            const auto & p1 = marker.points[i];
            const auto & p2 = marker.points[i + 1];

            // Project YZ plane (front view) or XY plane (top view)?
            // Bob's sdlviz uses Y/Z. Let's stick to that for consistency.
            int x1 = content_area.x + content_area.w / 2 + (marker.pose.position.y + p1.y) *
              scale_ + offset_x_;
            int y1 = content_area.y + content_area.h / 2 - (marker.pose.position.z + p1.z) *
              scale_ + offset_y_;
            int x2 = content_area.x + content_area.w / 2 + (marker.pose.position.y + p2.y) *
              scale_ + offset_x_;
            int y2 = content_area.y + content_area.h / 2 - (marker.pose.position.z + p2.z) *
              scale_ + offset_y_;

            draw_line(buffer, buffer_w, buffer_h, x1, y1, x2, y2, col);
          }
        } break;

      case visualization_msgs::msg::Marker::SPHERE:
      case visualization_msgs::msg::Marker::CYLINDER: {
          int cx = content_area.x + content_area.w / 2 + marker.pose.position.y * scale_ +
            offset_x_;
          int cy = content_area.y + content_area.h / 2 - marker.pose.position.z * scale_ +
            offset_y_;
          int radius = static_cast<int>((marker.scale.y * scale_) / 2.0);
          if (radius < 1) {radius = 1;}
          draw_filled_circle(buffer, buffer_w, buffer_h, cx, cy, radius, col);
        } break;

      case visualization_msgs::msg::Marker::POINTS: {
          for (const auto & p : marker.points) {
            int px = content_area.x + content_area.w / 2 + (marker.pose.position.y + p.y) * scale_ +
              offset_x_;
            int py = content_area.y + content_area.h / 2 - (marker.pose.position.z + p.z) * scale_ +
              offset_y_;
            // Just a single pixel or a small box? Small box is better visible.
            Rect pixel_rect = {px - 1, py - 1, 2, 2};
            fill_rect(buffer, buffer_w, buffer_h, pixel_rect, col);
          }
        } break;
    }
  }
}

}  // namespace bob_nviz
