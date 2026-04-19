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

#ifndef BOB_NVIZ__NANO_TERMINAL_HPP_
#define BOB_NVIZ__NANO_TERMINAL_HPP_

#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "bob_nviz/types.hpp"

namespace bob_nviz
{

class NanoTerminal
{
public:
  NanoTerminal(
    Rect area, Color text_color, Color bg_color, int scale, size_t line_limit,
    Alignment align, std::string title_in = "", int columns = 0,
    TerminalMode mode = MODE_DEFAULT);

  void clear();
  void append(const std::string & text);
  void draw(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h);

  void set_area(Rect area);
  void set_colors(Color text, Color bg);
  void set_scale(int scale);
  void set_align(Alignment align);
  void set_columns(int cols);
  void set_mode(TerminalMode mode);
  void set_title(const std::string & title);

  TerminalMode get_mode() const {return mode_;}

private:
  void update_wrap_width();
  void process_char(uint8_t c);
  void commit_word();

  Rect area_;
  Color text_color_, bg_color_;
  int scale_;
  Alignment align_;
  int columns_override_;
  size_t line_limit_, wrap_width_;
  std::string title_, current_word_;
  std::deque<std::string> lines_;
  std::mutex mutex_;
  TerminalMode mode_;
  uint8_t utf8_lead_;
};

}  // namespace bob_nviz

#endif  // BOB_NVIZ__NANO_TERMINAL_HPP_
