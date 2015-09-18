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

#ifndef SHILL_SHIMS_TASK_PROXY_H_
#define SHILL_SHIMS_TASK_PROXY_H_

#include <map>
#include <string>

#include <base/macros.h>
#include <shill/dbus_proxies/org.chromium.flimflam.Task.h>

namespace shill {

namespace shims {

class TaskProxy {
 public:
  TaskProxy(DBus::Connection* connection,
            const std::string& path,
            const std::string& service);
  ~TaskProxy();

  void Notify(const std::string& reason,
              const std::map<std::string, std::string>& dict);

  bool GetSecret(std::string* username, std::string* password);

 private:
  class Proxy : public org::chromium::flimflam::Task_proxy,
                public DBus::ObjectProxy {
   public:
    Proxy(DBus::Connection* connection,
          const std::string& path,
          const std::string& service);
    virtual ~Proxy();

   private:
    // Signal callbacks inherited from Task_proxy.
    // [None]

    // Method callbacks inherited from Task_proxy.
    // [None]

    DISALLOW_COPY_AND_ASSIGN(Proxy);
  };

  Proxy proxy_;

  DISALLOW_COPY_AND_ASSIGN(TaskProxy);
};

}  // namespace shims

}  // namespace shill

#endif  // SHILL_SHIMS_TASK_PROXY_H_