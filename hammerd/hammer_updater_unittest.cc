// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The calling structure of HammerUpdater:
//   Run() => RunLoop() => RunOnce() => PostRWProcess().
// Since RunLoop only iterately call the Run() method, so we don't test it
// directly. Therefore, we have 3-layer unittests:
//
// - HammerUpdaterFlowTest:
//  - Test the logic of Run(), the interaction with RunOnce().
//  - Mock RunOnce() and data members.
//
// - HammerUpdaterRWTest:
//  - Test the logic of RunOnce(), the interaction with PostRWProcess() and
//    external interfaces (fw_updater, pair_manager, ...etc).
//  - One exception: Test a special sequence that needs to reset 3 times called
//    by Run().
//  - Mock PostRWProcess() and data members.
//
// - HammerUpdaterPostRWTest:
//  - Test the logic of PostRWProcess, the interaction with external interfaces.
//  - Mock all external data members only. However, is currently not the status.
//    RunTouchpadUpdater is mocked and return success for all cases in class
//    HammerUpdaterPostRWTest. Refactor progress is tracking on b/65773038.
// TODO(kitching): Refactor out RO logic from HammerUpdaterPostRWTest as
//                    described in b/65773038.
//
// - HammerUpdaterRunTouchpadUpdaterTest:
//  - Test the return value if we can't get touchpad infomation
//  - Test the return value if update is failed during process.

#include <utility>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hammerd/hammer_updater.h"
#include "hammerd/mock_dbus_wrapper.h"
#include "hammerd/mock_pair_utils.h"
#include "hammerd/mock_update_fw.h"

using testing::_;
using testing::Assign;
using testing::AtLeast;
using testing::DoAll;
using testing::Exactly;
using testing::InSequence;
using testing::Return;
using testing::ReturnPointee;

namespace hammerd {

ACTION_P(Increment, n) {
  ++(*n);
}
ACTION_P(Decrement, n) {
  --(*n);
}

class MockRunOnceHammerUpdater : public HammerUpdater {
 public:
  using HammerUpdater::HammerUpdater;
  ~MockRunOnceHammerUpdater() override = default;

  MOCK_METHOD2(RunOnce, RunStatus(const bool post_rw_jump,
                                  const bool need_inject_entropy));
};

class MockRWProcessHammerUpdater : public HammerUpdater {
 public:
  using HammerUpdater::HammerUpdater;
  ~MockRWProcessHammerUpdater() override = default;

  MOCK_METHOD0(PostRWProcess, RunStatus());
};

class MockRunTouchpadUpdater : public HammerUpdater {
 public:
  using HammerUpdater::HammerUpdater;
  ~MockRunTouchpadUpdater() override = default;

  MOCK_METHOD0(RunTouchpadUpdater, RunStatus());
};

class MockNothing : public HammerUpdater {
 public:
  using HammerUpdater::HammerUpdater;
  ~MockNothing() override = default;
};

template <typename HammerUpdaterType>
class HammerUpdaterTest : public testing::Test {
 public:
  void SetUp() override {
    // Mock out data members.
    hammer_updater_.reset(new HammerUpdaterType{
        ec_image_,
        touchpad_image_,
        base::MakeUnique<MockFirmwareUpdater>(),
        base::MakeUnique<MockPairManagerInterface>(),
        base::MakeUnique<MockDBusWrapper>()});
    fw_updater_ =
        static_cast<MockFirmwareUpdater*>(hammer_updater_->fw_updater_.get());
    pair_manager_ = static_cast<MockPairManagerInterface*>(
        hammer_updater_->pair_manager_.get());
    dbus_wrapper_ = static_cast<MockDBusWrapper*>(
        hammer_updater_->dbus_wrapper_.get());

    // By default, expect no USB connections to be made. This can
    // be overridden by a call to ExpectUSBConnections.
    usb_connection_count_ = 0;
    EXPECT_CALL(*fw_updater_, TryConnectUSB()).Times(0);
    EXPECT_CALL(*fw_updater_, CloseUSB()).Times(0);
  }

