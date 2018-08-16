// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/samba_interface_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/memory/ptr_util.h>

#include "smbprovider/constants.h"
#include "smbprovider/smbprovider_helper.h"

namespace smbprovider {

namespace {

// Returns the mount root by appending 'smb://' to |server| and |share|.
std::string GetMountRoot(const char* server, const char* share) {
  DCHECK(server);
  DCHECK(share);

  return std::string(kSmbUrlScheme) + server + "/" + share;
}

// Default handler for server side copy progress. Since nothing can make use
// of this callback yet, it remains an implementation detail.
int CopyProgressHandler(off_t upto, void* callback_context) {
  // Return non-zero to indicate that copy should continue.
  return 1;
}

}  // namespace

template <typename T>
std::unique_ptr<SambaInterfaceImpl> SambaInterfaceImpl::Create(
    T auth_callback) {
  SMBCCTX* context = smbc_new_context();
  if (!context) {
    LOG(ERROR) << "Could not create smbc context";
    return nullptr;
  }

  smbc_setOptionUseKerberos(context, 1);
  smbc_setOptionFallbackAfterKerberos(context, 1);

  if (!smbc_init_context(context)) {
    smbc_free_context(context, 0);
    LOG(ERROR) << "Could not initialize smbc context";
    return nullptr;
  }

  smbc_set_context(context);

  // Change auth_callback to a static pointer.
  static uint8_t static_auth_callback_memory[sizeof(T)];
  static T* static_auth_callback =
      new (static_auth_callback_memory) T(std::move(auth_callback));

  // Use static_auth_callback as a C function pointer by converting it to a
  // lambda.
  smbc_setFunctionAuthDataWithContext(
      context,
      +[](SMBCCTX* context, const char* srv, const char* shr, char* wg,
          int32_t wglen, char* un, int32_t unlen, char* pw, int32_t pwlen) {
        static_auth_callback->Run(GetMountRoot(srv, shr), wg, wglen, un, unlen,
                                  pw, pwlen);
      });

  return base::WrapUnique(new SambaInterfaceImpl(context));
}

int32_t SambaInterfaceImpl::CloseFile(int32_t file_id) {
  SMBCFILE* file = GetFile(file_id);
  if (!file) {
    return EBADF;
  }

  return smbc_close_ctx_(context_, file) >= 0 ? 0 : errno;
}

int32_t SambaInterfaceImpl::OpenFile(const std::string& file_path,
                                     int32_t flags,
                                     int32_t* file_id) {
  DCHECK(file_id);
  DCHECK(IsValidOpenFileFlags(flags));

  SMBCFILE* file =
      smbc_open_ctx_(context_, file_path.c_str(), flags, 0 /* mode */);
  if (!file) {
    *file_id = -1;
    return errno;
  }

  *file_id = NewFd(file);
  return 0;
}

int32_t SambaInterfaceImpl::OpenDirectory(const std::string& directory_path,
                                          int32_t* dir_id) {
  DCHECK(dir_id);

  SMBCFILE* dir = smbc_opendir_ctx_(context_, directory_path.c_str());
  if (!dir) {
    *dir_id = -1;
    return errno;
  }

  *dir_id = NewFd(dir);
  return 0;
}

int32_t SambaInterfaceImpl::CloseDirectory(int32_t dir_id) {
  SMBCFILE* dir = GetFile(dir_id);
  if (!dir) {
    return EBADF;
  }

  return smbc_closedir_ctx_(context_, dir) >= 0 ? 0 : errno;
}

int32_t SambaInterfaceImpl::GetDirectoryEntries(int32_t dir_id,
                                                smbc_dirent* dirp,
                                                int32_t dirp_buffer_size,
                                                int32_t* bytes_read) {
  DCHECK(dirp);
  DCHECK(bytes_read);

  SMBCFILE* dir = GetFile(dir_id);
  if (!dir) {
    return EBADF;
  }

  *bytes_read = smbc_getdents_ctx_(context_, dir, dirp, dirp_buffer_size);
  if (*bytes_read < 0) {
    *bytes_read = -1;
    return errno;
  }

  return 0;
}

int32_t SambaInterfaceImpl::GetDirectoryEntryWithMetadata(
    int32_t dir_id, const struct libsmb_file_info** file_info) {
  DCHECK(file_info);

  SMBCFILE* dir = GetFile(dir_id);
  if (!dir) {
    return EBADF;
  }

  // errno must be set to 0 before the call because the function returns
  // nullptr in both the error case and when there are no more files. When
  // there are no more files errno remains untouched.
  errno = 0;
  *file_info = smbc_readdirplus_ctx_(context_, dir);
  return errno;
}

int32_t SambaInterfaceImpl::GetEntryStatus(const std::string& full_path,
                                           struct stat* stat) {
  DCHECK(stat);
  return smbc_stat_ctx_(context_, full_path.c_str(), stat) >= 0 ? 0 : errno;
}

int32_t SambaInterfaceImpl::ReadFile(int32_t file_id,
                                     uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t* bytes_read) {
  DCHECK(buffer);
  DCHECK(bytes_read);

  SMBCFILE* file = GetFile(file_id);
  if (!file) {
    return EBADF;
  }

  *bytes_read = smbc_read_ctx_(context_, file, buffer, buffer_size);
  return *bytes_read < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::Seek(int32_t file_id, int64_t offset) {
  SMBCFILE* file = GetFile(file_id);
  if (!file) {
    return EBADF;
  }

  return smbc_lseek_ctx_(context_, file, offset, SEEK_SET) < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::Unlink(const std::string& file_path) {
  return smbc_unlink_ctx_(context_, file_path.c_str()) < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::RemoveDirectory(const std::string& dir_path) {
  return smbc_rmdir_ctx_(context_, dir_path.c_str()) < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::CreateFile(const std::string& file_path,
                                       int32_t* file_id) {
  SMBCFILE* file = smbc_open_ctx_(context_, file_path.c_str(), kCreateFileFlags,
                                  kCreateEntryPermissions);
  if (!file) {
    *file_id = -1;
    return errno;
  }

  *file_id = NewFd(file);
  return 0;
}

int32_t SambaInterfaceImpl::Truncate(int32_t file_id, size_t size) {
  SMBCFILE* file = GetFile(file_id);
  if (!file) {
    return EBADF;
  }

  return smbc_ftruncate_ctx_(context_, file, size) < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::WriteFile(int32_t file_id,
                                      const uint8_t* buffer,
                                      size_t buffer_size) {
  SMBCFILE* file = GetFile(file_id);
  if (!file) {
    return EBADF;
  }

  return smbc_write_ctx_(context_, file, buffer, buffer_size) < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::CreateDirectory(const std::string& directory_path) {
  int32_t result = smbc_mkdir_ctx_(context_, directory_path.c_str(),
                                   kCreateEntryPermissions);
  return result < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::MoveEntry(const std::string& source_path,
                                      const std::string& target_path) {
  int32_t result = smbc_rename_ctx_(context_, source_path.c_str(), context_,
                                    target_path.c_str());
  return result < 0 ? errno : 0;
}

int32_t SambaInterfaceImpl::CopyFile(const std::string& source_path,
                                     const std::string& target_path) {
  return CopyFile(source_path, target_path, CopyProgressHandler, nullptr);
}

int32_t SambaInterfaceImpl::CopyFile(const std::string& source_path,
                                     const std::string& target_path,
                                     CopyProgressCallback progress_callback,
                                     void* callback_context) {
  int32_t source_fd = -1;
  int32_t target_fd = -1;
  int32_t result = OpenCopySource(source_path, &source_fd);
  if (result != 0) {
    return result;
  }
  DCHECK_GE(source_fd, 0);

  result = OpenCopyTarget(target_path, &target_fd);
  if (result != 0) {
    CloseCopySourceAndTarget(source_fd, target_fd);
    return result;
  }
  DCHECK_GE(target_fd, 0);

  struct stat source_stat;
  result = GetEntryStatus(source_path, &source_stat);
  if (result != 0) {
    CloseCopySourceAndTarget(source_fd, target_fd);
    return result;
  }

  if (smbc_splice_ctx_(context_, MustGetFile(source_fd), MustGetFile(target_fd),
                       source_stat.st_size, progress_callback,
                       callback_context) == -1) {
    result = errno;
    CloseCopySourceAndTarget(source_fd, target_fd);
    return result;
  }

  CloseCopySourceAndTarget(source_fd, target_fd);
  return 0;
}

int32_t SambaInterfaceImpl::SpliceFile(int32_t source_fd,
                                       int32_t target_fd,
                                       off_t length,
                                       off_t* bytes_written) {
  DCHECK(bytes_written);

  SMBCFILE* source = GetFile(source_fd);
  SMBCFILE* target = GetFile(target_fd);

  if (!source || !target) {
    return EBADF;
  }

  CopyProgressCallback progress_callback = CopyProgressHandler;
  void* callback_context = nullptr;

  *bytes_written = smbc_splice_ctx_(context_, source, target, length,
                                    progress_callback, callback_context);
  if (*bytes_written == -1) {
    return errno;
  }
  return 0;
}

int32_t SambaInterfaceImpl::OpenCopySource(const std::string& file_path,
                                           int32_t* source_fd) {
  DCHECK(source_fd);
  SMBCFILE* source_file =
      smbc_open_ctx_(context_, file_path.c_str(), O_RDONLY, 0 /* mode */);
  if (!source_file) {
    return errno;
  }

  *source_fd = NewFd(source_file);
  return 0;
}

int32_t SambaInterfaceImpl::OpenCopyTarget(const std::string& file_path,
                                           int32_t* target_fd) {
  DCHECK(target_fd);
  SMBCFILE* target_file = smbc_open_ctx_(context_, file_path.c_str(),
                                         kCreateFileFlags, 0 /* mode */);
  if (!target_file) {
    return errno;
  }

  *target_fd = NewFd(target_file);
  return 0;
}

void SambaInterfaceImpl::CloseCopySourceAndTarget(int32_t source_fd,
                                                  int32_t target_fd) {
  if (source_fd >= 0) {
    smbc_close_ctx_(context_, MustGetFile(source_fd));
  }

  if (target_fd >= 0) {
    smbc_close_ctx_(context_, MustGetFile(target_fd));
  }
}

int32_t SambaInterfaceImpl::NewFd(SMBCFILE* file) {
  DCHECK(file);
  return fds_.Insert(file);
}

SMBCFILE* SambaInterfaceImpl::GetFile(int32_t fd) {
  auto iter = fds_.Find(fd);
  if (iter == fds_.End()) {
    return nullptr;
  }

  return iter->second;
}

SMBCFILE* SambaInterfaceImpl::MustGetFile(int32_t fd) {
  SMBCFILE* file = GetFile(fd);
  DCHECK(file);
  return file;
}

SambaInterfaceImpl::~SambaInterfaceImpl() {
  smbc_free_context(context_, 0);
}

SambaInterfaceImpl::SambaInterfaceImpl(SMBCCTX* context) : context_(context) {
  DCHECK(context);

  // Load all the required context functions.
  smbc_close_ctx_ = smbc_getFunctionClose(context);
  smbc_closedir_ctx_ = smbc_getFunctionClosedir(context);
  smbc_ftruncate_ctx_ = smbc_getFunctionFtruncate(context);
  smbc_getdents_ctx_ = smbc_getFunctionGetdents(context);
  smbc_lseek_ctx_ = smbc_getFunctionLseek(context);
  smbc_mkdir_ctx_ = smbc_getFunctionMkdir(context);
  smbc_open_ctx_ = smbc_getFunctionOpen(context);
  smbc_opendir_ctx_ = smbc_getFunctionOpendir(context);
  smbc_read_ctx_ = smbc_getFunctionRead(context);
  smbc_readdir_ctx_ = smbc_getFunctionReaddir(context);
  smbc_readdirplus_ctx_ = smbc_getFunctionReaddirPlus(context);
  smbc_rename_ctx_ = smbc_getFunctionRename(context);
  smbc_rmdir_ctx_ = smbc_getFunctionRmdir(context);
  smbc_splice_ctx_ = smbc_getFunctionSplice(context);
  smbc_stat_ctx_ = smbc_getFunctionStat(context);
  smbc_unlink_ctx_ = smbc_getFunctionUnlink(context);
  smbc_write_ctx_ = smbc_getFunctionWrite(context);
}

// This is required to explicitly instantiate the template function.
template std::unique_ptr<SambaInterfaceImpl> SambaInterfaceImpl::Create<
    SambaInterfaceImpl::AuthCallback>(SambaInterfaceImpl::AuthCallback);

}  // namespace smbprovider
