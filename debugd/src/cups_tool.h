// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_CUPS_TOOL_H_
#define DEBUGD_SRC_CUPS_TOOL_H_

#include <string>

#include <base/macros.h>
#include <dbus-c++/dbus.h>

namespace debugd {

class CupsTool {
 public:
  CupsTool() = default;
  ~CupsTool() = default;

  // Add a printer to CUPS using lpadmin.
  bool AddPrinter(const std::string& name,
                  const std::string& uri,
                  const std::string& ppd_path,
                  bool ipp_everywhere,
                  DBus::Error* error);

  // Remove a printer from CUPS using lpadmin.
  bool RemovePrinter(const std::string& name, DBus::Error* error);

  // Clear CUPS state.
  void ResetState(DBus::Error* error);

 private:
  DISALLOW_COPY_AND_ASSIGN(CupsTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CUPS_TOOL_H_
