//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "shill/mock_device.h"

#include <string>

#include <base/memory/ref_counted.h>
#include <gmock/gmock.h>

namespace shill {

class ControlInterface;
class EventDispatcher;

using ::testing::_;
using ::testing::DefaultValue;
using std::string;

MockDevice::MockDevice(ControlInterface* control_interface,
                       EventDispatcher* dispatcher,
                       Metrics* metrics,
                       Manager* manager,
                       const string& link_name,
                       const string& address,
                       int interface_index)
    : Device(control_interface,
             dispatcher,
             metrics,
             manager,
             link_name,
             address,
             interface_index,
             Technology::kUnknown) {
  DefaultValue<Technology::Identifier>::Set(Technology::kUnknown);
  ON_CALL(*this, connection())
      .WillByDefault(testing::ReturnRef(Device::connection()));
}

MockDevice::~MockDevice() {}

}  // namespace shill
