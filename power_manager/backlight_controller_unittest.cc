// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/logging.h"
#include "power_manager/backlight_controller.h"
#include "power_manager/mock_backlight.h"
#include "power_manager/power_constants.h"
#include "power_manager/power_prefs.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgumentPointee;
using std::make_pair;
using std::pair;
using std::vector;

namespace power_manager {

namespace {

const int64 kDefaultBrightness = 50;
const int64 kMaxBrightness = 100;
const int64 kPluggedBrightness = 70;
const int64 kUnpluggedBrightness = 30;
const int64 kAlsBrightness = 0;

// Repeating either increase or decrease brightness this many times should
// always leave the brightness at a limit.
const int kStepsToHitLimit = 20;

// Simple helper class that logs brightness changes for the NotifyObserver test.
class MockObserver : public BacklightControllerObserver {
 public:
  MockObserver() {}
  virtual ~MockObserver() {}

  vector<pair<double, BrightnessChangeCause> > changes() const {
    return changes_;
  }

  void Clear() {
    changes_.clear();
  }

  // BacklightControllerObserver implementation:
  virtual void OnScreenBrightnessChanged(double brightness_percent,
                                         BrightnessChangeCause cause) {
    changes_.push_back(make_pair(brightness_percent, cause));
  }

 private:
  // Received changes, in oldest-to-newest order.
  vector<pair<double, BrightnessChangeCause> > changes_;

  DISALLOW_COPY_AND_ASSIGN(MockObserver);
};

}  // namespace

class BacklightControllerTest : public ::testing::Test {
 public:
  BacklightControllerTest()
      : prefs_(FilePath("."), FilePath(".")),
        controller_(&backlight_, &prefs_) {
  }

  virtual void SetUp() {
    EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
        .WillRepeatedly(DoAll(SetArgumentPointee<0>(kDefaultBrightness),
                              Return(true)));
    EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
        .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                              Return(true)));
    EXPECT_CALL(backlight_, SetBrightnessLevel(_))
        .WillRepeatedly(Return(false));
    prefs_.SetInt64(kPluggedBrightnessOffset, kPluggedBrightness);
    prefs_.SetInt64(kUnpluggedBrightnessOffset, kUnpluggedBrightness);
    prefs_.SetInt64(kAlsBrightnessLevel, kAlsBrightness);
    prefs_.SetInt64(kMinBacklightLevel, 0);
    CHECK(controller_.Init());
  }

 protected:
  ::testing::StrictMock<MockBacklight> backlight_;
  PowerPrefs prefs_;
  BacklightController controller_;
};

TEST_F(BacklightControllerTest, IncreaseBrightness) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(false));
#ifdef HAS_ALS
  EXPECT_EQ(kDefaultBrightness, controller_.target_percent());
#else
  EXPECT_EQ(kUnpluggedBrightness, controller_.target_percent());
#endif // defined(HAS_ALS)

  double old_percent = controller_.target_percent();
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_AUTOMATED);
  // Check that the first step increases the brightness; within the loop
  // will just ensure that the brightness never decreases.
  EXPECT_GT(controller_.target_percent(), old_percent);

  for (int i = 0; i < kStepsToHitLimit; ++i) {
    old_percent = controller_.target_percent();
    controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
    EXPECT_GE(controller_.target_percent(), old_percent);
  }

  EXPECT_EQ(kMaxBrightness, controller_.target_percent());
}

TEST_F(BacklightControllerTest, DecreaseBrightness) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
#ifdef HAS_ALS
  EXPECT_EQ(kDefaultBrightness, controller_.target_percent());
#else
  EXPECT_EQ(kPluggedBrightness, controller_.target_percent());
#endif // defined(HAS_ALS)

  double old_percent = controller_.target_percent();
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_AUTOMATED);
  // Check that the first step decreases the brightness; within the loop
  // will just ensure that the brightness never increases.
  EXPECT_LT(controller_.target_percent(), old_percent);

  for (int i = 0; i < kStepsToHitLimit; ++i) {
    old_percent = controller_.target_percent();
    controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
    EXPECT_LE(controller_.target_percent(), old_percent);
  }

  // Backlight should now be off.
  EXPECT_EQ(0, controller_.target_percent());
}

TEST_F(BacklightControllerTest, DecreaseBrightnessDisallowOff) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
#ifdef HAS_ALS
  EXPECT_EQ(kDefaultBrightness, controller_.target_percent());
#else
  EXPECT_EQ(kPluggedBrightness, controller_.target_percent());
#endif // defined(HAS_ALS)

  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);

  // Backlight must still be on.
  EXPECT_GT(controller_.target_percent(), 0);
}

TEST_F(BacklightControllerTest, DecreaseBrightnessDisallowOffAuto) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));

  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_AUTOMATED);

  // Backlight must still be on, even after a few state transitions.
  EXPECT_GT(controller_.target_percent(), 0);
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_DIM));
  EXPECT_GT(controller_.target_percent(), 0);
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  EXPECT_GT(controller_.target_percent(), 0);
}

