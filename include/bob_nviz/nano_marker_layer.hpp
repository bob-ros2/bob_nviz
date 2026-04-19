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

#ifndef BOB_NVIZ__NANO_MARKER_LAYER_HPP_
#define BOB_NVIZ__NANO_MARKER_LAYER_HPP_

#include <mutex>
#include <string>
#include <vector>
#include "visualization_msgs/msg/marker_array.hpp"
#include "bob_nviz/types.hpp"

namespace bob_nviz
{

class NanoMarkerLayer
{
public:
  NanoMarkerLayer(Rect area, double scale, double offset_x, double offset_y);

  void update_markers(const visualization_msgs::msg::MarkerArray::SharedPtr msg);
  void draw(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h);

  void set_area(Rect area) {area_ = area;}
  void set_scale(double scale) {scale_ = scale;}
  void set_offset(double x, double y) {offset_x_ = x; offset_y_ = y;}
  void set_excluded_ns(const std::vector<std::string> & ns) {excluded_ns_ = ns;}
  void set_title(const std::string & title) {title_ = title;}

private:
  Rect area_;
  double scale_;
  double offset_x_, offset_y_;
  std::vector<std::string> excluded_ns_;
  std::string title_;
  visualization_msgs::msg::MarkerArray::SharedPtr last_markers_;
  std::mutex mutex_;
};

}  // namespace bob_nviz

#endif  // BOB_NVIZ__NANO_MARKER_LAYER_HPP_