  void TearDown() override { ASSERT_EQ(usb_connection_count_, 0); }

  void ExpectUSBConnections(const testing::Cardinality count) {
    // Checked in TearDown.
    EXPECT_CALL(*fw_updater_, TryConnectUSB())
        .Times(count)
        .WillRepeatedly(DoAll(Increment(&usb_connection_count_), Return(true)));
    EXPECT_CALL(*fw_updater_, CloseUSB())
        .Times(count)
        .WillRepeatedly(DoAll(Decrement(&usb_connection_count_), Return()));
  }

 protected:
  std::unique_ptr<HammerUpdaterType> hammer_updater_;
  MockFirmwareUpdater* fw_updater_;
  MockPairManagerInterface* pair_manager_;
  MockDBusWrapper* dbus_wrapper_;
  std::string ec_image_ = "MOCK EC IMAGE";
  std::string touchpad_image_ = "MOCK TOUCHPAD IMAGE";
  int usb_connection_count_;
};

// We mock RunOnce function here to verify the interaction between Run() and
// RunOnce().
class HammerUpdaterFlowTest
    : public HammerUpdaterTest<MockRunOnceHammerUpdater> {};
// We mock PostRWProcess function here to verify the flow of RW section
// updating.
class HammerUpdaterRWTest
    : public HammerUpdaterTest<MockRWProcessHammerUpdater> {};
// We mock RunTouchpadUpdater function here to verify the remaining flow.
class HammerUpdaterPostRWTest
    : public HammerUpdaterTest<MockRunTouchpadUpdater> {};
// Mock nothing to test HammerUpdaterRunTouchpadUpdaterTest
class HammerUpdaterRunTouchpadUpdaterTest
    : public HammerUpdaterTest<MockNothing> {};

// Failed to load EC_image.
TEST_F(HammerUpdaterFlowTest, Run_LoadECImageFailed) {
  EXPECT_CALL(*fw_updater_, LoadECImage(ec_image_)).WillOnce(Return(false));
  EXPECT_CALL(*fw_updater_, TryConnectUSB()).Times(0);
  EXPECT_CALL(*hammer_updater_, RunOnce(_, _)).Times(0);

  ASSERT_FALSE(hammer_updater_->Run());
}

// Sends reset command if RunOnce returns kNeedReset.
TEST_F(HammerUpdaterFlowTest, Run_AlwaysReset) {
  EXPECT_CALL(*fw_updater_, LoadECImage(ec_image_)).WillOnce(Return(true));
  EXPECT_CALL(*hammer_updater_, RunOnce(false, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(HammerUpdater::RunStatus::kNeedReset));
  EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kImmediateReset))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  ExpectUSBConnections(AtLeast(1));
  ASSERT_FALSE(hammer_updater_->Run());
}

// A fatal error occurred during update.
TEST_F(HammerUpdaterFlowTest, Run_FatalError) {
  EXPECT_CALL(*fw_updater_, LoadECImage(ec_image_)).WillOnce(Return(true));
  EXPECT_CALL(*hammer_updater_, RunOnce(false, _))
      .WillOnce(Return(HammerUpdater::RunStatus::kFatalError));
  EXPECT_CALL(*fw_updater_,
              SendSubcommand(UpdateExtraCommand::kImmediateReset))
      .WillOnce(Return(true));

  ExpectUSBConnections(AtLeast(1));
  ASSERT_FALSE(hammer_updater_->Run());
}

// After three attempts, Run reports no update needed.
TEST_F(HammerUpdaterFlowTest, Run_Reset3Times) {
  EXPECT_CALL(*fw_updater_, LoadECImage(ec_image_)).WillOnce(Return(true));
  EXPECT_CALL(*hammer_updater_, RunOnce(false, _))
      .WillOnce(Return(HammerUpdater::RunStatus::kNeedReset))
      .WillOnce(Return(HammerUpdater::RunStatus::kNeedReset))
      .WillOnce(Return(HammerUpdater::RunStatus::kNeedReset))
      .WillRepeatedly(Return(HammerUpdater::RunStatus::kNoUpdate));
  EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kImmediateReset))
      .Times(3)
      .WillRepeatedly(Return(true));

  ExpectUSBConnections(Exactly(4));
  ASSERT_TRUE(hammer_updater_->Run());
}

