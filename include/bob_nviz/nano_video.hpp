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

#ifndef BOB_NVIZ__NANO_VIDEO_HPP_
#define BOB_NVIZ__NANO_VIDEO_HPP_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "bob_nviz/types.hpp"

namespace bob_nviz
{

class NanoVideo
{
public:
  NanoVideo(
    const std::string & pipe_path, Rect area, int src_w, int src_h,
    const std::string & encoding = "rgb");
  ~NanoVideo();

  void draw(std::vector<uint8_t> & buffer, int width, int height);
  void set_area(Rect area) {area_ = area;}

private:
  void pipe_reader();

  std::string pipe_path_;
  Rect area_;
  int src_w_, src_h_;
  std::string encoding_;
  std::vector<uint8_t> frame_buffer_;
  std::mutex mutex_;
  std::thread worker_;
  std::atomic<bool> running_;
  std::atomic<bool> frame_ready_;
};

}  // namespace bob_nviz

#endif  // BOB_NVIZ__NANO_VIDEO_HPP_
