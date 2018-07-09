// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/seneschal/service.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/vm_sockets.h>  // needs to come after sys/socket.h

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_split.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/scoped_minijail.h>
#include <seneschal/proto_bindings/seneschal_service.pb.h>

using std::string;

namespace vm_tools {
namespace seneschal {
namespace {
// Path to the runtime directory where we will create server jails.
constexpr char kRuntimeDir[] = "/run/seneschal";

// The chronos uid and gid.  These are used for file system access.
constexpr uid_t kChronosUid = 1000;
constexpr gid_t kChronosGid = 1000;

// The gid of the chronos-access group.
constexpr gid_t kChronosAccessGid = 1001;

// The uid used for authenticating with DBus.
constexpr uid_t kDbusAuthUid = 20115;

// How long we should wait for a server process to exit.
constexpr base::TimeDelta kServerExitTimeout = base::TimeDelta::FromSeconds(2);

// Path to the 9p server.
constexpr char kServerPath[] = "/usr/bin/9s";
constexpr char kServerRoot[] = "/fsroot";
constexpr char kSeccompPolicyPath[] = "/usr/share/policy/crosvm/9s.policy";

// `mkdir -p`, essentially.  Reimplement all of base::CreateDirectory because
// we want mode 0755 instead of mode 0700.
bool MkdirRecursively(const base::FilePath& full_path) {
  if (!full_path.IsAbsolute()) {
    LOG(INFO) << "Relative paths are not supported: " << full_path.value();
    return false;
  }

  // Collect a list of all parent directories.
  std::vector<std::string> components;
  full_path.GetComponents(&components);
  DCHECK(!components.empty());

  base::ScopedFD fd(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!fd.is_valid())
    return false;

  // Iterate through the parents and create the missing ones. '+ 1' is for
  // skipping "/".
  for (std::vector<std::string>::const_iterator i = components.begin() + 1;
       i != components.end(); ++i) {
    // Try to create the directory. Note that Chromium's MkdirRecursively() uses
    // 0700, but we use 0755.
    if (mkdirat(fd.get(), i->c_str(), 0755) != 0) {
      if (errno != EEXIST) {
        PLOG(ERROR) << "Failed to mkdirat " << *i
                    << ": full_path=" << full_path.value();
        return false;
      }

      // The path already exists. Make sure that the path is a directory.
      struct stat st;
      if (fstatat(fd.get(), i->c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
        PLOG(ERROR) << "Failed to fstatat " << *i
                    << ": full_path=" << full_path.value();
        return false;
      }
      if (!S_ISDIR(st.st_mode)) {
        LOG(ERROR) << *i << " is not a directory: st_mode=" << st.st_mode
                   << ", full_path=" << full_path.value();
        return false;
      }
    }

    // Updates the FD so it refers to the new directory created or checked
    // above.
    const int new_fd =
        openat(fd.get(), i->c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK, 0);
    if (new_fd < 0) {
      PLOG(ERROR) << "Failed to openat " << *i
                  << ": full_path=" << full_path.value();
      return false;
    }
    fd.reset(new_fd);
    continue;
  }
  return true;
}

// Passes |method_call| to |handler| and passes the response to
// |response_sender|. If |handler| returns NULL, an empty response is created
// and sent.
void HandleSynchronousDBusMethodCall(
    base::Callback<std::unique_ptr<dbus::Response>(dbus::MethodCall*)> handler,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> response = handler.Run(method_call);
  if (!response)
    response = dbus::Response::FromMethodCall(method_call);
  response_sender.Run(std::move(response));
}

}  // namespace

Service::ServerInfo::ServerInfo(pid_t pid, base::FilePath root_dir)
    : pid_(pid) {
  CHECK(root_dir_.Set(root_dir));
}

Service::ServerInfo::ServerInfo(Service::ServerInfo&& other)
    : pid_(other.pid_) {
  CHECK(root_dir_.Set(other.root_dir_.Take()));
}

Service::ServerInfo& Service::ServerInfo::operator=(
    Service::ServerInfo&& other) {
  // Self assignment check is required.
  if (this != &other) {
    pid_ = other.pid_;
    CHECK(root_dir_.Set(other.root_dir_.Take()));
  }

  return *this;
}

Service::ServerInfo::~ServerInfo() {
  if (!root_dir_.IsValid()) {
    // Nothing to see here.
    return;
  }

  // Clean up the mounts so that we can delete the temporary directory.  An
  // error in any of these operations means that we cannot safely delete the
  // directory.  Instead the directory will get cleaned up when seneschal exits
  // as this will delete the mount namespace and all the mounts in it.
  string contents;
  if (!base::ReadFileToString(base::FilePath("/proc/self/mounts"), &contents)) {
    PLOG(ERROR) << "Unable to read contents of /proc/self/mounts; not deleting "
                << "runtime directory";
    root_dir_.Take();
    return;
  }

  std::vector<string> mounts;
  for (base::StringPiece line : base::SplitStringPiece(
           contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::vector<base::StringPiece> mount_data = base::SplitStringPiece(
        line, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (mount_data.size() < 6) {
      LOG(ERROR) << "Invalid mount data: " << line;
      root_dir_.Take();
      return;
    }

    // The mount point is the second column.
    if (root_dir_.GetPath().IsParent(base::FilePath(mount_data[1]))) {
      mounts.emplace_back(mount_data[1].as_string());
    }
  }

  // Now unmount everything in reverse order.
  for (auto iter = mounts.rbegin(), end = mounts.rend(); iter != end; ++iter) {
    if (umount(iter->c_str()) != 0) {
      PLOG(ERROR) << "Unable to unmount path; not deleting runtime directory";
      root_dir_.Take();
      return;
    }
  }
}

// static
std::unique_ptr<Service> Service::Create(base::Closure quit_closure) {
  std::unique_ptr<Service> service(new Service(std::move(quit_closure)));

  if (!service->Init()) {
    service.reset();
  }

  return service;
}

Service::Service(base::Closure quit_closure)
    : next_server_handle_(1),
      quit_closure_(std::move(quit_closure)),
      weak_factory_(this) {}

bool Service::Init() {
  // Set up the dbus service.
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(std::move(opts));

  // When authenticating with DBus a client process that wants to connect to
  // the system dbus daemon sends an authentication request with its current
  // effective uid.  The dbus daemon then uses SO_PEERCRED to verify that the
  // uid of the client process matches what it claims to be.  Normally this is
  // fine but when the client process runs inside a user namespace it thinks it
  // has uid 0 inside the namespace while the dbus daemon, which runs outside
  // the namespace, thinks it has some other uid.  To deal with this we
  // temprarily change our effective uid to match the effective uid outside the
  // user namespace and then change it back once we have authenticated with the
  // dbus daemon.
  if (seteuid(kDbusAuthUid) != 0) {
    PLOG(ERROR) << "Unable to change effective uid to " << kDbusAuthUid;
    return false;
  }

  if (!bus_->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return false;
  }

  if (seteuid(0) != 0) {
    PLOG(ERROR) << "Unable to change effective uid back to 0";
    return false;
  }

  // Add chronos-access to our list of supplementary groups.  This is needed so
  // that we can access the user's files in the /home directory.
  gid_t list[NGROUPS_MAX] = {};
  int count = getgroups(NGROUPS_MAX, list);
  if (count < 0) {
    PLOG(ERROR) << "Failed to get supplementary groups";
    return false;
  }
  CHECK_LT(count, NGROUPS_MAX);

  list[count++] = kChronosAccessGid;
  if (setgroups(count, list) != 0) {
    PLOG(ERROR) << "Failed to add chronos-access to supplementary groups";
    return false;
  }

  exported_object_ =
      bus_->GetExportedObject(dbus::ObjectPath(kSeneschalServicePath));
  if (!exported_object_) {
    LOG(ERROR) << "Failed to export " << kSeneschalServicePath << " object";
    return false;
  }

  using ServiceMethod =
      std::unique_ptr<dbus::Response> (Service::*)(dbus::MethodCall*);
  const std::map<const char*, ServiceMethod> kServiceMethods = {
      {kStartServerMethod, &Service::StartServer},
      {kStopServerMethod, &Service::StopServer},
      {kSharePathMethod, &Service::SharePath},
  };

  for (const auto& iter : kServiceMethods) {
    bool ret = exported_object_->ExportMethodAndBlock(
        kSeneschalInterface, iter.first,
        base::Bind(&HandleSynchronousDBusMethodCall,
                   base::Bind(iter.second, base::Unretained(this))));
    if (!ret) {
      LOG(ERROR) << "Failed to export method " << iter.first;
      return false;
    }
  }

  if (!bus_->RequestOwnershipAndBlock(kSeneschalServiceName,
                                      dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(ERROR) << "Failed to take ownership of " << kSeneschalServiceName;
    return false;
  }

  // Set up the signalfd for receiving SIGCHLD and SIGTERM.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGTERM);

  signal_fd_.reset(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
  if (!signal_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create signalfd";
    return false;
  }

  bool ret = base::MessageLoopForIO::current()->WatchFileDescriptor(
      signal_fd_.get(), true /*persistent*/, base::MessageLoopForIO::WATCH_READ,
      &watcher_, this);
  if (!ret) {
    LOG(ERROR) << "Failed to watch signalfd";
    return false;
  }

  // Now block signals from the normal signal handling path so that we will get
  // them via the signalfd.
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
    PLOG(ERROR) << "Failed to block signals via sigprocmask";
    return false;
  }

  return true;
}

void Service::OnFileCanReadWithoutBlocking(int fd) {
  DCHECK_EQ(signal_fd_.get(), fd);

  struct signalfd_siginfo siginfo;
  if (read(signal_fd_.get(), &siginfo, sizeof(siginfo)) != sizeof(siginfo)) {
    PLOG(ERROR) << "Failed to read from signalfd";
    return;
  }

  if (siginfo.ssi_signo == SIGCHLD) {
    HandleChildExit();
  } else if (siginfo.ssi_signo == SIGTERM) {
    HandleSigterm();
  } else {
    LOG(ERROR) << "Received unknown signal from signal fd: "
               << strsignal(siginfo.ssi_signo);
  }
}

void Service::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED();
}

void Service::HandleChildExit() {
  // We can't just rely on the information in the siginfo structure because
  // more than one child may have exited but only one SIGCHLD will be
  // generated.
  while (true) {
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG);
    if (pid <= 0) {
      if (pid == -1 && errno != ECHILD) {
        PLOG(ERROR) << "Unable to reap child processes";
      }
      break;
    }

    if (WIFEXITED(status)) {
      LOG(INFO) << "Process " << pid << " exited with status "
                << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(INFO) << "Process " << pid << " killed by signal " << WTERMSIG(status)
                << (WCOREDUMP(status) ? " (core dumped)" : "");
    } else {
      LOG(WARNING) << "Unknown exit status " << status << " for process "
                   << pid;
    }

    // See if this is a process we launched.
    for (const auto& pair : servers_) {
      if (pid == pair.second.pid()) {
        servers_.erase(pair.first);
        break;
      }
    }
  }
}

void Service::HandleSigterm() {
  LOG(INFO) << "Shutting down due to SIGTERM";

  // Close our connection to the bus.
  bus_->ShutdownAndBlock();

  // Stop the message loop.
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, quit_closure_);
}

// Handles a request to start a new 9p server.
std::unique_ptr<dbus::Response> Service::StartServer(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received request to start new 9p server";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  StartServerRequest request;
  StartServerResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse StartServerRequest from message";
    response.set_failure_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::ScopedTempDir root_dir;
  if (!root_dir.CreateUniqueTempDirUnderPath(base::FilePath(kRuntimeDir))) {
    LOG(ERROR) << "Unable to create working dir for server";
    response.set_failure_reason("Unable to create working dir for server");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Make sure the child process has permission to read the contents.
  if (chmod(root_dir.GetPath().value().c_str(), 0755) != 0) {
    PLOG(ERROR) << "Failed to change permissions for "
                << root_dir.GetPath().value();
    response.set_failure_reason(
        "Failed to change permissions for server's working dir");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Create the directory that the server will serve to clients.  Offset the
  // root path by 1 because Append wants relative paths.
  base::FilePath client_root = root_dir.GetPath().Append(&kServerRoot[1]);
  if (mkdir(client_root.value().c_str(), 0755) != 0) {
    PLOG(ERROR) << "Unable to create server root dir";
    response.set_failure_reason("Unable to create server root dir");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Get the listening address and any extra command line options.
  std::vector<string> args = {kServerPath, "-r", kServerRoot};
  bool valid_address = false;
  switch (request.listen_address_case()) {
    case StartServerRequest::kVsock: {
      const VsockAddress& addr = request.vsock();
      if (addr.accept_cid() < 3) {
        LOG(ERROR) << "Missing or invalid accept_cid field in vsock address: "
                   << addr.accept_cid();
        break;
      }

      args.emplace_back("--accept_cid");
      args.emplace_back(std::to_string(addr.accept_cid()));
      args.emplace_back(string("vsock:") + std::to_string(addr.port()));
      valid_address = true;
      break;
    }
    case StartServerRequest::kUnixAddr:
    case StartServerRequest::kNet:
    case StartServerRequest::kFd:
      LOG(ERROR) << "Listen address not implemented: "
                 << request.listen_address_case();
      break;
    case StartServerRequest::LISTEN_ADDRESS_NOT_SET:
      LOG(ERROR) << "Listen address not set";
    default:
      LOG(ERROR) << "Unknown listen address: " << request.listen_address_case();
      break;
  }

  if (!valid_address) {
    LOG(ERROR) << "Unable to create listening address";
    response.set_failure_reason("Unable to create listening address");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::vector<const char*> argv(args.size());
  std::transform(args.begin(), args.end(), argv.begin(),
                 [](const string& arg) -> const char* { return arg.c_str(); });
  argv.emplace_back(nullptr);

  // Mount in some useful paths.  We cannot use minijail_bind here because that
  // implicitly enters a new mount namespace and we explicitly want the child
  // process to live in seneschal's mount namespace.
  constexpr struct {
    const char* src;
    bool writable;
  } bind_mounts[] = {
      {
          .src = "/proc", .writable = false,
      },
      {
          .src = "/dev/null", .writable = true,
      },
      {
          .src = "/dev/log", .writable = true,
      },
  };
  for (const auto& bind_mount : bind_mounts) {
    // Offset by 1 because Append wants relative paths.
    base::FilePath dst = root_dir.GetPath().Append(&bind_mount.src[1]);
    struct stat info;
    if (stat(bind_mount.src, &info) != 0) {
      PLOG(ERROR) << "Unable to stat " << bind_mount.src;
      response.set_failure_reason("Unable to set up server jail");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    if (S_ISDIR(info.st_mode)) {
      // Only need to create the directory.
      if (!MkdirRecursively(dst)) {
        PLOG(ERROR) << "Failed to create " << dst.value();
        response.set_failure_reason("Unable to set up server jail");
        writer.AppendProtoAsArrayOfBytes(response);
        return dbus_response;
      }
    } else {
      // Need to create the file and the parent directories.
      if (!MkdirRecursively(dst.DirName())) {
        PLOG(ERROR) << "Failed to create " << dst.DirName().value();
        response.set_failure_reason("Unable to set up server jail");
        writer.AppendProtoAsArrayOfBytes(response);
        return dbus_response;
      }

      // Now touch the file so we can mount over it.
      base::ScopedFD file(open(dst.value().c_str(),
                               O_WRONLY | O_CREAT | O_CLOEXEC | O_NONBLOCK,
                               0600) != 0);
      if (!file.is_valid()) {
        PLOG(ERROR) << "Unable to touch " << dst.value();
        response.set_failure_reason("Unable to set up server jail");
        writer.AppendProtoAsArrayOfBytes(response);
        return dbus_response;
      }
    }

    // Now actually do the bind mount.
    unsigned long flags = MS_BIND | MS_REC;  // NOLINT(runtime/int)
    const char* target = dst.value().c_str();
    if (mount(bind_mount.src, target, "none", flags, nullptr) != 0) {
      PLOG(ERROR) << "Unable to bind mount " << bind_mount.src;
      response.set_failure_reason("Unable to set up server jail");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }

    // Remount read-only if necessary.
    if (!bind_mount.writable) {
      flags |= MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC;
      if (mount(bind_mount.src, target, "none", flags, nullptr) != 0) {
        PLOG(ERROR) << "Unable to remount " << bind_mount.src << " read-only";
        response.set_failure_reason("Unable to set up server jail");
        writer.AppendProtoAsArrayOfBytes(response);
        return dbus_response;
      }
    }
  }

  ScopedMinijail jail(minijail_new());
  if (!jail) {
    LOG(ERROR) << "Unable to create minijail";
    response.set_failure_reason("Unable to create minijail");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // We want this process to share namespaces with its parent.
  minijail_change_uid(jail.get(), kChronosUid);
  minijail_change_gid(jail.get(), kChronosGid);

  // The process can only see what is in its root directory.
  int ret =
      minijail_enter_chroot(jail.get(), root_dir.GetPath().value().c_str());
  if (ret < 0) {
    LOG(ERROR) << "Unable to configure pivot_root: " << strerror(-ret);
    response.set_failure_reason("Unable to configure pivot_root");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // We will manage this process's lifetime.
  minijail_run_as_init(jail.get());

  // It doesn't need any caps or any new privileges.
  minijail_use_caps(jail.get(), 0);
  minijail_no_new_privs(jail.get());

  // Use a seccomp filter.
  minijail_log_seccomp_filter_failures(jail.get());
  minijail_parse_seccomp_filters(jail.get(), kSeccompPolicyPath);
  minijail_use_seccomp_filter(jail.get());

  // Reset the signal mask since we block SIGCHLD and SIGTERM in this process
  // for signalfd.
  minijail_reset_signal_mask(jail.get());
  minijail_reset_signal_handlers(jail.get());

  // Launch the server.
  pid_t child_pid = 0;
  ret = minijail_run_pid(jail.get(), kServerPath,
                         const_cast<char* const*>(argv.data()), &child_pid);
  if (ret < 0) {
    LOG(ERROR) << "Unable to spawn server process: " << strerror(-ret);
    response.set_failure_reason("Unable to spawn server");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // We're done.
  LOG(INFO) << "Started server on " << root_dir.GetPath().value();

  uint32_t handle = next_server_handle_++;
  servers_.emplace(handle, ServerInfo(child_pid, root_dir.Take()));

  response.set_success(true);
  response.set_handle(handle);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

// Handles a request to stop a running 9p server.
std::unique_ptr<dbus::Response> Service::StopServer(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received request to stop server";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  StopServerRequest request;
  StopServerResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse StopServerRequest from message";
    response.set_failure_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto& iter = servers_.find(request.handle());
  if (iter == servers_.end()) {
    // The server is gone.  Nothing left to do here.
    response.set_success(true);
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Otherwise we send the process a SIGTERM and report success while lazily
  // ensuring the server will exit.  This works because we don't reuse handles
  // (unless we somehow spawn ~4 billion servers in ~2 seconds).
  if (kill(iter->second.pid(), SIGTERM) != 0 && errno != ESRCH) {
    PLOG(ERROR) << "Unable to send SIGTERM to child process";
    response.set_failure_reason("Unable to send signal to child process");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Service::KillServer, weak_factory_.GetWeakPtr(),
                 request.handle()),
      kServerExitTimeout);

  response.set_success(true);
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

// Handles a request to share a path with a running server.
std::unique_ptr<dbus::Response> Service::SharePath(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received request to share path with server";

  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));

  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());

  SharePathRequest request;
  SharePathResponse response;

  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse SharePathRequest from message";
    response.set_failure_reason("Unable to parse protobuf");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const auto& iter = servers_.find(request.handle());
  if (iter == servers_.end()) {
    LOG(ERROR) << "Requested server does not exist";
    response.set_failure_reason("Requested server does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Build the starting directory.
  base::FilePath src = base::FilePath("/home/user").Append(request.owner_id());
  switch (request.storage_location()) {
    case SharePathRequest::DOWNLOADS:
      src = src.Append("Downloads");
      break;
    default:
      LOG(ERROR) << "Unknown storage location: " << request.storage_location();
      response.set_failure_reason("Unknown storage location");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
  }

  // Get the remaining path.
  base::FilePath path(request.shared_path().path());
  if (path.IsAbsolute() || path.ReferencesParent() ||
      path.BaseName().value() == ".") {
    LOG(ERROR) << "Requested path references parent, is absolute, or ends "
               << "with ./";
    response.set_failure_reason(
        "Path must be relative and cannot reference parent components nor end "
        "with \".\"");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  src = src.Append(path);

  if (!base::PathExists(src)) {
    LOG(ERROR) << "Requested path does not exist";
    response.set_failure_reason("Requested path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  base::FilePath dst = iter->second.root_dir()
                           .GetPath()
                           .Append(&kServerRoot[1])
                           .Append(src.BaseName());
  if (base::PathExists(dst)) {
    LOG(ERROR) << "Destination path already exists";
    response.set_failure_reason("Destination path already exists");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  struct stat info;
  if (stat(src.value().c_str(), &info) != 0) {
    PLOG(ERROR) << "Unable to stat source path";
    response.set_failure_reason("Unable to stat source path");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (S_ISDIR(info.st_mode)) {
    if (mkdir(dst.value().c_str(), 0700) != 0) {
      PLOG(ERROR) << "Unable to create destination directory";
      response.set_failure_reason("Unable to create destination directory");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
  } else {
    base::ScopedFD file(open(dst.value().c_str(),
                             O_WRONLY | O_CREAT | O_CLOEXEC | O_NONBLOCK,
                             0600) != 0);
    if (!file.is_valid()) {
      PLOG(ERROR) << "Unable to create destination file";
      response.set_failure_reason("Unable to create destination file");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
  }

  // Do the mount.
  unsigned long flags = MS_BIND | MS_REC;  // NOLINT(runtime/int)
  const char* source = src.value().c_str();
  const char* target = dst.value().c_str();
  if (mount(source, target, "none", flags, nullptr) != 0) {
    PLOG(ERROR) << "Unable to create bind mount";
    response.set_failure_reason("Unable to create bind mount");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Left out because we do not currently have permissions to change the flags
  // of a mount, even if it reduces privilege.  Thanks, Torvalds.
  // We cannot specify `MS_BIND` and `MS_RDONLY` in the same mount call so
  // we have remount the path to make it read-only.
  // if (!request.shared_path().writable()) {
  //   flags |= MS_REMOUNT | MS_RDONLY;
  //   if (mount(source, target, "none", flags, nullptr) != 0) {
  //     PLOG(ERROR) << "Unable to remount read-only";

  //     // Unmount the target so that we don't leak it in a writable state.
  //     // There's not a lot we can do in case of failure here.
  //     umount2(target, MNT_DETACH);
  //     // TODO: also delete the path

  //     response.set_failure_reason("Unable to remount read-only");
  //     writer.AppendProtoAsArrayOfBytes(response);
  //     return dbus_response;
  //   }
  // }

  response.set_success(true);
  response.set_path(base::FilePath("/").Append(src.BaseName()).value());
  writer.AppendProtoAsArrayOfBytes(response);
  return dbus_response;
}

// Forcibly kills a server if it hasn't already exited.
void Service::KillServer(uint32_t handle) {
  const auto& iter = servers_.find(handle);
  if (iter != servers_.end()) {
    // Kill it with fire.
    if (kill(iter->second.pid(), SIGKILL) != 0) {
      PLOG(ERROR) << "Unable to send SIGKILL to child process";
    }
  }
  // We reap the child process through the normal sigchld handling mechanism.
}

}  // namespace seneschal
}  // namespace vm_tools