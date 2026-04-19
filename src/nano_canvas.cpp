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

#include "bob_nviz/nano_canvas.hpp"
#include <algorithm>

namespace bob_nviz
{

NanoCanvas::NanoCanvas(Rect area, Color fg, int depth)
: area_(area), fg_(fg), depth_(depth)
{
  data_.resize((depth == 8) ? (area.w * area.h) : ((area.w * area.h + 7) / 8), 0);
}

void NanoCanvas::update_data(const std::vector<uint8_t> & d)
{
  std::lock_guard<std::mutex> lock(mutex_);
  size_t sz = (depth_ == 8) ? (area_.w * area_.h) : ((area_.w * area_.h + 7) / 8);
  if (d.size() >= sz) {
    std::copy(d.begin(), d.begin() + sz, data_.begin());
  }
}

void NanoCanvas::draw(std::vector<uint8_t> & b, int bw, int bh)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (data_.empty()) {return;}

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

void NanoCanvas::set_area(Rect a)
{
  std::lock_guard<std::mutex> lock(mutex_);
  area_ = a;
}

void NanoCanvas::set_color(Color fg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  fg_ = fg;
}

}  // namespace bob_nviz
