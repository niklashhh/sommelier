// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hidapi/hidapi.h>
#include <memory>
#include <vector>

#include "u2fd/g2f_tools/g2f_client.h"

namespace {

hid_device* kDummyDevice = reinterpret_cast<hid_device*>(0xdeadbeef);

constexpr char kDummyDeviceName[] = "DummyDeviceName";

constexpr char kDummySingleResponse[] =
    "AABBCCDD860008DEADBEEF000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000";

constexpr char kDummyLargeResponse[] =
    "AABBCCDD860050DEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDE"
    "DEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDE";

constexpr char kDummyLargeResponseCont[] =
    "AABBCCDD00DEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDE"
    "DEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDEDE";

constexpr size_t kRwBufSize = u2f::kU2fReportSize * 4;

int hid_open_called;
int hid_close_called;

unsigned char hid_write_data[kRwBufSize];
int hid_write_count;

unsigned char hid_read_data[kRwBufSize];
int hid_read_count;

bool hid_read_fail_timeout = false;

}  // namespace

hid_device* hid_open_path(const char* path) {
  EXPECT_THAT(path, ::testing::StrEq(kDummyDeviceName));
  hid_open_called++;
  return kDummyDevice;
}

void hid_close(hid_device* device) {
  EXPECT_EQ(device, kDummyDevice);
  hid_close_called++;
}

int hid_write(hid_device* device, const unsigned char* data, size_t length) {
  EXPECT_EQ(device, kDummyDevice);
  length = std::min(length, kRwBufSize - hid_write_count);
  memcpy(hid_write_data + hid_write_count, data, length);
  hid_write_count += length;
  return length;
}

int hid_read_timeout(hid_device* device, unsigned char* data, size_t length,
                     int milliseconds) {
  length = std::min(length, kRwBufSize - hid_read_count);
  if (hid_read_fail_timeout) {
    // sleep
  }
  EXPECT_EQ(device, kDummyDevice);
  memcpy(data, hid_read_data + hid_read_count, length);
  hid_read_count += length;
  return length;
}

const wchar_t* hid_error(hid_device* device) {
  return nullptr;
}

namespace g2f_client {
namespace {

using ::testing::_;
using ::testing::ByRef;
using ::testing::ContainerEq;
using ::testing::DoAll;
using ::testing::Each;
using ::testing::Eq;
using ::testing::ElementsAre;
using ::testing::InvokeWithoutArgs;
using ::testing::MatcherCast;
using ::testing::MatchesRegex;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrEq;

static HidDevice::Cid kDummyCid = { { 0xAA, 0xBB, 0xCC, 0xDD } };

class G2fClientTest : public ::testing::Test {
 public:
  G2fClientTest() =  default;

  virtual ~G2fClientTest() = default;

  void SetUp() {
    hid_open_called = 0;
    hid_close_called = 0;
    memset(hid_write_data, 0, kRwBufSize);
    hid_write_count = 0;
    memset(hid_read_data, 0, kRwBufSize);
    hid_read_count = 0;
    hid_read_fail_timeout = false;
    device_.reset(new HidDevice(kDummyDeviceName));
  }

 protected:
  std::unique_ptr<HidDevice> device_;

