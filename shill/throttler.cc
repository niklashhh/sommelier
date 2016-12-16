//
// Copyright 2016 The Chromium OS Authors. All rights reserved.
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
#include <stdlib.h>

#include <base/memory/weak_ptr.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/minijail/minijail.h>

#include <algorithm>
#include <vector>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/process_manager.h"
#include "shill/throttler.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kTC;
static std::string ObjectID(Throttler* t) {
  return "throttler";
}
}

const char Throttler::kTCPath[] = "/sbin/tc";

const char* const Throttler::kTCCleanUpCmds[] = {
    "qdisc del dev ${INTERFACE} root\n",
    "qdisc del dev ${INTERFACE} ingress\n"};

// For fq_codel quantum 300 gives a boost to interactive flows
// Only works for bandwidths < 50 Mbps.
const char* const Throttler::kTCThrottleUplinkCmds[] = {
    "qdisc add dev ${INTERFACE} root handle 1: htb default 11\n",
    "class add dev ${INTERFACE} parent 1: classid 1:1 htb rate ${ULRATE}\n",
    "class add dev ${INTERFACE} parent 1:1 classid 1:11 htb rate "
    "${ULRATE} prio 0 quantum 300\n"};

const char* const Throttler::kTCThrottleDownlinkCmds[] = {
    "qdisc add dev ${INTERFACE} handle ffff: ingress\n",
    "filter add dev ${INTERFACE} parent ffff: protocol all "
    " prio 50 u32 match ip"
    " src 0.0.0.0/0 police rate ${DLRATE} burst ${BURST}k mtu 66000"
    " drop flowid :1\n"};

const char kTemplateInterface[] = "${INTERFACE}";
const char kTemplateULRate[] = "${ULRATE}";
const char kTemplateDLRate[] = "${DLRATE}";
const char kTemplateBurst[] = "${BURST}";

const char Throttler::kTCUser[] = "nobody";
const char Throttler::kTCGroup[] = "nobody";

Throttler::Throttler(EventDispatcher* dispatcher, Manager* manager)
    : file_io_(FileIO::GetInstance()),
      tc_pid_(0),
      dispatcher_(dispatcher),
      manager_(manager),
      process_manager_(ProcessManager::GetInstance()) {
  SLOG(this, 2) << __func__;
}

Throttler::~Throttler() {
  SLOG(this, 2) << __func__;
}

void Throttler::ClearTCState() {
  tc_pid_ = 0;
  tc_commands_.clear();
  tc_current_interface_.clear();
  tc_interfaces_to_throttle_.clear();
  callback_.Reset();
}

bool Throttler::DisableThrottlingOnAllInterfaces(
    const ResultCallback& callback) {
  bool result = false;

  std::vector<std::string> interfaces = manager_->GetDeviceInterfaceNames();
  std::vector<std::string> commands;

  for (const auto& interface_name : interfaces) {
    for (std::string command : kTCCleanUpCmds) {
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                         interface_name);
      commands.push_back(command);
    }
  }

  callback_ = callback;
  result = StartTCForCommands(commands);
  if (result) {
    ClearThrottleStatus();
  }
  return result;
}

void Throttler::Done(const ResultCallback& callback,
                     Error::Type error_type,
                     const std::string& message) {
  Error error;
  Error::PopulateAndLog(FROM_HERE, &error, error_type, message);
  if (!callback.is_null()) {
    callback.Run(error);
    SLOG(this, 4) << "ran callback";
  } else {
    SLOG(this, 4) << "null callback";
  }
  ClearTCState();
  return;
}

bool Throttler::ThrottleInterfaces(const ResultCallback& callback,
                                   uint32_t upload_rate_kbits,
                                   uint32_t download_rate_kbits) {
  // At least one of upload/download should be throttled.
  // 0 value indicates no throttling.
  if ((upload_rate_kbits == 0) && (download_rate_kbits == 0)) {
    Done(callback, Error::kInvalidArguments,
         "One of download/upload rates should be set");
    return false;
  }

  tc_interfaces_to_throttle_ = manager_->GetDeviceInterfaceNames();

  std::string interface_name = GetNextInterface();

  if (interface_name.empty()) {
    Done(callback, Error::kOperationFailed,
         "No interfaces available for throttling");
    return false;
  }

  // Set state here, OnProcessExited will clear in case of failure
  desired_throttling_enabled_ = true;
  desired_upload_rate_kbits_ = upload_rate_kbits;
  desired_download_rate_kbits_ = download_rate_kbits;

  return Throttle(callback, interface_name, upload_rate_kbits,
                  download_rate_kbits);
}

