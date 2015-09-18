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

#include "shill/diagnostics_reporter.h"

#include <vector>

#include <base/files/file_util.h>
#include <chromeos/minijail/minijail.h>

#include "shill/net/shill_time.h"
#include "shill/process_killer.h"
#include "shill/shims/net_diags_upload.h"

using base::Closure;
using std::vector;

namespace shill {

namespace {

base::LazyInstance<DiagnosticsReporter> g_reporter = LAZY_INSTANCE_INITIALIZER;
const char kNetDiagsUpload[] = SHIMDIR "/net-diags-upload";
const char kNetDiagsUploadUser[] = "syslog";

}  // namespace

// static
const int DiagnosticsReporter::kLogStashThrottleSeconds = 30 * 60;

DiagnosticsReporter::DiagnosticsReporter()
    : minijail_(chromeos::Minijail::GetInstance()),
      process_killer_(ProcessKiller::GetInstance()),
      time_(Time::GetInstance()),
      last_log_stash_(0),
      stashed_net_log_(shims::kStashedNetLog) {}

DiagnosticsReporter::~DiagnosticsReporter() {}

// static
DiagnosticsReporter* DiagnosticsReporter::GetInstance() {
  return g_reporter.Pointer();
}

void DiagnosticsReporter::OnConnectivityEvent() {
  LOG(INFO) << "Diagnostics event triggered.";

  struct timeval now{};
  time_->GetTimeMonotonic(&now);
  if (last_log_stash_ &&
      last_log_stash_ + kLogStashThrottleSeconds >
      static_cast<uint64_t>(now.tv_sec)) {
    LOG(INFO) << "Diagnostics throttled.";
    return;
  }

  last_log_stash_ = now.tv_sec;
  // Delete logs possibly stashed by a different user.
  base::DeleteFile(stashed_net_log_, false);

  LOG(INFO) << "Spawning " << kNetDiagsUpload << " @ " << last_log_stash_;
  vector<char*> args;
  args.push_back(const_cast<char*>(kNetDiagsUpload));
  if (IsReportingEnabled()) {
    args.push_back(const_cast<char*>("--upload"));
  }
  args.push_back(nullptr);
  pid_t pid = 0;
  struct minijail* jail = minijail_->New();
  minijail_->DropRoot(jail, kNetDiagsUploadUser, kNetDiagsUploadUser);
  if (minijail_->RunAndDestroy(jail, args, &pid)) {
    process_killer_->Wait(pid, Closure());
  } else {
    LOG(ERROR) << "Unable to spawn " << kNetDiagsUpload;
  }
}

bool DiagnosticsReporter::IsReportingEnabled() {
  // TODO(petkov): Implement this when there's a way to control reporting
  // through policy. crbug.com/218045.
  return false;
}

}  // namespace shill