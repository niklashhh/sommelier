// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA3_TEST_CAMERA3_MODULE_FIXTURE_H_
#define CAMERA3_TEST_CAMERA3_MODULE_FIXTURE_H_

#include <dlfcn.h>
#include <base/logging.h>
#include <base/synchronization/lock.h>
#include <gtest/gtest.h>
#include <hardware/camera3.h>

#include "camera3_test_thread.h"

namespace camera3_test {

const int kMaxNumCameras = 2;
const size_t kNumOfElementsInStreamConfigEntry = 4;
enum {
  STREAM_CONFIG_FORMAT_INDEX,
  STREAM_CONFIG_WIDTH_INDEX,
  STREAM_CONFIG_HEIGHT_INDEX,
  STREAM_CONFIG_DIRECTION_INDEX,
  STREAM_CONFIG_STALL_DURATION_INDEX = STREAM_CONFIG_DIRECTION_INDEX,
  STREAM_CONFIG_MIN_DURATION_INDEX = STREAM_CONFIG_DIRECTION_INDEX
};

class ResolutionInfo {
 public:
  ResolutionInfo(int32_t width, int32_t height)
      : width_(width), height_(height) {}

  int32_t Width() const;

  int32_t Height() const;

  int32_t Area() const;

  bool operator==(const ResolutionInfo& resolution) const;

  bool operator<(const ResolutionInfo& resolution) const;

 private:
  int32_t width_, height_;
};

class Camera3Module {
 public:
  Camera3Module();

  // Initialize
  int Initialize();

  // Get number of cameras
  int GetNumberOfCameras();

  // Get list of camera IDs
  std::vector<int> GetCameraIds();

  // Open camera device
  camera3_device* OpenDevice(int cam_id);

  // Close camera device
  int CloseDevice(camera3_device& cam_device);

  // Check if a stream format is supported
  bool IsFormatAvailable(int cam_id, int format);

  // Get camera information
  int GetCameraInfo(int cam_id, camera_info* info);

  // Get the image output formats in this stream configuration
  std::vector<int32_t> GetOutputFormats(int cam_id);

  // Get the image output resolutions in this stream configuration
  std::vector<ResolutionInfo> GetSortedOutputResolutions(int cam_id,
                                                         int32_t format);

  // Get the stall duration for the format/size combination (in nanoseconds)
  int64_t GetOutputStallDuration(int cam_id,
                                 int32_t format,
                                 const ResolutionInfo& resolution);

  //  Get the minimum frame duration
  int64_t GetOutputMinFrameDuration(int cam_id,
                                    int32_t format,
                                    const ResolutionInfo& resolution);

 private:
  void GetNumberOfCamerasOnHalThread(int* result);

  void GetCameraInfoOnHalThread(int cam_id, camera_info* info, int* result);

  void OpenDeviceOnHalThread(int cam_id, camera3_device_t** cam_device);

  void CloseDeviceOnHalThread(camera3_device_t* cam_device, int* result);

  void GetStreamConfigEntry(int cam_id,
                            int32_t key,
                            camera_metadata_ro_entry_t* entry);

  int64_t GetOutputKeyParameterI64(int cam_id,
                                   int32_t format,
                                   const ResolutionInfo& resolution,
                                   int32_t key,
                                   int32_t index);

  const camera_module_t* cam_module_;

  // This thread is needed because of the ARC++ HAL assumption that all the
  // camera_module functions should be called on the same Chromium thread.
  // It is expected to start this thread before gtest initialization in main()
  // because test case instantiation needs it running to get the camera ID
  // list.
  Camera3TestThread* hal_thread_;

  DISALLOW_COPY_AND_ASSIGN(Camera3Module);
};

class Camera3ModuleFixture : public testing::Test {
 public:
  Camera3ModuleFixture() {}

  virtual void SetUp() override;

 protected:
  Camera3Module cam_module_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Camera3ModuleFixture);
};

}  // namespace camera3_test

#endif  // CAMERA3_TEST_CAMERA3_MODULE_FIXTURE_H_