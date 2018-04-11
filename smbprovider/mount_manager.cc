// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/mount_manager.h"

#include <algorithm>
#include <utility>

#include <base/strings/string_util.h>

#include "smbprovider/credential_store.h"
#include "smbprovider/smbprovider_helper.h"

namespace smbprovider {

MountManager::MountManager(std::unique_ptr<CredentialStore> credential_store)
    : credential_store_(std::move(credential_store)) {}

MountManager::~MountManager() = default;

bool MountManager::IsAlreadyMounted(int32_t mount_id) const {
  auto mount_iter = mounts_.find(mount_id);
  if (mount_iter == mounts_.end()) {
    return false;
  }

  DCHECK(credential_store_->HasCredentials(mount_iter->second));
  return true;
}

bool MountManager::IsAlreadyMounted(const std::string& mount_root) const {
  const bool has_credentials = credential_store_->HasCredentials(mount_root);
  if (!has_credentials) {
    DCHECK(!ExistsInMounts(mount_root));
    return false;
  }

  DCHECK(ExistsInMounts(mount_root));
  return true;
}

bool MountManager::AddMount(const std::string& mount_root,
                            const std::string& workgroup,
                            const std::string& username,
                            const base::ScopedFD& password_fd,
                            int32_t* mount_id) {
  DCHECK(!IsAlreadyMounted(next_mount_id_));
  DCHECK(mount_id);

  if (!credential_store_->AddCredentials(mount_root, workgroup, username,
                                         password_fd)) {
    return false;
  }

  can_remount_ = false;
  mounts_[next_mount_id_] = mount_root;
  *mount_id = next_mount_id_++;
  return true;
}

bool MountManager::Remount(const std::string& mount_root, int32_t mount_id) {
  DCHECK(can_remount_);
  DCHECK(!IsAlreadyMounted(mount_id));
  DCHECK_GE(mount_id, 0);

  if (!credential_store_->AddEmptyCredentials(mount_root)) {
    // TODO(allenvic): Handle persistent credentials on remount.
    return false;
  }

  mounts_[mount_id] = mount_root;
  next_mount_id_ = std::max(next_mount_id_, mount_id) + 1;
  return true;
}

bool MountManager::RemoveMount(int32_t mount_id) {
  auto mount_iter = mounts_.find(mount_id);
  if (mount_iter == mounts_.end()) {
    return false;
  }

  bool removed = credential_store_->RemoveCredentials(mount_iter->second);
  DCHECK(removed);

  mounts_.erase(mount_iter);
  return true;
}

bool MountManager::GetFullPath(int32_t mount_id,
                               const std::string& entry_path,
                               std::string* full_path) const {
  DCHECK(full_path);

  auto mount_iter = mounts_.find(mount_id);
  if (mount_iter == mounts_.end()) {
    return false;
  }

  *full_path = AppendPath(mount_iter->second, entry_path);
  return true;
}

std::string MountManager::GetRelativePath(int32_t mount_id,
                                          const std::string& full_path) const {
  auto mount_iter = mounts_.find(mount_id);
  DCHECK(mount_iter != mounts_.end());

  DCHECK(StartsWith(full_path, mount_iter->second,
                    base::CompareCase::INSENSITIVE_ASCII));

  return full_path.substr(mount_iter->second.length());
}

bool MountManager::ExistsInMounts(const std::string& mount_root) const {
  for (auto const& mount : mounts_) {
    if (mount.second == mount_root) {
      return true;
    }
  }

  return false;
}

}  // namespace smbprovider