// Return false if the layout of the firmware is changed.
// Condition:
//   1. The current section is Invalid.
TEST_F(HammerUpdaterRWTest, RunOnce_InvalidSection) {
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::Invalid));

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
  }

  ASSERT_EQ(hammer_updater_->RunOnce(false, false),
            HammerUpdater::RunStatus::kInvalidFirmware);
}

// Update the RW after JUMP_TO_RW failed.
// Condition:
//   1. In RO section.
//   2. RW does not need update.
//   3. Fails to jump to RW due to invalid signature.
TEST_F(HammerUpdaterRWTest, Run_UpdateRWAfterJumpToRWFailed) {
  SectionName current_section = SectionName::RO;

  EXPECT_CALL(*fw_updater_, LoadECImage(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(ReturnPointee(&current_section));

  {
    InSequence dummy;

    // First round: RW does not need update.  Attempt to jump to RW.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kJumpToRW))
        .WillOnce(Return(true));

    // Second round: Jump to RW fails, so update RW.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*dbus_wrapper_, SendSignal(kBaseFirmwareUpdateStartedSignal));
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kStayInRO))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RW))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_,
                SendSubcommand(UpdateExtraCommand::kImmediateReset))
        .WillOnce(Return(true));

    // Third round: Again attempt to jump to RW.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kJumpToRW))
        .WillOnce(
            DoAll(Assign(&current_section, SectionName::RW), Return(true)));

    // Fourth round: Check that jumping to RW was successful, and that
    // PostRWProcessing is called.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*hammer_updater_, PostRWProcess());
    EXPECT_CALL(*dbus_wrapper_, SendSignal(kBaseFirmwareUpdateSucceededSignal));
  }

  ExpectUSBConnections(AtLeast(1));
  ASSERT_TRUE(hammer_updater_->Run());
}

// Send UpdateFailed DBus signal after continuous RW update failure.
// Condition:
//   1. In RO section.
//   2. RW needs update.
//   3. Always fails to update RW.
//   4. USB device disconnects after RunLoop.
TEST_F(HammerUpdaterRWTest, Run_UpdateRWFailed) {
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RO));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RW))
      .WillRepeatedly(Return(false));

  // Hammerd would try to update RW 10 times, so just use WillRepeatedly
  // instead of using InSequence.
  EXPECT_CALL(*fw_updater_, LoadECImage(_)).WillOnce(Return(true));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());
  EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kStayInRO))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kImmediateReset))
      .WillRepeatedly(Return(true));

  // USB losts connection after jumping out the RunLoop.
  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, TryConnectUSB())
        .Times(10)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*fw_updater_, TryConnectUSB())
        .WillOnce(Return(false));
  }
  EXPECT_CALL(*fw_updater_, CloseUSB())
      .Times(11)
      .WillRepeatedly(Return());

  // We should send UpdateStart and UpdateFailed DBus signal.
  EXPECT_CALL(*dbus_wrapper_, SendSignal(kBaseFirmwareUpdateStartedSignal));
  EXPECT_CALL(*dbus_wrapper_, SendSignal(kBaseFirmwareUpdateFailedSignal));

  ASSERT_FALSE(hammer_updater_->Run());
}