 private:
  DISALLOW_COPY_AND_ASSIGN(G2fClientTest);
};

TEST_F(G2fClientTest, HidDeviceOpenClose) {
  // Sanity check.
  EXPECT_EQ(0, hid_open_called);
  EXPECT_EQ(0, hid_close_called);

  EXPECT_FALSE(device_->IsOpened());
  EXPECT_TRUE(device_->Open());
  EXPECT_TRUE(device_->IsOpened());

  EXPECT_EQ(1, hid_open_called);
  EXPECT_EQ(0, hid_close_called);

  // Does not open multiple times.
  EXPECT_TRUE(device_->Open());
  EXPECT_TRUE(device_->IsOpened());

  EXPECT_EQ(1, hid_open_called);
  EXPECT_EQ(0, hid_close_called);

  device_->Close();
  EXPECT_FALSE(device_->IsOpened());

  EXPECT_EQ(1, hid_open_called);
  EXPECT_EQ(1, hid_close_called);
}

TEST_F(G2fClientTest, HidDeviceSendWithoutOpen) {
  brillo::Blob payload;
  EXPECT_FALSE(device_->SendRequest(kDummyCid, 0, payload));
}

TEST_F(G2fClientTest, HidDeviceRecvWithoutOpen) {
  uint8_t cmd;
  brillo::Blob payload;
  EXPECT_FALSE(device_->RecvResponse(kDummyCid, &cmd, &payload, 0));
}

TEST_F(G2fClientTest, HidDeviceSend) {
  EXPECT_TRUE(device_->Open());

  brillo::Blob payload { 0xDD, 0xFF };
  EXPECT_TRUE(device_->SendRequest(kDummyCid, 0xAB, payload));

  EXPECT_THAT(
      base::HexEncode(hid_write_data, hid_write_count),
      MatchesRegex(".*"
                   "AABBCCDD.*"  // Cid
                   "AB.*"        // Command
                   "DDFF.*"));   // Payload
}

TEST_F(G2fClientTest, HidDeviceSendMultipleFrames) {
  EXPECT_TRUE(device_->Open());

  brillo::Blob payload;
  for (int i = 0; i < u2f::kU2fReportSize * 2; i++) {
    payload.push_back(i);
  }
  EXPECT_TRUE(device_->SendRequest(kDummyCid, 0xAB, payload));

  EXPECT_THAT(
      base::HexEncode(hid_write_data, hid_write_count),
      MatchesRegex(".*"
                   "AABBCCDD.*"   // Cid
                   "AB.*"         // Command
                   "010203.*"     // Payload
                   // Second frame:
                   "AABBCCDD.*"   // Cid
                   "01.*"         // Cont
                   "747576.*"));  // Payload
}

TEST_F(G2fClientTest, HidDeviceSendTooLarge) {
  EXPECT_TRUE(device_->Open());

  brillo::Blob payload(UINT16_MAX + 1, 0);
  EXPECT_FALSE(device_->SendRequest(kDummyCid, 0xAB, payload));
}


TEST_F(G2fClientTest, HidDeviceSendWriteFails) {
  EXPECT_TRUE(device_->Open());

  // Pretend the whole buffer has been read already;
  // subsequent reads will fail.
  hid_write_count = sizeof(hid_write_data);

  brillo::Blob payload(10, 0);
  EXPECT_FALSE(device_->SendRequest(kDummyCid, 0xAB, payload));
}

namespace {

void HexStringToBuffer(const char* str, unsigned char** dest) {
  std::vector<uint8_t> bytes;
  CHECK(base::HexStringToBytes(str, &bytes));
  std::copy(bytes.begin(), bytes.end(), *dest);
  *dest += bytes.size();
}

}  // namespace

TEST_F(G2fClientTest, HidDeviceRecvResponse) {
  unsigned char* dest = hid_read_data;
  HexStringToBuffer(kDummySingleResponse, &dest);

  uint8_t cmd;
  brillo::Blob payload;

  EXPECT_TRUE(device_->Open());
  EXPECT_TRUE(device_->RecvResponse(kDummyCid, &cmd, &payload, 10));

  EXPECT_THAT(payload, ElementsAre(0xDE, 0xAD, 0xBE, 0xEF,
                                   0, 0, 0, 0));
}

TEST_F(G2fClientTest, HidDeviceRecvResponseMultiPart) {
  unsigned char* dest = hid_read_data;
  HexStringToBuffer(kDummyLargeResponse, &dest);
  HexStringToBuffer(kDummyLargeResponseCont, &dest);

  uint8_t cmd;
  brillo::Blob payload;

  EXPECT_TRUE(device_->Open());
  EXPECT_TRUE(device_->RecvResponse(kDummyCid, &cmd, &payload, 10));

  EXPECT_EQ(0x50, payload.size());
  EXPECT_THAT(payload, Each(0xDE));
}

TEST_F(G2fClientTest, HidDeviceRecvResponseUnexpectedInit) {
  unsigned char* dest = hid_read_data;
  HexStringToBuffer(kDummyLargeResponse, &dest);
  HexStringToBuffer(kDummyLargeResponse, &dest);

  uint8_t cmd;
  brillo::Blob payload;

  EXPECT_TRUE(device_->Open());
  EXPECT_FALSE(device_->RecvResponse(kDummyCid, &cmd, &payload, 10));
}

TEST_F(G2fClientTest, HidDeviceRecvResponseUnexpectedCont) {
  unsigned char* dest = hid_read_data;
  HexStringToBuffer(kDummyLargeResponseCont, &dest);

  uint8_t cmd;
  brillo::Blob payload;

  EXPECT_TRUE(device_->Open());
  EXPECT_FALSE(device_->RecvResponse(kDummyCid, &cmd, &payload, 10));
}

TEST_F(G2fClientTest, HidDeviceRecvResponseUnexpectedChannel) {
  unsigned char* dest = hid_read_data;
  HexStringToBuffer(kDummySingleResponse, &dest);

  uint8_t cmd;
  brillo::Blob payload;

  EXPECT_TRUE(device_->Open());
  EXPECT_FALSE(device_->RecvResponse({ 0xFF, 0xFF, 0xFF, 0xFF },
                                     &cmd, &payload, 10));
}

TEST_F(G2fClientTest, HidDeviceRecvResponseUnexpectedSeq) {
  unsigned char* dest = hid_read_data;
  HexStringToBuffer(kDummyLargeResponse, &dest);
  HexStringToBuffer(kDummyLargeResponseCont, &dest);

  hid_read_data[4] = 7;

  uint8_t cmd;
  brillo::Blob payload;

  EXPECT_TRUE(device_->Open());
  EXPECT_FALSE(device_->RecvResponse(kDummyCid, &cmd, &payload, 10));
}

TEST_F(G2fClientTest, HidDeviceRecvResponseReadFail) {
  // Simulate having read all data; subsequent reads will fail.
  hid_read_count = sizeof(hid_read_data);

  uint8_t cmd;
  brillo::Blob payload;

  EXPECT_TRUE(device_->Open());
  EXPECT_FALSE(device_->RecvResponse(kDummyCid, &cmd, &payload, 10));
}

class MockHidDevice : public HidDevice {
 public:
  MockHidDevice() : HidDevice("unused") {}
  MOCK_CONST_METHOD0(IsOpened, bool());
  MOCK_METHOD0(Open, bool());
  MOCK_METHOD0(Close, void());
  MOCK_METHOD3(SendRequest,
               bool(const Cid& cid, uint8_t cmd, const brillo::Blob& payload));
  MOCK_METHOD4(RecvResponse, bool(const Cid& cid, uint8_t* cmd,
                                  brillo::Blob* payload, int timeout_ms));
};

class U2FHidTest : public ::testing::Test {
 public:
  U2FHidTest() : hid_(&device_) {}
  virtual ~U2FHidTest() = default;

