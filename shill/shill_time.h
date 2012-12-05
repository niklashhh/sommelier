// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TIME_H_
#define SHILL_TIME_H_

#include <sys/time.h>

#include <string>

#include <base/lazy_instance.h>

namespace shill {

// Timestamp encapsulates a |monotonic| time that can be used to compare the
// relative order and distance of events as well as a |wall_clock| time that can
// be used for presenting the time in human-readable format. Note that the
// monotonic clock does not necessarily advance during suspend.
struct Timestamp {
  Timestamp() : monotonic((const struct timeval){ 0 }) {}
  Timestamp(const struct timeval &in_monotonic,
            const std::string &in_wall_clock)
      : monotonic(in_monotonic),
        wall_clock(in_wall_clock) {}

  struct timeval monotonic;
  std::string wall_clock;
};

// A "sys/time.h" abstraction allowing mocking in tests.
class Time {
 public:
  virtual ~Time();

  static Time *GetInstance();

  // clock_gettime(CLOCK_MONOTONIC ...
  virtual int GetTimeMonotonic(struct timeval *tv);

  // gettimeofday
  virtual int GetTimeOfDay(struct timeval *tv, struct timezone *tz);

  // Returns a snapshot of the current time.
  virtual Timestamp GetNow();

 protected:
  Time();

 private:
  friend struct base::DefaultLazyInstanceTraits<Time>;

  DISALLOW_COPY_AND_ASSIGN(Time);
};

}  // namespace shill

#endif  // SHILL_TIME_H_