bool Throttler::Throttle(const ResultCallback& callback,
                         const std::string& interface_name,
                         uint32_t upload_rate_kbits,
                         uint32_t download_rate_kbits) {
  SLOG(this, 4) << __func__ << " : " << interface_name << "("
                << upload_rate_kbits << ", " << download_rate_kbits << ")";

  if (tc_pid_ || !tc_commands_.empty() || !tc_current_interface_.empty()) {
    Done(callback, Error::kWrongState, "Cannot run concurrent TC operations");
    return false;
  }

  std::string throttle_file;
  std::vector<std::string> commands;

  // Easier to clean up first and start afresh than issue tc changes.
  for (std::string command : kTCCleanUpCmds) {
    base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                       interface_name);
    commands.push_back(command);
  }

  // Add commands for upload(egress) queueing disciplines
  // and filters
  if (upload_rate_kbits) {
    for (std::string command : kTCThrottleUplinkCmds) {
      std::string ulrate(base::UintToString(upload_rate_kbits) + "kbit");
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                         interface_name);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateULRate, ulrate);
      commands.push_back(command);
    }
  }

  // Add commands for download(ingress) queueing disciplines
  // and filters
  if (download_rate_kbits) {
    for (std::string command : kTCThrottleDownlinkCmds) {
      std::string dlrate(base::UintToString(download_rate_kbits) + "kbit");
      std::string to_burst(base::UintToString(download_rate_kbits * 2));
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                         interface_name);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateDLRate, dlrate);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateBurst, to_burst);
      commands.push_back(command);
    }
  }
  callback_ = callback;
  tc_current_interface_ = interface_name;
  return StartTCForCommands(commands);
}

bool Throttler::ApplyThrottleToNewInterface(const std::string& interface_name) {
  if (!desired_throttling_enabled_) {
    // Nothing to do if no throttling is desired
    return false;
  }
  // An operation is currently in progress, append to list of interfaces
  if (tc_pid_ != 0) {
    tc_interfaces_to_throttle_.push_back(interface_name);
    return true;
  }
  // No operation currently in progress, start a new tc process
  ResultCallback dummy;
  return Throttle(dummy, interface_name, desired_upload_rate_kbits_,
                  desired_download_rate_kbits_);
}

bool Throttler::StartTCForCommands(const std::vector<std::string>& commands) {
  CHECK_EQ(tc_pid_, 0);
  std::vector<std::string> args = {
      "-f",  // Continue if there is a failure or no-op
      "-b",  // Batch mode
      "-"    // Use stdin for input
  };

  uint64_t capmask = CAP_TO_MASK(CAP_NET_ADMIN);

  tc_commands_ = commands;
  tc_pid_ = process_manager_->StartProcessInMinijailWithPipes(
      FROM_HERE, base::FilePath(kTCPath), args, kTCUser, kTCGroup, capmask,
      base::Bind(&Throttler::OnProcessExited, AsWeakPtr()), &tc_stdin_, nullptr,
      // shill's stderr is wired to syslog, so nullptr
      // here implies throttling errors show up in /var/log/net.log.
      nullptr);

  LOG(ERROR) << "Spawned tc with pid: " << tc_pid_;

  if (file_io_->SetFdNonBlocking(tc_stdin_)) {
    Done(callback_, Error::kOperationFailed,
         "Unable to set TC pipes to be non-blocking");
    return false;
  }
  tc_stdin_handler_.reset(dispatcher_->CreateReadyHandler(
      tc_stdin_, IOHandler::kModeOutput,
      Bind(&Throttler::WriteTCCommands, AsWeakPtr())));
  return true;
}

void Throttler::WriteTCCommands(int fd) {
  CHECK_EQ(fd, tc_stdin_);
  CHECK(tc_pid_);

  for (const auto& command : tc_commands_) {
    SLOG(this, 2) << "Issuing tc command: " << command;

    ssize_t bytes_written =
        file_io_->Write(tc_stdin_, command.data(), command.size());
    if (bytes_written != static_cast<ssize_t>(command.size())) {
      LOG(ERROR) << "Bytes written: " << bytes_written
                 << "v/s Command size: " << command.size();
    }
  }

  tc_stdin_handler_.reset();
  file_io_->Close(tc_stdin_);
  tc_stdin_ = -1;
  return;
}

void Throttler::ClearThrottleStatus() {
  desired_throttling_enabled_ = false;
  desired_upload_rate_kbits_ = 0;
  desired_download_rate_kbits_ = 0;
}

std::string Throttler::GetNextInterface() {
  std::string interface_name;
  if (!tc_interfaces_to_throttle_.empty()) {
    interface_name = tc_interfaces_to_throttle_.back();
    tc_interfaces_to_throttle_.pop_back();
  }
  return interface_name;
}

void Throttler::OnProcessExited(int exit_status) {
  CHECK(tc_pid_);
  CHECK(!tc_commands_.empty());
  // Should keep track of interface names if throttling, but not if disabling
  CHECK(!desired_throttling_enabled_ || !tc_current_interface_.empty());

  Error::Type error_type =
      (exit_status == EXIT_SUCCESS) ? Error::kSuccess : Error::kOperationFailed;

  std::string message =
      ((desired_throttling_enabled_) ? "throttling " : "disabling throttle ") +
      ((exit_status == EXIT_SUCCESS)
           ? std::string("succeeded")
           : (std::string("failed: ") + base::IntToString(exit_status)));

  Error error;
  Error::PopulateAndLog(FROM_HERE, &error, error_type, message);

  std::string next_interface = GetNextInterface();

  if (next_interface.empty()) {
    Done(callback_, Error::kSuccess, "");
  } else {
    SLOG(this, 2) << "Done with " << tc_current_interface_ << " now calling "
                  << next_interface;
    tc_pid_ = 0;
    tc_commands_.clear();
    tc_current_interface_.clear();
    Throttle(callback_, next_interface, desired_upload_rate_kbits_,
             desired_download_rate_kbits_);
  }
}

}  // namespace shill