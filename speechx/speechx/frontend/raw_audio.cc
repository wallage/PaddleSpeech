// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
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

#include "frontend/raw_audio.h"
#include "kaldi/base/timer.h"

namespace ppspeech {

using kaldi::BaseFloat;
using kaldi::VectorBase;
using kaldi::Vector;

RawAudioCache::RawAudioCache(int buffer_size)
    : finished_(false), data_length_(0), start_(0), timeout_(1) {
    ring_buffer_.resize(buffer_size);
}

void RawAudioCache::Accept(const VectorBase<BaseFloat>& waves) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (data_length_ + waves.Dim() > ring_buffer_.size()) {
        ready_feed_condition_.wait(lock);
    }
    for (size_t idx = 0; idx < waves.Dim(); ++idx) {
        int32 buffer_idx = (idx + start_) % ring_buffer_.size();
        ring_buffer_[buffer_idx] = waves(idx);
    }
    data_length_ += waves.Dim();
}

bool RawAudioCache::Read(Vector<BaseFloat>* waves) {
    size_t chunk_size = waves->Dim();
    kaldi::Timer timer;
    std::unique_lock<std::mutex> lock(mutex_);
    while (chunk_size > data_length_) {
        // when audio is empty and no more data feed
        // ready_read_condition will block in dead lock. so replace with
        // timeout_
        // ready_read_condition_.wait(lock);
        int32 elapsed = static_cast<int32>(timer.Elapsed() * 1000);
        if (elapsed > timeout_) {
            if (finished_ == true) {  // read last chunk data
                break;
            }
            if (chunk_size > data_length_) {
                return false;
            }
        }
        usleep(100);  // sleep 0.1 ms
    }

    // read last chunk data
    if (chunk_size > data_length_) {
        chunk_size = data_length_;
        waves->Resize(chunk_size);
    }

    for (size_t idx = 0; idx < chunk_size; ++idx) {
        int buff_idx = (start_ + idx) % ring_buffer_.size();
        waves->Data()[idx] = ring_buffer_[buff_idx];
    }
    data_length_ -= chunk_size;
    start_ = (start_ + chunk_size) % ring_buffer_.size();
    ready_feed_condition_.notify_one();
    return true;
}

}  // namespace ppspeech
