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

#include "bob_nviz/nano_video.hpp"
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <iostream>

namespace bob_nviz
{

NanoVideo::NanoVideo(
  const std::string & pipe_path, Rect area, int src_w, int src_h,
  const std::string & encoding)
: pipe_path_(pipe_path), area_(area), src_w_(src_w), src_h_(src_h), encoding_(encoding),
  running_(true), frame_ready_(false)
{
  frame_buffer_.resize(src_w_ * src_h_ * 3, 0);
  worker_ = std::thread(&NanoVideo::pipe_reader, this);
}

NanoVideo::~NanoVideo()
{
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
}

void NanoVideo::pipe_reader()
{
  int fd = -1;
  while (running_) {
    if (fd == -1) {
      fd = open(pipe_path_.c_str(), O_RDONLY | O_NONBLOCK);
      if (fd == -1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }
    }

    size_t frame_size = src_w_ * src_h_ * 3;
    std::vector<uint8_t> tmp(frame_size);
    size_t read_bytes = 0;
    bool ok = true;
    while (read_bytes < tmp.size() && running_) {
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLIN;
      int ret = poll(&pfd, 1, 100);  // Efficiently wait up to 100ms

      if (ret < 0) {
        if (errno == EINTR) {continue;}
        close(fd); fd = -1; ok = false; break;
      }
      if (ret == 0) {continue;}

      ssize_t n = read(fd, tmp.data() + read_bytes, tmp.size() - read_bytes);
      if (n > 0) {
        read_bytes += n;
      } else if (n == 0) {
        close(fd); fd = -1; ok = false; break;
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {continue;}
        close(fd); fd = -1; ok = false; break;
      }
    }

    if (ok && read_bytes == tmp.size()) {
      std::lock_guard<std::mutex> lock(mutex_);
      frame_buffer_ = std::move(tmp);
      frame_ready_ = true;
    }
  }
  if (fd != -1) {close(fd);}
}

void NanoVideo::draw(std::vector<uint8_t> & buffer, int width, int height)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!frame_ready_ || frame_buffer_.empty()) {return;}

  bool swap = (encoding_ == "bgr");

  for (int y = 0; y < src_h_; ++y) {
    int dy = area_.y + y;
    if (dy < 0 || dy >= height) {continue;}

    int start_x_in_src = 0;
    int render_x = area_.x;
    int current_row_w = src_w_;

    if (render_x < 0) {
      start_x_in_src = -render_x;
      current_row_w += render_x;
      render_x = 0;
    }
    if (render_x + current_row_w > width) {
      current_row_w = width - render_x;
    }
    if (current_row_w <= 0) {continue;}

    size_t src_row_off = static_cast<size_t>(y) * src_w_ * 3;
    size_t dst_row_off = (static_cast<size_t>(dy) * width + render_x) * 4;

    for (int x = 0; x < current_row_w; ++x) {
      size_t s = src_row_off + static_cast<size_t>(start_x_in_src + x) * 3;
      size_t d = dst_row_off + static_cast<size_t>(x) * 4;

      if (swap) {
        buffer[d] = frame_buffer_[s + 2];
        buffer[d + 1] = frame_buffer_[s + 1];
        buffer[d + 2] = frame_buffer_[s];
        buffer[d + 3] = 255;
      } else {
        buffer[d] = frame_buffer_[s];
        buffer[d + 1] = frame_buffer_[s + 1];
        buffer[d + 2] = frame_buffer_[s + 2];
        buffer[d + 3] = 255;
      }
    }
  }
}

}  // namespace bob_nviz
