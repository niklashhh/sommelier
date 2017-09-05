// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HAMMERD_MOCK_UPDATE_FW_H_
#define HAMMERD_MOCK_UPDATE_FW_H_

#include <string>

#include <gmock/gmock.h>

#include "hammerd/update_fw.h"

namespace hammerd {

class MockFirmwareUpdater : public FirmwareUpdaterInterface {
 public:
  MockFirmwareUpdater() = default;

  MOCK_METHOD1(LoadECImage, bool(const std::string& ec_image));
  MOCK_METHOD1(LoadTouchpadImage, bool(const std::string& touchpad_image));
  MOCK_METHOD0(TryConnectUSB, bool());
  MOCK_METHOD0(CloseUSB, void());
  MOCK_METHOD0(SendFirstPDU, bool());
  MOCK_METHOD0(SendDone, void());
  MOCK_METHOD0(InjectEntropy, bool());
  MOCK_METHOD1(SendSubcommand, bool(UpdateExtraCommand subcommand));
  MOCK_METHOD2(SendSubcommandWithPayload,
               bool(UpdateExtraCommand subcommand,
                    const std::string& cmd_body));
  MOCK_METHOD4(SendSubcommandReceiveResponse,
               bool(UpdateExtraCommand subcommand,
                    const std::string& cmd_body,
                    void* resp,
                    size_t resp_size));
  MOCK_METHOD1(TransferImage, bool(SectionName section_name));
  MOCK_METHOD2(TransferTouchpadFirmware,
               bool(uint32_t section_addr, size_t data_len));
  MOCK_CONST_METHOD0(CurrentSection, SectionName());
  MOCK_CONST_METHOD1(UpdatePossible, bool(SectionName section_name));
  MOCK_CONST_METHOD1(VersionMismatch, bool(SectionName section_name));
  MOCK_CONST_METHOD1(IsSectionLocked, bool(SectionName section_name));
  MOCK_METHOD1(UnLockSection, bool(SectionName section_name));
  MOCK_CONST_METHOD0(IsRollbackLocked, bool());
  MOCK_METHOD0(UnLockRollback, bool());
};

}  // namespace hammerd
#endif  // HAMMERD_MOCK_UPDATE_FW_H_
