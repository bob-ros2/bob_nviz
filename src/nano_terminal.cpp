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
#include <deque>
#include <string>
#include <vector>

#include "bob_nviz/nano_terminal.hpp"
#include "bob_nviz/render_utils.hpp"

namespace bob_nviz
{

NanoTerminal::NanoTerminal(
  Rect area, Color text_color, Color bg_color, int scale, size_t line_limit,
  Alignment align, std::string title_in, int columns, TerminalMode mode)
: area_(area), text_color_(text_color), bg_color_(bg_color), scale_(scale), align_(align),
  columns_override_(columns), line_limit_(line_limit), mode_(mode), utf8_lead_(0)
{
  lines_.push_back("");
  update_wrap_width();
  if (!title_in.empty()) {
    set_title(title_in);
  }
}

void NanoTerminal::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);
  lines_.clear();
  lines_.push_back("");
  current_word_.clear();
}

void NanoTerminal::commit_word()
{
  if (current_word_.empty()) {return;}
  if (lines_.back().length() + current_word_.length() > wrap_width_) {
    lines_.push_back("");
  }
  lines_.back() += current_word_;
  current_word_.clear();
}

void NanoTerminal::append(const std::string & text)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == MODE_CLEAR_ON_NEW && !text.empty()) {
    lines_.clear();
    lines_.push_back("");
  }
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
      process_char('\n');
      i++;
    } else {
      unsigned char c = text[i];
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
  }
  if (mode_ == MODE_APPEND_NEWLINE && !text.empty()) {
    process_char('\n');
  }
  while (lines_.size() > line_limit_) {
    lines_.pop_front();
  }
}

void NanoTerminal::set_title(const std::string & title)
{
  std::lock_guard<std::mutex> lock(mutex_);
  title_ = "";
  uint8_t lead = 0;
  for (unsigned char c : title) {
    if (lead == 0) {
      if (c == 0xC2 || c == 0xC3) {
        lead = c; continue;
      }
      if (c >= 32) {title_ += static_cast<char>(c);}
    } else {
      uint8_t d = (lead == 0xC2) ? c : (c + 0x40);
      if (d >= 32) {title_ += static_cast<char>(d);}
      lead = 0;
    }
  }
}

void NanoTerminal::draw(std::vector<uint8_t> & buffer, int buffer_w, int buffer_h)
{
  std::lock_guard<std::mutex> lock(mutex_);

  // Draw Background
  fill_rect(buffer, buffer_w, buffer_h, area_, bg_color_);

  int cur_y = area_.y;
  // Draw Title Bar
  if (!title_.empty()) {
    int th = 8 * scale_ + 6;
    Rect title_rect = {area_.x, area_.y, area_.w, th};
    Color title_bg = {0, 0, 0, 128};  // Semi-transparent black
    fill_rect(buffer, buffer_w, buffer_h, title_rect, title_bg);

    int dist = static_cast<int>(title_.length()) * 8 * scale_;
    int tx = area_.x + (area_.w - dist) / 2;
    for (unsigned char c : title_) {
      draw_char(buffer, buffer_w, buffer_h, c, tx, area_.y + 3, {255, 255, 255, 255}, scale_);
      tx += 8 * scale_;
    }
    cur_y += th + 4;
  } else {
    cur_y += 4;
  }

  // Draw Main Text
  int ch = 8 * scale_, cw = 8 * scale_, ls = 2;
  int fit = (area_.y + area_.h - cur_y - 4) / (ch + ls);
  if (fit < 1) {fit = 1;}

  std::deque<std::string> draw_lines = lines_;
  if (!current_word_.empty()) {
    if (draw_lines.back().length() + current_word_.length() > wrap_width_) {
      draw_lines.push_back(current_word_);
    } else {
      draw_lines.back() += current_word_;
    }
  }

  size_t start =
    (draw_lines.size() >
    static_cast<size_t>(fit)) ? (draw_lines.size() - static_cast<size_t>(fit)) : 0;

  for (size_t i = start; i < draw_lines.size(); ++i) {
    int cur_x = area_.x + 4;
    if (align_ == ALIGN_RIGHT) {
      cur_x = area_.x + area_.w - static_cast<int>(draw_lines[i].length()) * cw - 4;
    } else if (align_ == ALIGN_CENTER) {
      cur_x = area_.x + (area_.w - static_cast<int>(draw_lines[i].length()) * cw) / 2;
    }
    for (unsigned char c : draw_lines[i]) {
      draw_char(buffer, buffer_w, buffer_h, c, cur_x, cur_y, text_color_, scale_);
      cur_x += cw;
    }
    cur_y += ch + ls;
  }
}

void NanoTerminal::set_area(Rect area)
{
  std::lock_guard<std::mutex> lock(mutex_);
  area_ = area;
  update_wrap_width();
}

void NanoTerminal::set_colors(Color text, Color bg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  text_color_ = text;
  bg_color_ = bg;
}

void NanoTerminal::set_scale(int scale)
{
  std::lock_guard<std::mutex> lock(mutex_);
  scale_ = scale;
  update_wrap_width();
}

void NanoTerminal::set_align(Alignment align)
{
  std::lock_guard<std::mutex> lock(mutex_);
  align_ = align;
}

void NanoTerminal::set_columns(int cols)
{
  std::lock_guard<std::mutex> lock(mutex_);
  columns_override_ = cols;
  update_wrap_width();
}

void NanoTerminal::set_mode(TerminalMode mode)
{
  std::lock_guard<std::mutex> lock(mutex_);
  mode_ = mode;
}

void NanoTerminal::update_wrap_width()
{
  if (columns_override_ > 0) {
    wrap_width_ = static_cast<size_t>(columns_override_);
  } else {
    wrap_width_ = static_cast<size_t>((area_.w - 10) / (8 * scale_));
  }
  if (wrap_width_ < 1) {wrap_width_ = 1;}
}

void NanoTerminal::process_char(uint8_t c)
{
  if (c == '\n') {
    commit_word();
    lines_.push_back("");
  } else if (c == ' ' || c == '\t') {
    commit_word();
    if (!lines_.back().empty() && lines_.back().length() < wrap_width_) {
      lines_.back() += ' ';
    }
  } else if (c >= 32) {
    current_word_ += static_cast<char>(c);
    if (current_word_.length() >= wrap_width_) {
      commit_word();
    }
  }
}

}  // namespace bob_nviz
