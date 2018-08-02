// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/nvram_boot_lockbox.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <cryptohome/bootlockbox/fake_tpm_nvspace_utility.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
  const base::FilePath::CharType kTestFilePath[] =
    FILE_PATH_LITERAL("test_file_path.pb");
}

namespace cryptohome {

class NVRamBootLockboxTest : public testing::Test {
 public:
  void SetUp() override {
    base::ScopedTempDir temp_directory;
    ASSERT_TRUE(temp_directory.CreateUniqueTempDir());
    file_path_ = temp_directory.path().Append(kTestFilePath);
    nvram_boot_lockbox_ =
        std::make_unique<NVRamBootLockbox>(&fake_tpm_nvspace_utility_,
                                           file_path_);
  }
 protected:
  FakeTpmNVSpaceUtility fake_tpm_nvspace_utility_;
  std::unique_ptr<NVRamBootLockbox> nvram_boot_lockbox_;
  base::FilePath file_path_;
};

TEST_F(NVRamBootLockboxTest, Finalize) {
  EXPECT_TRUE(nvram_boot_lockbox_->Finalize());
  EXPECT_EQ(nvram_boot_lockbox_->GetState(), NVSpaceState::kNVSpaceWriteLocked);
}

TEST_F(NVRamBootLockboxTest, DefineSpace) {
  EXPECT_TRUE(nvram_boot_lockbox_->DefineSpace());
  EXPECT_EQ(nvram_boot_lockbox_->GetState(),
            NVSpaceState::kNVSpaceUninitialized);
}

TEST_F(NVRamBootLockboxTest, StoreFail) {
  std::string key = "test_key";
  std::string value ="test_value";
  EXPECT_TRUE(nvram_boot_lockbox_->Finalize());
  EXPECT_FALSE(nvram_boot_lockbox_->Store(key, value));
}

TEST_F(NVRamBootLockboxTest, LoadFailDigestMisMatch) {
  std::string key = "test_key";
  std::string value = "test_value";
  EXPECT_TRUE(nvram_boot_lockbox_->Store(key, value));
  // modify the proto file.
  std::string invalid_proto = "aaa";
  base::WriteFile(file_path_, invalid_proto.c_str(),
                  invalid_proto.size());
  EXPECT_FALSE(nvram_boot_lockbox_->Load());
}

TEST_F(NVRamBootLockboxTest, StoreLoadReadSuccess) {
  std::string key = "test_key";
  std::string value = "test_value_digest";
  EXPECT_TRUE(nvram_boot_lockbox_->Store(key, value));
  EXPECT_TRUE(nvram_boot_lockbox_->Load());
  std::string stored_value;
  EXPECT_TRUE(nvram_boot_lockbox_->Read(key, &stored_value));
  EXPECT_EQ(value, stored_value);
  EXPECT_FALSE(nvram_boot_lockbox_->Read("non-exist-key", &stored_value));
}

}  // namespace cryptohome