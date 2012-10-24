// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_METRICS_CONSTANTS_H_
#define POWER_MANAGER_POWERD_METRICS_CONSTANTS_H_
#pragma once

#include <time.h>

namespace power_manager {

extern const char kMetricBacklightLevelName[];
extern const int kMetricBacklightLevelMax;
extern const time_t kMetricBacklightLevelIntervalMs;
extern const int kMetricIdleAfterScreenOffMin;
extern const int kMetricIdleAfterScreenOffMax;
extern const int kMetricIdleAfterScreenOffBuckets;
extern const char kMetricIdleName[];
extern const int kMetricIdleMin;
extern const int kMetricIdleMax;
extern const int kMetricIdleBuckets;
extern const char kMetricIdleAfterDimName[];
extern const int kMetricIdleAfterDimMin;
extern const int kMetricIdleAfterDimMax;
extern const int kMetricIdleAfterDimBuckets;
extern const char kMetricIdleAfterScreenOffName[];
extern const char kMetricBatteryChargeHealthName[];
extern const int kMetricBatteryChargeHealthMax;
extern const char kMetricBatteryDischargeRateName[];
extern const int kMetricBatteryDischargeRateMin;
extern const int kMetricBatteryDischargeRateMax;
extern const int kMetricBatteryDischargeRateBuckets;
extern const time_t kMetricBatteryDischargeRateInterval;
extern const char kMetricBatteryRemainingWhenChargeStartsName[];
extern const int kMetricBatteryRemainingWhenChargeStartsMax;
extern const char kMetricBatteryRemainingAtEndOfSessionName[];
extern const int kMetricBatteryRemainingAtEndOfSessionMax;
extern const char kMetricBatteryRemainingAtStartOfSessionName[];
extern const int kMetricBatteryRemainingAtStartOfSessionMax;
extern const char kMetricNumberOfAlsAdjustmentsPerSessionName[];
extern const int kMetricNumberOfAlsAdjustmentsPerSessionMin;
extern const int kMetricNumberOfAlsAdjustmentsPerSessionMax;
extern const int kMetricNumberOfAlsAdjustmentsPerSessionBuckets;
extern const char kMetricUserBrightnessAdjustmentsPerSessionName[];
extern const int kMetricUserBrightnessAdjustmentsPerSessionMin;
extern const int kMetricUserBrightnessAdjustmentsPerSessionMax;
extern const int kMetricUserBrightnessAdjustmentsPerSessionBuckets;
extern const char kMetricLengthOfSessionName[];
extern const int kMetricLengthOfSessionMin;
extern const int kMetricLengthOfSessionMax;
extern const int kMetricLengthOfSessionBuckets;
extern const char kMetricNumOfSessionsPerChargeName[];
extern const int kMetricNumOfSessionsPerChargeMin;
extern const int kMetricNumOfSessionsPerChargeMax;
extern const int kMetricNumOfSessionsPerChargeBuckets;
extern const char kMetricBrightnessAdjust[];
extern const char kMetricPowerButtonDownTimeName[];
extern const int kMetricPowerButtonDownTimeMin;
extern const int kMetricPowerButtonDownTimeMax;
extern const int kMetricPowerButtonDownTimeBuckets;
extern const int kMetricThermalAbortedFanTurnOnMax;
extern const char kMetricThermalAbortedFanTurnOnName[];
extern const char kMetricThermalAbortedFanFilename[];
extern const int kMetricThermalMultipleFanTurnOnMax;
extern const char kMetricThermalMultipleFanTurnOnName[];
extern const char kMetricThermalMultipleFanFilename[];
extern const char kMetricThermalTurnedOnFanFilename[];
extern const time_t kMetricThermalIntervalMs;
extern const char kMetricBatteryInfoSampleName[];

// Enum for kMetricBrightnessAdjust.
enum MetricBrightnessAdjustType {
  kMetricBrightnessAdjustDown,
  kMetricBrightnessAdjustUp,
  kMetricBrightnessAdjustAbsolute,
  kMetricBrightnessAdjustEnumMax,
};

// Enum for kMetricBatteryInfoSample.
enum MetricBatteryInfoSampleResult {
  kMetricBatteryInfoSampleRead,
  kMetricBatteryInfoSampleGood,
  kMetricBatteryInfoSampleBad,
  kMetricBatteryInfoSampleEnumMax,
};

}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_METRICS_CONSTANTS_H_
