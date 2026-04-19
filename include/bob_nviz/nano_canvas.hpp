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

#ifndef BOB_NVIZ__NANO_CANVAS_HPP_
#define BOB_NVIZ__NANO_CANVAS_HPP_

#include <mutex>
#include <vector>
#include "bob_nviz/types.hpp"

namespace bob_nviz
{

class NanoCanvas
{
public:
  NanoCanvas(Rect area, Color fg, int depth);
  void update_data(const std::vector<uint8_t> & d);
  void draw(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h);

  void set_area(Rect a);
  void set_color(Color fg);

private:
  Rect area_;
  Color fg_;
  int depth_;
  std::vector<uint8_t> data_;
  std::mutex mutex_;
};

}  // namespace bob_nviz

#endif  // BOB_NVIZ__NANO_CANVAS_HPP_
