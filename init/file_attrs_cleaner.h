// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_FILE_ATTRS_CLEANER_H_
#define INIT_FILE_ATTRS_CLEANER_H_

#include <string>

#include <base/files/file_path.h>

namespace file_attrs_cleaner {

// Whether we allow |path| to be marked with immutable file attribute.
// If |path| is supposed to be a directory, set |isdir| to true.
bool ImmutableAllowed(const base::FilePath& path, bool isdir);

// Check the file attributes of the specified path.  |path| is used for logging
// and policy checking, so |fd| needs to be an open handle to it.  This helps
// with TOCTTOU issues.  If |path| is supposed to be a directory, set |isdir|
// to true.
bool CheckFileAttributes(const base::FilePath& path, bool isdir, int fd);

// Recursively scan the file attributes of paths under |dir|.
bool ScanDir(const base::FilePath& dir);

// Convenience function.
static inline bool ScanDir(const std::string& dir) {
  return ScanDir(base::FilePath(dir));
}

}  // namespace file_attrs_cleaner

#endif  // INIT_FILE_ATTRS_CLEANER_H_