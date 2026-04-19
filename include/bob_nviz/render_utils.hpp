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

#ifndef BOB_NVIZ__RENDER_UTILS_HPP_
#define BOB_NVIZ__RENDER_UTILS_HPP_

#include <vector>
#include "bob_nviz/types.hpp"

namespace bob_nviz
{

/**
 * @brief Draws a single character using font8x8 data.
 */
void draw_char(
  std::vector<uint8_t> & buffer, int buffer_w, int buffer_h,
  uint8_t c, int sx, int sy, Color col, int scale);

/**
 * @brief Draws a line using Bresenham's algorithm.
 */
void draw_line(
  std::vector<uint8_t> & buffer, int buffer_w, int buffer_h,
  int x1, int y1, int x2, int y2, Color col);

/**
 * @brief Draws a filled circle using the midpoint circle algorithm.
 */
void draw_filled_circle(
  std::vector<uint8_t> & buffer, int buffer_w, int buffer_h,
  int cx, int cy, int radius, Color col);

/**
 * @brief Fills a rectangular area with a color (supports alpha blending).
 */
void fill_rect(
  std::vector<uint8_t> & buffer, int buffer_w, int buffer_h,
  Rect area, Color col);

}  // namespace bob_nviz

#endif  // BOB_NVIZ__RENDER_UTILS_HPP_