// Test that BacklightController notifies its observer in response to brightness
// changes.
TEST_F(BacklightControllerTest, NotifyObserver) {
  // Set an initial state.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(false));
  controller_.SetAlsBrightnessOffsetPercent(16);

  MockObserver observer;
  controller_.set_observer(&observer);

  // Increase the brightness and check that the observer is notified.
  observer.Clear();
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_AUTOMATED);
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_EQ(controller_.target_percent(),
            observer.changes()[0].first);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].second);

  // Decrease the brightness.
  observer.Clear();
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_EQ(controller_.target_percent(),
            observer.changes()[0].first);
  EXPECT_EQ(BRIGHTNESS_CHANGE_USER_INITIATED, observer.changes()[0].second);

  // Send enough ambient light sensor samples to trigger a brightness change.
  observer.Clear();
  double old_percent = controller_.target_percent();
  for (int i = 0; i < 10; ++i)
    controller_.SetAlsBrightnessOffsetPercent(32);
  ASSERT_NE(old_percent, controller_.target_percent());
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_EQ(controller_.target_percent(), observer.changes()[0].first);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].second);

  // Plug the device in.
  observer.Clear();
  ASSERT_TRUE(controller_.OnPlugEvent(true));
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_EQ(controller_.target_percent(), observer.changes()[0].first);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].second);

#ifndef IS_DESKTOP
  // Dim the backlight.
  observer.Clear();
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_DIM));
  ASSERT_EQ(1, static_cast<int>(observer.changes().size()));
  EXPECT_EQ(controller_.target_percent(), observer.changes()[0].first);
  EXPECT_EQ(BRIGHTNESS_CHANGE_AUTOMATED, observer.changes()[0].second);
#endif
}

TEST_F(BacklightControllerTest, MinBrightnessLevel) {
  // Set a minimum visible backlight level and reinitialize to load it.
  const int kMinLevel = 10;
  prefs_.SetInt64(kMinBacklightLevel, kMinLevel);
  ASSERT_TRUE(controller_.Init());
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));

  // Increase the brightness and check that we hit the max.
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());

  // Decrease the brightness with allow_off=false and check that we stop when we
  // get to the minimum level that we set in the pref.
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(static_cast<double>(kMinLevel) / kMaxBrightness * 100.0,
                   controller_.target_percent());

  // Decrease again with allow_off=true and check that we turn the backlight
  // off.
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(0.0, controller_.target_percent());
}

// Test the case where the minimum visible backlight level matches the maximum
// level exposed by hardware.
TEST_F(BacklightControllerTest, MinBrightnessLevelMatchesMax) {
  prefs_.SetInt64(kMinBacklightLevel, kMaxBrightness);
  ASSERT_TRUE(controller_.Init());
#ifdef HAS_ALS
  // The controller avoids adjusting the brightness until it gets its first
  // reading from the ambient light sensor.
  controller_.SetAlsBrightnessOffsetPercent(0.0);
#endif
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());

  // Decrease the brightness with allow_off=false.
  controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());

  // Decrease again with allow_off=true.
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(0.0, controller_.target_percent());
}

// Test the saved brightness level before and after suspend.
TEST_F(BacklightControllerTest, SuspendBrightnessLevel) {
#ifdef HAS_ALS
  // The controller avoids adjusting the brightness until it gets its first
  // reading from the ambient light sensor.
  controller_.SetAlsBrightnessOffsetPercent(0.0);
#endif
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(true));
  EXPECT_DOUBLE_EQ(kPluggedBrightness, controller_.target_percent());

  // Test suspend and resume.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_SUSPENDED));
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  EXPECT_DOUBLE_EQ(kPluggedBrightness, controller_.target_percent());

#ifndef IS_DESKTOP
  // This test is not done on desktops because the brightness does not get
  // adjusted by SetPowerState() when idling.
  // Test idling into suspend state.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_DIM));
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_IDLE_OFF));
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_SUSPENDED));
  EXPECT_DOUBLE_EQ(0.0, controller_.target_percent());

  // Test resume.
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  EXPECT_DOUBLE_EQ(kPluggedBrightness, controller_.target_percent());
#endif // !defined(IS_DESKTOP)
}

// Check that BacklightController reinitializes itself correctly when the
// backlight device changes (i.e. a new monitor is connected).
TEST_F(BacklightControllerTest, ChangeBacklightDevice) {
  ASSERT_TRUE(controller_.SetPowerState(BACKLIGHT_ACTIVE));
  ASSERT_TRUE(controller_.OnPlugEvent(false));
  for (int i = 0; i < kStepsToHitLimit; ++i)
    controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());

  // Update the backlight to expose a [0, 1] range.
  const int64 kNewMaxBrightness = 1;
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kNewMaxBrightness),
                            Return(true)));
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kNewMaxBrightness),
                            Return(true)));

  // Check that there's a single step between 100% and 0%.
  controller_.OnBacklightDeviceChanged();
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());
  controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());
  controller_.DecreaseBrightness(true, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(0.0, controller_.target_percent());
  controller_.IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());

  // Make the backlight expose the original range again.
  EXPECT_CALL(backlight_, GetMaxBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));
  EXPECT_CALL(backlight_, GetCurrentBrightnessLevel(NotNull()))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(kMaxBrightness),
                            Return(true)));

  // We should permit more steps now.
  controller_.OnBacklightDeviceChanged();
  EXPECT_DOUBLE_EQ(100.0, controller_.target_percent());
  controller_.DecreaseBrightness(false, BRIGHTNESS_CHANGE_USER_INITIATED);
  EXPECT_LT(controller_.target_percent(), 100.0);
  EXPECT_GT(controller_.target_percent(), 0.0);
}

}  // namespace power_manager
