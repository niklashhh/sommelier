/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INCLUDE_CROS_CAMERA_FUTURE_INTERNAL_H_
#define INCLUDE_CROS_CAMERA_FUTURE_INTERNAL_H_

#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>

namespace cros {

class CancellationRelay;

namespace internal {

class FutureLock {
 public:
  explicit FutureLock(CancellationRelay* relay);

  ~FutureLock();

  void Signal();

  /* Default timeout is set to 5 seconds.  Setting the timeout to a value less
   * than or equal to 0 will wait indefinitely until the value is set.
   */
  bool Wait(int timeout_ms = 5000);

  void Cancel();

 private:
  /* Used to serialize all member access. */
  base::Lock lock_;

  base::ConditionVariable cond_;

  /* Used to indicate that the FutureLock is cancelled. */
  bool cancelled_;

  /* Used to indicate that the FutureLock is signalled. */
  bool signalled_;

  /* Registerred by FutureLock to receive cancel signal */
  CancellationRelay* relay_;

  DISALLOW_COPY_AND_ASSIGN(FutureLock);
};

}  // namespace internal

}  // namespace cros

#endif  // INCLUDE_CROS_CAMERA_FUTURE_INTERNAL_H_