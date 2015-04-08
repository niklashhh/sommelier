// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "germ/launcher.h"

#include <sys/types.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/minijail/minijail.h>
#include <chromeos/process.h>

#include "germ/environment.h"

namespace {
const char* kSandboxedServiceTemplate = "germ_template";
const size_t kStdoutBufSize = 1024;

pid_t GetPidFromStdout(int stdout) {
  // germ_template (test) start/running, process 8117
  char buf[kStdoutBufSize] = {0};
  base::ReadFromFD(stdout, buf, kStdoutBufSize - 1);
  std::string output(buf);
  std::vector<std::string> tokens;
  base::SplitString(output, ' ', &tokens);
  pid_t pid = -1;
  if (tokens.size() < 5 || !base::StringToInt(tokens[4], &pid)) {
    LOG(ERROR) << "Could not extract pid from '" << output << "'";
    return -1;
  }
  return pid;
}

}  // namespace

namespace germ {

// Starts with the top half of user ids.
class UidService final {
 public:
  UidService() { min_uid_ = 1 << ((sizeof(uid_t) * 8) >> 1); }
  ~UidService() {}

  uid_t GetUid() {
    return static_cast<uid_t>(base::RandInt(min_uid_, 2 * min_uid_));
  }

 private:
  uid_t min_uid_;
};

Launcher::Launcher() {
  uid_service_.reset(new UidService());
}

Launcher::~Launcher() {}

bool Launcher::RunInteractive(const std::string& name,
                              const std::vector<std::string>& argv,
                              int* status) {
  std::vector<char*> command_line;
  for (const auto& t : argv) {
    command_line.push_back(const_cast<char*>(t.c_str()));
  }
  // Minijail will use the underlying char* array as 'argv',
  // so null-terminate it.
  command_line.push_back(nullptr);

  chromeos::Minijail* minijail = chromeos::Minijail::GetInstance();

  uid_t uid = uid_service_->GetUid();
  Environment env(uid, uid);

  return minijail->RunSyncAndDestroy(env.GetForInteractive(), command_line,
                                     status);
}

bool Launcher::RunDaemonized(const std::string& name,
                             const std::vector<std::string>& argv,
                             pid_t* pid) {
  // initctl start germ_template NAME=yes ENVIRONMENT= COMMANDLINE=/usr/bin/yes
  uid_t uid = uid_service_->GetUid();
  Environment env(uid, uid);

  chromeos::ProcessImpl initctl;
  initctl.AddArg("/sbin/initctl");
  initctl.AddArg("start");
  initctl.AddArg(kSandboxedServiceTemplate);
  initctl.AddArg(base::StringPrintf("NAME=%s", name.c_str()));
  initctl.AddArg(env.GetForService());
  std::string command_line = JoinString(argv, ' ');
  initctl.AddArg(base::StringPrintf("COMMANDLINE=%s", command_line.c_str()));
  initctl.RedirectUsingPipe(STDOUT_FILENO, false /* is_input */);

  // Since we're running 'initctl', and not the executable itself,
  // we wait for it to exit.
  initctl.Start();
  int stdout_fd = initctl.GetPipe(STDOUT_FILENO);
  *pid = GetPidFromStdout(stdout_fd);
  int exit_status = initctl.Wait();
  if (exit_status != 0) {
    LOG(ERROR) << "'initctl start' failed with exit status " << exit_status;
    *pid = -1;
    return false;
  }
  VLOG(1) << "service name " << name << " pid " << pid;
  names_[*pid] = name;
  return true;
}

bool Launcher::Terminate(pid_t pid) {
  if (pid < 0) {
    LOG(ERROR) << "Invalid pid " << pid;
    return false;
  }

  if (names_.find(pid) == names_.end()) {
    LOG(ERROR) << "Unknown pid " << pid;
    return false;
  }

  std::string name = names_[pid];

  // initctl stop germ_template NAME=<name>
  chromeos::ProcessImpl initctl;
  initctl.AddArg("/sbin/initctl");
  initctl.AddArg("stop");
  initctl.AddArg(kSandboxedServiceTemplate);
  initctl.AddArg(base::StringPrintf("NAME=%s", name.c_str()));
  int exit_status = initctl.Run();
  if (exit_status != 0) {
    LOG(ERROR) << "'initctl stop' failed with exit status " << exit_status;
    return false;
  }
  names_.erase(pid);
  return true;
}

}  // namespace germ
