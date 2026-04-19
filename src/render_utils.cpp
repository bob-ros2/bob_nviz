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
#include <cmath>

#include "bob_nviz/render_utils.hpp"
#include "bob_nviz/font8x8.h"

namespace bob_nviz
{

static inline void blend_pixel(std::vector<uint8_t> & b, int bw, int bh, int x, int y, Color col)
{
  if (x >= 0 && x < bw && y >= 0 && y < bh) {
    int idx = (y * bw + x) * 4;
    if (col.a == 255) {
      b[idx] = col.b;
      b[idx + 1] = col.g;
      b[idx + 2] = col.r;
      b[idx + 3] = 255;
    } else if (col.a > 0) {
      uint32_t a = col.a;
      uint32_t na = 255 - a;
      b[idx] =
        static_cast<uint8_t>((static_cast<uint32_t>(b[idx]) * na + static_cast<uint32_t>(col.b) *
        a) >> 8);
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

void draw_char(
  std::vector<uint8_t> & buffer, int buffer_w, int buffer_h, uint8_t c, int sx, int sy,
  Color col, int scale)
{
  const uint8_t * bm = font8x8_data[c];
  for (int r = 0; r < 8; ++r) {
    for (int cl = 0; cl < 8; ++cl) {
      if (bm[r] & (1 << (7 - cl))) {
        for (int py = 0; py < scale; ++py) {
          for (int px = 0; px < scale; ++px) {
            blend_pixel(buffer, buffer_w, buffer_h, sx + cl * scale + px, sy + r * scale + py, col);
          }
        }
      }
    }
  }
}

void draw_line(
  std::vector<uint8_t> & buffer, int buffer_w, int buffer_h, int x1, int y1, int x2,
  int y2, Color col)
{
  int dx = std::abs(x2 - x1);
  int dy = std::abs(y2 - y1);
  int sx = (x1 < x2) ? 1 : -1;
  int sy = (y1 < y2) ? 1 : -1;
  int err = dx - dy;

  while (true) {
    blend_pixel(buffer, buffer_w, buffer_h, x1, y1, col);
    if (x1 == x2 && y1 == y2) {break;}
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x1 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y1 += sy;
    }
  }
}

void draw_filled_circle(
  std::vector<uint8_t> & buffer, int buffer_w, int buffer_h, int cx, int cy,
  int radius, Color col)
{
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        blend_pixel(buffer, buffer_w, buffer_h, cx + x, cy + y, col);
      }
    }
  }
}

void fill_rect(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h, Rect area, Color col)
{
  for (int y = area.y; y < area.y + area.h; ++y) {
    for (int x = area.x; x < area.x + area.w; ++x) {
      blend_pixel(buffer, buffer_w, buffer_h, x, y, col);
    }
  }
}

}  // namespace bob_nviz