// Inject Entropy.
// Condition:
//   1. In RO section at the begining.
//   2. RW does not need update.
//   3. RW is not locked.
//   4. Pairing failed at the first time.
//   5. After injecting entropy successfully, pairing is successful
TEST_F(HammerUpdaterRWTest, Run_InjectEntropy) {
  SectionName current_section = SectionName::RO;

  EXPECT_CALL(*fw_updater_, LoadECImage(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(ReturnPointee(&current_section));

  {
    InSequence dummy;

    // First round: RW does not need update.  Attempt to jump to RW.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kJumpToRW))
        .WillOnce(DoAll(Assign(&current_section, SectionName::RW),
                        Return(true)));

    // Second round: Entering RW section, and need to inject entropy.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*hammer_updater_, PostRWProcess())
        .WillOnce(Return(HammerUpdater::RunStatus::kNeedInjectEntropy));
    EXPECT_CALL(*fw_updater_,
                SendSubcommand(UpdateExtraCommand::kImmediateReset))
        .WillOnce(DoAll(Assign(&current_section, SectionName::RO),
                        Return(true)));

    // Third round: Inject entropy and reset again.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kStayInRO))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, InjectEntropy()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_,
                SendSubcommand(UpdateExtraCommand::kImmediateReset))
        .WillOnce(Return(true));

    // Fourth round: Send JumpToRW.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kJumpToRW))
        .WillOnce(DoAll(Assign(&current_section, SectionName::RW),
                        Return(true)));

    // Fifth round: Pairing is successful.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*hammer_updater_, PostRWProcess())
        .WillOnce(Return(HammerUpdater::RunStatus::kNoUpdate));
  }

  ExpectUSBConnections(AtLeast(1));
  ASSERT_TRUE(hammer_updater_->Run());
}

// Update the RW and continue.
// Condition:
//   1. In RO section.
//   2. RW needs update.
//   3. RW is not locked.
TEST_F(HammerUpdaterRWTest, RunOnce_UpdateRW) {
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RO));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kStayInRO))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RW))
        .WillOnce(Return(true));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(false, false),
            HammerUpdater::RunStatus::kNeedReset);
}

// Unlock the RW and reset.
// Condition:
//   1. In RO section.
//   2. RW needs update.
//   3. RW is locked.
TEST_F(HammerUpdaterRWTest, RunOnce_UnlockRW) {
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RO));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kStayInRO))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, UnLockSection(SectionName::RW))
        .WillRepeatedly(Return(true));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(false, false),
            HammerUpdater::RunStatus::kNeedReset);
}

// Jump to RW.
// Condition:
//   1. In RO section.
//   2. RW does not need update.
TEST_F(HammerUpdaterRWTest, RunOnce_JumpToRW) {
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RO));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(false, false),
            HammerUpdater::RunStatus::kNeedJump);
}

// Complete RW jump.
// Condition:
//   1. In RW section.
//   2. RW jump flag is set.
TEST_F(HammerUpdaterRWTest, RunOnce_CompleteRWJump) {
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RW));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;

    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*hammer_updater_, PostRWProcess())
        .WillOnce(Return(HammerUpdater::RunStatus::kNoUpdate));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(true, false),
            HammerUpdater::RunStatus::kNoUpdate);
}

// Keep in RW.
// Condition:
//   1. In RW section.
//   2. RW does not need update.
TEST_F(HammerUpdaterRWTest, RunOnce_KeepInRW) {
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RW));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*hammer_updater_, PostRWProcess())
        .WillOnce(Return(HammerUpdater::RunStatus::kNoUpdate));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(false, false),
            HammerUpdater::RunStatus::kNoUpdate);
}

// Reset to RO.
// Condition:
//   1. In RW section.
//   2. RW needs update.
TEST_F(HammerUpdaterRWTest, RunOnce_ResetToRO) {
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RW));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*dbus_wrapper_, SendSignal(kBaseFirmwareUpdateStartedSignal));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(false, false),
            HammerUpdater::RunStatus::kNeedReset);
}

// Update working RW with incompatible key firmware.
// Under the situation RO (key1, v1) RW (key1, v1),
// invoke hammerd with (key2, v2).
// Should print: "RW section needs update, but local image is
// incompatible. Continuing to post-RW process; maybe RO can
// be updated."
// Condition:
//   1. In RW section.
//   2. RW needs update.
//   3. Local image key_version is incompatible.
TEST_F(HammerUpdaterRWTest, RunOnce_UpdateWorkingRWIncompatibleKey) {
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RW));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*hammer_updater_, PostRWProcess())
        .WillOnce(Return(HammerUpdater::RunStatus::kNoUpdate));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(false, false),
            HammerUpdater::RunStatus::kNoUpdate);
}