  void SetUp() {
  }

 protected:
  void ExpectInit(bool copy_nonce,
                  const char* cid,
                  const char* version,
                  const char* caps);

  void ExpectDefaultInit() {
    ExpectInit(true, "AABBCCDD", "00000000", "00");
  }

  void ExpectMsg(U2FHid::CommandCode cmd, bool echo_req);

  MockHidDevice device_;
  U2FHid hid_;

  U2FHid::Command request;
  U2FHid::Command response;
  brillo::Blob nonce;

 private:
  DISALLOW_COPY_AND_ASSIGN(U2FHidTest);
};

MATCHER(IsBroadcastCid, "Matches the broadcast cid.") {
  return arg.raw[0] == 0xFF &&
      arg.raw[1] == 0xFF &&
      arg.raw[2] == 0xFF &&
      arg.raw[3] == 0xFF;
}

MATCHER_P(EqCommandCode, value, "Matches the specified command code") {
  return arg == static_cast<uint8_t>(value);
}

ACTION_P4(PrepareInitResponse, copy_nonce, req, resp, str) {
  if (copy_nonce)
    *resp = *req;
  std::vector<uint8_t> bytes;
  LOG(ERROR) << str;
  CHECK(base::HexStringToBytes(str, &bytes));
  std::copy(bytes.begin(), bytes.end(), std::back_inserter(*resp));
}

void U2FHidTest::ExpectInit(bool copy_nonce,
                            const char* cid,
                            const char* version,
                            const char* caps) {
  EXPECT_CALL(device_, Open()).WillOnce(Return(true));

  std::string resp = base::StringPrintf("%s%s%s",
                                        cid,
                                        version,
                                        caps);

  EXPECT_CALL(device_, SendRequest(
      IsBroadcastCid(),
      EqCommandCode(U2FHid::CommandCode::kInit),
      _))
      .WillOnce(DoAll(
          SaveArg<2>(&request.payload),
          PrepareInitResponse(copy_nonce,
                              &request.payload, &response.payload,
                              resp),
          Return(true)));

  EXPECT_CALL(device_, RecvResponse(_, _, _, _))
      .WillOnce(DoAll(
          SetArgPointee<1>(static_cast<uint8_t>(U2FHid::CommandCode::kInit)),
          SetArgPointee<2>(ByRef(response.payload)),
          Return(true)));
}

void U2FHidTest::ExpectMsg(U2FHid::CommandCode cmd, bool echo_req) {
  EXPECT_CALL(device_, Open()).WillOnce(Return(true));

  EXPECT_CALL(device_, SendRequest(
      _,
      EqCommandCode(cmd),
      _))
      .WillOnce(DoAll(
          SaveArg<2>(&request.payload),
          InvokeWithoutArgs([this, echo_req]() {
                              if (echo_req)
                                response.payload = request.payload;
                              else
                                response.payload.clear();
                            }),
          Return(true)));

  EXPECT_CALL(device_, RecvResponse(_, _, _, _))
      .WillOnce(DoAll(
          SetArgPointee<1>(static_cast<uint8_t>(cmd)),
          SetArgPointee<2>(ByRef(response.payload)),
          Return(true)));
}

TEST_F(U2FHidTest, Init) {
  ExpectInit(true,        // Copy nonce
             "AABBCCDD",  // Cid
             "00000000",  // Version
             "DE");       // Caps
  EXPECT_TRUE(hid_.Init(false));
  EXPECT_TRUE(hid_.Initialized());
  EXPECT_EQ(00, hid_.GetVersion().protocol);
  EXPECT_EQ(0xDE, hid_.GetCaps());

  // Calling again does not re-initialize.
  EXPECT_TRUE(hid_.Init(false));
  EXPECT_TRUE(hid_.Initialized());

  ExpectInit(true,        // Copy nonce
             "AABBCCDD",  // Cid
             "DEADBEEF",  // Version
             "AF");       // Caps

  // Force re-initialization
  EXPECT_TRUE(hid_.Init(true));
  EXPECT_TRUE(hid_.Initialized());
  EXPECT_EQ(0xEF, hid_.GetVersion().build);
  EXPECT_EQ(0xAF, hid_.GetCaps());
}

TEST_F(U2FHidTest, InitBadResponseSize) {
  ExpectInit(true,        // Copy nonce
             "AABBCCDD",  // Cid
             "00",        // Version - too short!
             "DE");       // Caps
  EXPECT_FALSE(hid_.Init(false));
  EXPECT_FALSE(hid_.Initialized());
}

TEST_F(U2FHidTest, InitBadNonce) {
  ExpectInit(false,       // Copy nonce
             "0000000000000000"   // Incorrect nonce (prepend to cid)
             "AABBCCDD",  // Cid
             "00000000",  // Version - too short!
             "DE");       // Caps
  EXPECT_FALSE(hid_.Init(false));
  EXPECT_FALSE(hid_.Initialized());
}

TEST_F(U2FHidTest, InitSendError) {
  EXPECT_CALL(device_, Open()).WillOnce(Return(true));
  EXPECT_CALL(device_, SendRequest(_, _, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(hid_.Init(false));
  EXPECT_FALSE(hid_.Initialized());
}

TEST_F(U2FHidTest, InitRecvError) {
  EXPECT_CALL(device_, Open()).WillOnce(Return(true));
  EXPECT_CALL(device_, SendRequest(
      IsBroadcastCid(),
      EqCommandCode(U2FHid::CommandCode::kInit),
      _))
      .WillOnce(Return(true));
  EXPECT_CALL(device_, RecvResponse(_, _, _, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(hid_.Init(false));
  EXPECT_FALSE(hid_.Initialized());
}

TEST_F(U2FHidTest, Lock) {
  ExpectDefaultInit();
  EXPECT_TRUE(hid_.Init(false));

  ExpectMsg(U2FHid::CommandCode::kLock, false);
  EXPECT_TRUE(hid_.Lock(10));
}

TEST_F(U2FHidTest, Msg) {
  brillo::Blob request = { 1, 2, 3, 4, 5 };
  brillo::Blob response;

  ExpectDefaultInit();
  EXPECT_TRUE(hid_.Init(false));

  ExpectMsg(U2FHid::CommandCode::kMsg, true);
  EXPECT_TRUE(hid_.Msg(request, &response));
  EXPECT_THAT(response, ContainerEq(request));
}

TEST_F(U2FHidTest, Ping) {
  ExpectDefaultInit();
  EXPECT_TRUE(hid_.Init(false));

  ExpectMsg(U2FHid::CommandCode::kPing, true);
  EXPECT_TRUE(hid_.Ping(10));
}

TEST_F(U2FHidTest, Wink) {
  ExpectDefaultInit();
  EXPECT_TRUE(hid_.Init(false));

  ExpectMsg(U2FHid::CommandCode::kWink, true);
  EXPECT_TRUE(hid_.Wink());
}

}  // namespace
}  // namespace g2f_client
