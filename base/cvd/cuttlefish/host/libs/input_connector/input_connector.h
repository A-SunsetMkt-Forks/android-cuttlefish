/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

struct MultitouchSlot {
  int32_t id;
  int32_t x;
  int32_t y;
};

// The InputConnector encapsulates the components required to interact with the
// Android VM. In order to send input events to the guest an EventSink must be
// instantiated. The event sink should be destroyed when it is known no more
// events will be delivered through it. Multiple event sinks can exist at the
// same time and used concurrently.
class InputConnector {
 public:
  class EventSink {
   public:
    virtual ~EventSink() = default;
    virtual Result<void> SendMouseMoveEvent(int x, int y) = 0;
    virtual Result<void> SendMouseButtonEvent(int button, bool down) = 0;
    virtual Result<void> SendMouseWheelEvent(int pixels) = 0;
    virtual Result<void> SendTouchEvent(const std::string& display, int x,
                                        int y, bool down) = 0;
    virtual Result<void> SendMultiTouchEvent(
        const std::string& device_label,
        const std::vector<MultitouchSlot>& slots, bool down) = 0;
    virtual Result<void> SendKeyboardEvent(uint16_t code, bool down) = 0;
    virtual Result<void> SendRotaryEvent(int pixels) = 0;
    virtual Result<void> SendSwitchesEvent(uint16_t code, bool state) = 0;
  };

  virtual ~InputConnector() = default;

  virtual std::unique_ptr<EventSink> CreateSink() = 0;
};

class InputConnectorImpl;

class InputConnectorBuilder {
 public:
  explicit InputConnectorBuilder();
  ~InputConnectorBuilder();
  InputConnectorBuilder(const InputConnectorBuilder&) = delete;
  InputConnectorBuilder(InputConnectorBuilder&&) = delete;
  InputConnectorBuilder& operator=(const InputConnectorBuilder&) = delete;

  void WithMultitouchDevice(const std::string& device_label, SharedFD conn);
  void WithTouchDevice(const std::string& device_label, SharedFD conn);
  void WithKeyboard(SharedFD conn);
  void WithSwitches(SharedFD conn);
  void WithRotary(SharedFD conn);
  void WithMouse(SharedFD conn);
  // This object becomes invalid after calling Build(), the rvalue reference
  // makes it explicit that it shouldn't be used after.
  std::unique_ptr<InputConnector> Build() &&;

 private:
  std::unique_ptr<InputConnectorImpl> connector_;
};

}  // namespace cuttlefish