// Update corrupt RW with incompatible key firmware.
// Under the situation RO (key1, v1) RW (corrupt),
// invoke hammerd with (key2, v2).
// Should print: "RW section is unusable, but local image is
// incompatible. Giving up."
// Condition:
//   1. In RO section right after a failed JumpToRW.
//   2. RW needs update.
//   3. Local image key_version is incompatible.
TEST_F(HammerUpdaterRWTest, RunOnce_UpdateCorruptRWIncompatibleKey) {
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(Return(SectionName::RO));
  EXPECT_CALL(*fw_updater_, SendDone()).WillRepeatedly(Return());

  {
    InSequence dummy;
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
  }

  ASSERT_EQ(hammer_updater_->RunOnce(true, false),
            HammerUpdater::RunStatus::kFatalError);
}

// Successfully Pair with Hammer.
TEST_F(HammerUpdaterPostRWTest, Pairing_Passed) {
  // Short-circuit RO updating.
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
    .WillOnce(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  EXPECT_CALL(*pair_manager_, PairChallenge(fw_updater_))
      .WillOnce(Return(ChallengeStatus::kChallengePassed));
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kNoUpdate);
}

// Hammer needs to inject entropy, and rollback is locked.
TEST_F(HammerUpdaterPostRWTest, Pairing_NeedEntropyRollbackLocked) {
  // Short-circuit RO updating.
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
    .WillOnce(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  {
    InSequence dummy;
    EXPECT_CALL(*pair_manager_, PairChallenge(fw_updater_))
        .WillOnce(Return(ChallengeStatus::kNeedInjectEntropy));
    EXPECT_CALL(*fw_updater_, IsRollbackLocked())
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, UnLockRollback())
        .WillOnce(Return(true));
  }
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kNeedInjectEntropy);
}

// Hammer needs to inject entropy, and rollback is not locked.
TEST_F(HammerUpdaterPostRWTest, Pairing_NeedEntropyRollbackUnLocked) {
  // Short-circuit RO updating.
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
    .WillOnce(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  {
    InSequence dummy;
    EXPECT_CALL(*pair_manager_, PairChallenge(fw_updater_))
        .WillOnce(Return(ChallengeStatus::kNeedInjectEntropy));
    EXPECT_CALL(*fw_updater_, IsRollbackLocked())
        .WillOnce(Return(false));
  }
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kNeedInjectEntropy);
}

// Failed to pair with Hammer.
TEST_F(HammerUpdaterPostRWTest, Pairing_Failed) {
  // Short-circuit RO updating.
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
    .WillOnce(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  EXPECT_CALL(*pair_manager_, PairChallenge(fw_updater_))
      .WillOnce(Return(ChallengeStatus::kChallengeFailed));
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kFatalError);
}

// RO update is required and successful.
TEST_F(HammerUpdaterPostRWTest, ROUpdate_Passed) {
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(false));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kNeedReset);
}

// RO update is required and fails.
TEST_F(HammerUpdaterPostRWTest, ROUpdate_Failed) {
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(false));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RO))
      .WillOnce(Return(false));
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kNeedReset);
}

// RO update is not possible.
TEST_F(HammerUpdaterPostRWTest, ROUpdate_NotPossible) {
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RO))
      .Times(0);
  EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RO))
      .Times(0);
  EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
      .WillRepeatedly(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  EXPECT_CALL(*pair_manager_, PairChallenge(fw_updater_))
      .WillOnce(Return(ChallengeStatus::kChallengePassed));
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kNoUpdate);
}

// RO update is not needed.
TEST_F(HammerUpdaterPostRWTest, ROUpdate_NotNeeded) {
  EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
      .WillRepeatedly(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillOnce(Return(false));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RO))
      .WillOnce(Return(false));
  EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RO))
      .Times(0);
  EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
      .WillRepeatedly(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  EXPECT_CALL(*pair_manager_, PairChallenge(fw_updater_))
      .WillOnce(Return(ChallengeStatus::kChallengePassed));
  EXPECT_EQ(hammer_updater_->PostRWProcess(),
            HammerUpdater::RunStatus::kNoUpdate);
}

