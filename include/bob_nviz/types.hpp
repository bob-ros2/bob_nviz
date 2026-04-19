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

#ifndef BOB_NVIZ__TYPES_HPP_
#define BOB_NVIZ__TYPES_HPP_

#include <cstdint>
#include <string>

namespace bob_nviz
{

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

}  // namespace bob_nviz

#endif  // BOB_NVIZ__TYPES_HPP_