// Test updating to new key version on a dogfood device.
TEST_F(HammerUpdaterPostRWTest, Run_KeyVersionUpdate) {
  SectionName current_section = SectionName::RO;
  bool rw_version_mismatch = false;

  EXPECT_CALL(*fw_updater_, LoadECImage(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RW))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RW))
      .WillRepeatedly(ReturnPointee(&rw_version_mismatch));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RO))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, IsSectionLocked(SectionName::RW))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*fw_updater_, CurrentSection())
      .WillRepeatedly(ReturnPointee(&current_section));
  EXPECT_CALL(*pair_manager_, PairChallenge(fw_updater_))
      .WillRepeatedly(Return(ChallengeStatus::kChallengePassed));

  {
    InSequence dummy;

    // RW cannot be updated, since the key version is incorrect. Attempt to
    // jump to RW.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kJumpToRW))
        .WillOnce(
            DoAll(Assign(&current_section, SectionName::RW), Return(true)));

    // After jumping to RW, RO will be updated. Reset afterwards.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RO))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RO))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RO))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_,
                SendSubcommand(UpdateExtraCommand::kImmediateReset))
        .WillOnce(
            DoAll(Assign(&current_section, SectionName::RO),
                  Assign(&rw_version_mismatch, true),
                  Return(true)));

    // Hammer resets back into RO. Now the key version is correct, and
    // RW will be updated. Reset afterwards.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kStayInRO))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, TransferImage(SectionName::RW))
        .WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_,
                SendSubcommand(UpdateExtraCommand::kImmediateReset))
        .WillOnce(
            DoAll(Assign(&rw_version_mismatch, false), Return(true)));

    // Now both sections are updated. Jump from RO to RW.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, SendSubcommand(UpdateExtraCommand::kJumpToRW))
        .WillOnce(
            DoAll(Assign(&current_section, SectionName::RW), Return(true)));

    // Check that jumping to RW was successful.
    EXPECT_CALL(*fw_updater_, SendFirstPDU()).WillOnce(Return(true));
    EXPECT_CALL(*fw_updater_, SendDone()).WillOnce(Return());
    EXPECT_CALL(*fw_updater_, UpdatePossible(SectionName::RO))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*fw_updater_, VersionMismatch(SectionName::RO))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*hammer_updater_, RunTouchpadUpdater())
        .WillRepeatedly(Return(HammerUpdater::RunStatus::kTouchpadUpdated));
  }

  ExpectUSBConnections(AtLeast(1));
  ASSERT_EQ(hammer_updater_->Run(), true);
}

// Test the return value if we can't get touchpad infomation
TEST_F(HammerUpdaterRunTouchpadUpdaterTest, Run_FailToGetTouchpadInfo) {
  EXPECT_CALL(*fw_updater_, LoadTouchpadImage(touchpad_image_))
      .WillOnce(Return(true));
  EXPECT_CALL(*fw_updater_, SendSubcommandReceiveResponse(
      UpdateExtraCommand::kTouchpadInfo, "", _,
      sizeof(TouchpadInfo))).WillOnce(Return(false));

  ASSERT_EQ(hammer_updater_->RunTouchpadUpdater(),
            HammerUpdater::RunStatus::kNeedReset);
}

// Test the return value if update is failed during process.
TEST_F(HammerUpdaterRunTouchpadUpdaterTest, Run_FailToTransferFirmware) {
  EXPECT_CALL(*fw_updater_, LoadTouchpadImage(touchpad_image_))
      .WillOnce(Return(true));
  EXPECT_CALL(*fw_updater_, SendSubcommandReceiveResponse(
      UpdateExtraCommand::kTouchpadInfo, "", _,
      sizeof(TouchpadInfo))).WillOnce(Return(true));
  EXPECT_CALL(*fw_updater_, TransferTouchpadFirmware(_, _))
      .WillOnce(Return(false));

  ASSERT_EQ(hammer_updater_->RunTouchpadUpdater(),
            HammerUpdater::RunStatus::kFatalError);
}


}  // namespace hammerd
