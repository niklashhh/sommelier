// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_module_fixture.h"

#include <algorithm>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/command_line.h>
#include <base/logging.h>

namespace camera3_test {

static camera_module_t* g_cam_module = NULL;
static Camera3TestThread g_module_thread("Camera3 Test Module Thread");

int32_t ResolutionInfo::Width() const {
  return width_;
}

int32_t ResolutionInfo::Height() const {
  return height_;
}

int32_t ResolutionInfo::Area() const {
  return width_ * height_;
}

bool ResolutionInfo::operator==(const ResolutionInfo& resolution) const {
  return (width_ == resolution.Width()) && (height_ == resolution.Height());
}

bool ResolutionInfo::operator<(const ResolutionInfo& resolution) const {
  // Compare by area it covers, if the areas are same, then compare the widths.
  return (Area() < resolution.Area()) ||
         (Area() == resolution.Area() && width_ < resolution.Width());
}

static void InitCameraModule(void*& cam_hal_handle) {
  const char kCameraHalDllName[] = "camera_hal.so";
  cam_hal_handle = dlopen(kCameraHalDllName, RTLD_NOW);
  ASSERT_NE(nullptr, cam_hal_handle) << "Failed to dlopen: " << dlerror();

  camera_module_t* cam_module = static_cast<camera_module_t*>(
      dlsym(cam_hal_handle, HAL_MODULE_INFO_SYM_AS_STR));
  ASSERT_NE(nullptr, cam_module) << "Camera module is invalid";
  ASSERT_NE(nullptr, cam_module->get_number_of_cameras)
      << "get_number_of_cameras is not implemented";
  ASSERT_NE(nullptr, cam_module->get_camera_info)
      << "get_camera_info is not implemented";
  ASSERT_NE(nullptr, cam_module->common.methods->open)
      << "open() is unimplemented";
  g_cam_module = cam_module;
}

static camera_module_t* GetCameraModule() {
  return g_cam_module;
}

// Camera module

Camera3Module::Camera3Module()
    : cam_module_(GetCameraModule()), hal_thread_(&g_module_thread) {}

int Camera3Module::Initialize() {
  return cam_module_ ? 0 : -ENODEV;
}

int Camera3Module::GetNumberOfCameras() {
  if (!cam_module_) {
    return -ENODEV;
  }
  int result = -EINVAL;
  hal_thread_->PostTaskSync(
      base::Bind(&Camera3Module::GetNumberOfCamerasOnHalThread,
                 base::Unretained(this), &result));
  return result;
}

std::vector<int> Camera3Module::GetCameraIds() {
  if (!cam_module_) {
    return std::vector<int>();
  }

  int num_cams = GetNumberOfCameras();
  std::vector<int> ids(num_cams);
  for (int i = 0; i < num_cams; i++) {
    ids[i] = i;
  }

  return ids;
}

void Camera3Module::GetStreamConfigEntry(int cam_id,
                                         int32_t key,
                                         camera_metadata_ro_entry_t* entry) {
  entry->count = 0;

  camera_info info;
  ASSERT_EQ(0, GetCameraInfo(cam_id, &info)) << "Can't get camera info for"
                                             << cam_id;

  camera_metadata_ro_entry_t local_entry = {};
  ASSERT_EQ(
      0, find_camera_metadata_ro_entry(
             const_cast<camera_metadata_t*>(info.static_camera_characteristics),
             key, &local_entry))
      << "Fail to find metadata key " << get_camera_metadata_tag_name(key);
  ASSERT_NE(0u, local_entry.count) << "Camera stream configuration is empty";
  ASSERT_EQ(0u, local_entry.count % kNumOfElementsInStreamConfigEntry)
      << "Camera stream configuration parsing error";
  *entry = local_entry;
}

bool Camera3Module::IsFormatAvailable(int cam_id, int format) {
  if (!cam_module_) {
    return false;
  }

  camera_metadata_ro_entry_t available_config = {};
  GetStreamConfigEntry(cam_id, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                       &available_config);

  for (uint32_t i = 0; i < available_config.count;
       i += kNumOfElementsInStreamConfigEntry) {
    if (available_config.data.i32[i + STREAM_CONFIG_FORMAT_INDEX] == format) {
      return true;
    }
  }

  return false;
}

camera3_device* Camera3Module::OpenDevice(int cam_id) {
  if (!cam_module_) {
    return NULL;
  }
  camera3_device_t* cam_device = nullptr;
  hal_thread_->PostTaskSync(base::Bind(&Camera3Module::OpenDeviceOnHalThread,
                                       base::Unretained(this), cam_id,
                                       &cam_device));
  return cam_device;
}

int Camera3Module::CloseDevice(camera3_device& cam_device) {
  if (!cam_module_) {
    return -ENODEV;
  }
  int result = -ENODEV;
  hal_thread_->PostTaskSync(base::Bind(&Camera3Module::CloseDeviceOnHalThread,
                                       base::Unretained(this), &cam_device,
                                       &result));
  return result;
}

int Camera3Module::GetCameraInfo(int cam_id, camera_info* info) {
  if (!cam_module_) {
    return -ENODEV;
  }
  int result = -ENODEV;
  hal_thread_->PostTaskSync(base::Bind(&Camera3Module::GetCameraInfoOnHalThread,
                                       base::Unretained(this), cam_id, info,
                                       &result));
  return result;
}

std::vector<int32_t> Camera3Module::GetOutputFormats(int cam_id) {
  if (!cam_module_) {
    return std::vector<int32_t>();
  }

  camera_metadata_ro_entry_t available_config = {};
  GetStreamConfigEntry(cam_id, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                       &available_config);

  std::set<int32_t> available_formats;
  for (uint32_t i = 0; i < available_config.count;
       i += kNumOfElementsInStreamConfigEntry) {
    if (available_config.data.i32[i + STREAM_CONFIG_DIRECTION_INDEX] ==
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
      available_formats.insert(
          available_config.data.i32[i + STREAM_CONFIG_FORMAT_INDEX]);
    }
  }

  return std::vector<int32_t>(available_formats.begin(),
                              available_formats.end());
}

std::vector<ResolutionInfo> Camera3Module::GetSortedOutputResolutions(
    int cam_id,
    int32_t format) {
  if (!cam_module_) {
    return std::vector<ResolutionInfo>();
  }

  camera_metadata_ro_entry_t available_config = {};
  GetStreamConfigEntry(cam_id, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                       &available_config);

  std::vector<ResolutionInfo> available_resolutions;
  for (uint32_t i = 0; i < available_config.count;
       i += kNumOfElementsInStreamConfigEntry) {
    int32_t fmt = available_config.data.i32[i + STREAM_CONFIG_FORMAT_INDEX];
    int32_t width = available_config.data.i32[i + STREAM_CONFIG_WIDTH_INDEX];
    int32_t height = available_config.data.i32[i + STREAM_CONFIG_HEIGHT_INDEX];
    int32_t in_or_out =
        available_config.data.i32[i + STREAM_CONFIG_DIRECTION_INDEX];
    if ((fmt == format) &&
        (in_or_out == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)) {
      available_resolutions.emplace_back(width, height);
    }
  }
  std::sort(available_resolutions.begin(), available_resolutions.end());
  return available_resolutions;
}

int64_t Camera3Module::GetOutputKeyParameterI64(
    int cam_id,
    int32_t format,
    const ResolutionInfo& resolution,
    int32_t key,
    int32_t index) {
  if (!cam_module_) {
    return -EINVAL;
  }

  camera_metadata_ro_entry_t available_config = {};
  GetStreamConfigEntry(cam_id, key, &available_config);

  for (uint32_t i = 0; i < available_config.count;
       i += kNumOfElementsInStreamConfigEntry) {
    int64_t fmt = available_config.data.i64[i + STREAM_CONFIG_FORMAT_INDEX];
    int64_t width = available_config.data.i64[i + STREAM_CONFIG_WIDTH_INDEX];
    int64_t height = available_config.data.i64[i + STREAM_CONFIG_HEIGHT_INDEX];
    if (fmt == format && width == resolution.Width() &&
        height == resolution.Height()) {
      return available_config.data.i64[i + index];
    }
  }

  return -EINVAL;
}

int64_t Camera3Module::GetOutputStallDuration(
    int cam_id,
    int32_t format,
    const ResolutionInfo& resolution) {
  return GetOutputKeyParameterI64(cam_id, format, resolution,
                                  ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
                                  STREAM_CONFIG_STALL_DURATION_INDEX);
}

int64_t Camera3Module::GetOutputMinFrameDuration(
    int cam_id,
    int32_t format,
    const ResolutionInfo& resolution) {
  return GetOutputKeyParameterI64(cam_id, format, resolution,
                                  ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                                  STREAM_CONFIG_MIN_DURATION_INDEX);
}

void Camera3Module::GetNumberOfCamerasOnHalThread(int* result) {
  *result = cam_module_->get_number_of_cameras();
}

void Camera3Module::GetCameraInfoOnHalThread(int cam_id,
                                             camera_info* info,
                                             int* result) {
  *result = cam_module_->get_camera_info(cam_id, info);
}

void Camera3Module::OpenDeviceOnHalThread(int cam_id,
                                          camera3_device_t** cam_device) {
  *cam_device = nullptr;
  hw_device_t* device = nullptr;
  char cam_id_name[3];
  snprintf(cam_id_name, 3, "%d", cam_id);
  if (cam_module_->common.methods->open((const hw_module_t*)cam_module_,
                                        cam_id_name, &device) == 0) {
    *cam_device = reinterpret_cast<camera3_device_t*>(device);
  }
}

void Camera3Module::CloseDeviceOnHalThread(camera3_device_t* cam_device,
                                           int* result) {
  ASSERT_NE(nullptr, cam_device->common.close)
      << "Camera close() is not implemented";
  *result = cam_device->common.close(&cam_device->common);
}

// Test fixture

void Camera3ModuleFixture::SetUp() {
  ASSERT_EQ(0, cam_module_.Initialize())
      << "Camera module initialization fails";
}

// Test cases

TEST_F(Camera3ModuleFixture, NumberOfCameras) {
  ASSERT_GT(cam_module_.GetNumberOfCameras(), 0) << "No cameras found";
  ASSERT_LE(cam_module_.GetNumberOfCameras(), kMaxNumCameras)
      << "Too many cameras found";
}

TEST_F(Camera3ModuleFixture, OpenDeviceOfBadIndices) {
  int bad_indices[] = {-1, cam_module_.GetNumberOfCameras(),
                       cam_module_.GetNumberOfCameras() + 1};
  for (size_t i = 0; i < arraysize(bad_indices); ++i) {
    ASSERT_EQ(nullptr, cam_module_.OpenDevice(bad_indices[i]))
        << "Open camera device of bad index " << bad_indices[i];
  }
}

TEST_F(Camera3ModuleFixture, IsActiveArraySizeSubsetOfPixelArraySize) {
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera_info info;
    ASSERT_EQ(0, cam_module_.GetCameraInfo(cam_id, &info))
        << "Can't get camera info for " << cam_id;

    camera_metadata_ro_entry_t entry;
    ASSERT_EQ(
        0,
        find_camera_metadata_ro_entry(
            const_cast<camera_metadata_t*>(info.static_camera_characteristics),
            ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, &entry))
        << "Can't find the sensor pixel array size.";
    int pixel_array_w = entry.data.i32[0];
    int pixel_array_h = entry.data.i32[1];

    ASSERT_EQ(
        0,
        find_camera_metadata_ro_entry(
            const_cast<camera_metadata_t*>(info.static_camera_characteristics),
            ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &entry))
        << "Can't find the sensor active array size.";
    int active_array_w = entry.data.i32[0];
    int active_array_h = entry.data.i32[1];

    ASSERT_LE(active_array_h, pixel_array_h);
    ASSERT_LE(active_array_w, pixel_array_w);
  }
}

TEST_F(Camera3ModuleFixture, OpenDevice) {
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera3_device* cam_dev = cam_module_.OpenDevice(cam_id);
    ASSERT_NE(nullptr, cam_dev) << "Camera open() returned a NULL device";
    cam_module_.CloseDevice(*cam_dev);
  }
}

TEST_F(Camera3ModuleFixture, OpenDeviceTwice) {
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera3_device* cam_dev = cam_module_.OpenDevice(cam_id);
    ASSERT_NE(nullptr, cam_dev) << "Camera open() returned a NULL device";
    // Open the device again
    camera3_device* cam_bad_dev = cam_module_.OpenDevice(cam_id);
    ASSERT_EQ(nullptr, cam_bad_dev) << "Opening camera device " << cam_id
                                    << " should have failed";
    cam_module_.CloseDevice(*cam_dev);
  }
}

TEST_F(Camera3ModuleFixture, RequiredFormats) {
  auto IsResolutionSupported = [](
      const std::vector<ResolutionInfo>& resolution_list,
      const ResolutionInfo& resolution) {
    return std::find(resolution_list.begin(), resolution_list.end(),
                     resolution) != resolution_list.end();
  };
  auto RemoveResolution = [](std::vector<ResolutionInfo>& resolution_list,
                             const ResolutionInfo& resolution) {
    auto it =
        std::find(resolution_list.begin(), resolution_list.end(), resolution);
    if (it != resolution_list.end()) {
      resolution_list.erase(it);
    }
  };

  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    ASSERT_TRUE(cam_module_.IsFormatAvailable(cam_id, HAL_PIXEL_FORMAT_BLOB))
        << "Camera stream configuration does not support JPEG";
    ASSERT_TRUE(
        cam_module_.IsFormatAvailable(cam_id, HAL_PIXEL_FORMAT_YCbCr_420_888))
        << "Camera stream configuration does not support flexible YUV";

    // Reference:
    // camera2/cts/ExtendedCameraCharacteristicsTest.java#testAvailableStreamConfigs
    camera_info info;
    ASSERT_EQ(0, cam_module_.GetCameraInfo(cam_id, &info))
        << "Can't get camera info for " << cam_id;

    std::vector<ResolutionInfo> jpeg_resolutions =
        cam_module_.GetSortedOutputResolutions(cam_id, HAL_PIXEL_FORMAT_BLOB);
    std::vector<ResolutionInfo> yuv_resolutions =
        cam_module_.GetSortedOutputResolutions(cam_id,
                                               HAL_PIXEL_FORMAT_YCbCr_420_888);
    std::vector<ResolutionInfo> private_resolutions =
        cam_module_.GetSortedOutputResolutions(
            cam_id, HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED);

    const ResolutionInfo full_hd(1920, 1080), full_hd_alt(1920, 1088),
        hd(1280, 720), vga(640, 480), qvga(320, 240);

    camera_metadata_ro_entry_t entry;
    ASSERT_EQ(
        0,
        find_camera_metadata_ro_entry(
            const_cast<camera_metadata_t*>(info.static_camera_characteristics),
            ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &entry))
        << "Can't find the sensor active array size.";
    ResolutionInfo active_array(entry.data.i32[0], entry.data.i32[1]);
    if ((active_array.Width() >= full_hd.Width()) &&
        (active_array.Height() >= full_hd.Height())) {
      EXPECT_TRUE(IsResolutionSupported(jpeg_resolutions, full_hd) ||
                  IsResolutionSupported(jpeg_resolutions, full_hd_alt))
          << "Required FULLHD size not found for JPEG for camera " << cam_id;
    }
    if ((active_array.Width() >= hd.Width()) &&
        (active_array.Height() >= hd.Height())) {
      EXPECT_TRUE(IsResolutionSupported(jpeg_resolutions, hd))
          << "Required HD size not found for JPEG for camera " << cam_id;
    }
    if ((active_array.Width() >= vga.Width()) &&
        (active_array.Height() >= vga.Height())) {
      EXPECT_TRUE(IsResolutionSupported(jpeg_resolutions, vga))
          << "Required VGA size not found for JPEG for camera " << cam_id;
    }
    if ((active_array.Width() >= qvga.Width()) &&
        (active_array.Height() >= qvga.Height())) {
      EXPECT_TRUE(IsResolutionSupported(jpeg_resolutions, qvga))
          << "Required QVGA size not found for JPEG for camera " << cam_id;
    }

    ASSERT_EQ(
        0,
        find_camera_metadata_ro_entry(
            const_cast<camera_metadata_t*>(info.static_camera_characteristics),
            ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &entry))
        << "Cannot find the metadata ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL";
    int32_t hw_level = entry.data.i32[0];

    // Handle FullHD special case first
    if (IsResolutionSupported(jpeg_resolutions, full_hd)) {
      if (hw_level == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL) {
        EXPECT_TRUE(IsResolutionSupported(yuv_resolutions, full_hd) ||
                    IsResolutionSupported(yuv_resolutions, full_hd_alt))
            << "FullHD YUV size not found in Full device ";
        EXPECT_TRUE(IsResolutionSupported(private_resolutions, full_hd) ||
                    IsResolutionSupported(private_resolutions, full_hd_alt))
            << "FullHD private size not found in Full device ";
      }
      // Remove all FullHD or FullHD_Alt sizes for the remaining of the test
      RemoveResolution(jpeg_resolutions, full_hd);
      RemoveResolution(jpeg_resolutions, full_hd_alt);
    }

    // Check all sizes other than FullHD
    if (hw_level == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED) {
      // Remove all jpeg sizes larger than max video size
      for (auto it = jpeg_resolutions.begin(); it != jpeg_resolutions.end();) {
        if (it->Width() >= full_hd.Width() &&
            it->Height() >= full_hd.Height()) {
          it = jpeg_resolutions.erase(it);
        } else {
          it++;
        }
      }
    }

    std::stringstream ss;
    auto PrintResolutions =
        [&](const std::vector<ResolutionInfo>& resolutions) {
          ss.str("");
          for (const auto& it : resolutions) {
            ss << (ss.str().empty() ? "" : ", ") << it.Width() << "x"
               << it.Height();
          }
          return ss.str().c_str();
        };
    if (hw_level == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL ||
        hw_level == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED) {
      std::vector<ResolutionInfo> diff;
      std::set_difference(jpeg_resolutions.begin(), jpeg_resolutions.end(),
                          yuv_resolutions.begin(), yuv_resolutions.end(),
                          std::inserter(diff, diff.begin()));
      EXPECT_TRUE(diff.empty()) << "Sizes " << PrintResolutions(diff)
                                << " not found in YUV format";
    }

    std::vector<ResolutionInfo> diff;
    std::set_difference(jpeg_resolutions.begin(), jpeg_resolutions.end(),
                        private_resolutions.begin(), private_resolutions.end(),
                        std::inserter(diff, diff.begin()));
    EXPECT_TRUE(diff.empty()) << "Sizes " << PrintResolutions(diff)
                              << " not found in private format";
  }
}

// TODO: test keys used by RAW, burst and reprocessing capabilities when full
// mode is supported

static bool AreAllCapabilitiesSupported(
    camera_metadata_t* characteristics,
    const std::vector<int32_t>& capabilities) {
  std::set<int32_t> supported_capabilities;
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics,
                                    ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                    &entry) == 0) {
    for (size_t i = 0; i < entry.count; i++) {
      if ((entry.data.i32[i] >=
           ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE) &&
          (entry.data.i32[i] <=
           ANDROID_REQUEST_AVAILABLE_CAPABILITIES_CONSTRAINED_HIGH_SPEED_VIDEO)) {
        supported_capabilities.insert(entry.data.i32[i]);
      }
    }
  }

  for (const auto& it : capabilities) {
    if (supported_capabilities.find(it) == supported_capabilities.end()) {
      return false;
    }
  }
  return true;
}

static void ExpectKeyAvailable(camera_metadata_t* characteristics,
                               int32_t key,
                               int32_t hw_level,
                               const std::vector<int32_t>& capabilities) {
  camera_metadata_ro_entry_t entry;
  ASSERT_EQ(0,
            find_camera_metadata_ro_entry(
                characteristics, ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &entry))
      << "Cannot find the metadata ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL";
  int32_t actual_hw_level = entry.data.i32[0];

  // For LIMITED-level targeted keys, rely on capability check, not level
  if (actual_hw_level >= hw_level &&
      hw_level != ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED) {
    ASSERT_EQ(0, find_camera_metadata_ro_entry(characteristics, key, &entry))
        << "Key " << get_camera_metadata_tag_name(key)
        << " must be in characteristics for this hardware level ";
  } else if (AreAllCapabilitiesSupported(characteristics, capabilities)) {
    if (!(hw_level == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED &&
          actual_hw_level < hw_level)) {
      // Don't enforce LIMITED-starting keys on LEGACY level, even if cap is
      // defined
      std::stringstream ss;
      auto PrintCapabilities = [&]() {
        for (const auto& it : capabilities) {
          ss << (ss.str().empty() ? "" : ", ");
          switch (it) {
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE:
              ss << "BACKWARD_COMPATIBLE";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR:
              ss << "MANUAL_SENSOR";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING:
              ss << "MANUAL_POST_PROCESSING";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW:
              ss << "RAW";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING:
              ss << "PRIVATE_PROCESSING";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_READ_SENSOR_SETTINGS:
              ss << "READ_SENSOR_SETTINGS";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BURST_CAPTURE:
              ss << "BURST_CAPTURE";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING:
              ss << "YUV_REPROCESSING";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_DEPTH_OUTPUT:
              ss << "DEPTH_OUTPUT";
              break;
            case ANDROID_REQUEST_AVAILABLE_CAPABILITIES_CONSTRAINED_HIGH_SPEED_VIDEO:
              ss << "HIGHT_SPEED_VIDEO";
              break;
            default:
              ss << "unknown(" << it << ")";
          }
        }
        return ss.str().c_str();
      };
      ASSERT_EQ(0, find_camera_metadata_ro_entry(characteristics, key, &entry))
          << "Key " << get_camera_metadata_tag_name(key)
          << " must be in characteristics for capabilities "
          << PrintCapabilities();
    }
  }
}

static void ExpectKeyAvailable(camera_metadata_t* c,
                               int32_t key,
                               int32_t hw_level,
                               int32_t capability) {
  return ExpectKeyAvailable(c, key, hw_level, std::vector<int>({capability}));
}

TEST_F(Camera3ModuleFixture, StaticKeysTest) {
// Reference:
// camera2/cts/ExtendedCameraCharacteristicsTest.java#testKeys
#define IGNORE_HARDWARE_LEVEL INT32_MAX
#define IGNORE_CAPABILITY -1
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera_info info;
    ASSERT_EQ(0, cam_module_.GetCameraInfo(cam_id, &info))
        << "Can't get camera info for " << cam_id;
    auto c = const_cast<camera_metadata_t*>(info.static_camera_characteristics);
    ExpectKeyAvailable(
        c, ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
        IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AVAILABLE_MODES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
        IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AE_AVAILABLE_MODES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
        IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AE_COMPENSATION_RANGE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AE_COMPENSATION_STEP, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AE_LOCK_AVAILABLE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AF_AVAILABLE_MODES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AVAILABLE_EFFECTS, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AVAILABLE_SCENE_MODES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
        IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AWB_AVAILABLE_MODES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_CONTROL_AWB_LOCK_AVAILABLE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    // TODO: ANDROID_CONTROL_MAX_REGIONS_AE, ANDROID_CONTROL_MAX_REGIONS_AF,
    // ANDROID_CONTROL_MAX_REGIONS_AWB
    ExpectKeyAvailable(c, ANDROID_EDGE_AVAILABLE_EDGE_MODES,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       IGNORE_CAPABILITY);
    ExpectKeyAvailable(
        c, ANDROID_FLASH_INFO_AVAILABLE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(c, ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
                       IGNORE_HARDWARE_LEVEL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(
        c, ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_LENS_FACING, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(c, ANDROID_LENS_INFO_AVAILABLE_APERTURES,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
    ExpectKeyAvailable(c, ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
    ExpectKeyAvailable(
        c, ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(c, ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
    ExpectKeyAvailable(
        c, ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
        IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, IGNORE_HARDWARE_LEVEL,
        std::vector<int32_t>(
            {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING,
             ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING}));
    ExpectKeyAvailable(
        c, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_REQUEST_PIPELINE_MAX_DEPTH, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_SCALER_CROPPING_TYPE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
        std::vector<int32_t>(
            {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR,
             ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW}));
    ExpectKeyAvailable(c, ANDROID_SENSOR_CALIBRATION_TRANSFORM1,
                       IGNORE_HARDWARE_LEVEL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(c, ANDROID_SENSOR_COLOR_TRANSFORM1,
                       IGNORE_HARDWARE_LEVEL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(c, ANDROID_SENSOR_FORWARD_MATRIX1, IGNORE_HARDWARE_LEVEL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(
        c, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, IGNORE_HARDWARE_LEVEL,
        std::vector<int32_t>(
            {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE,
             ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW}));
    ExpectKeyAvailable(c, ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(c, ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
    ExpectKeyAvailable(c, ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
    ExpectKeyAvailable(
        c, ANDROID_SENSOR_INFO_PHYSICAL_SIZE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(c, ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
    ExpectKeyAvailable(c, ANDROID_SENSOR_INFO_WHITE_LEVEL,
                       IGNORE_HARDWARE_LEVEL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(
        c, ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(c, ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR);
    ExpectKeyAvailable(
        c, ANDROID_SENSOR_ORIENTATION, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(c, ANDROID_SENSOR_REFERENCE_ILLUMINANT1,
                       IGNORE_HARDWARE_LEVEL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(
        c, ANDROID_SHADING_AVAILABLE_MODES,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED,
        std::vector<int32_t>(
            {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING,
             ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW}));
    ExpectKeyAvailable(
        c, ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
        IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(c, ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
                       IGNORE_HARDWARE_LEVEL,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(c,
                       ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
                       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED,
                       ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    ExpectKeyAvailable(
        c, ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_SYNC_MAX_LATENCY, IGNORE_HARDWARE_LEVEL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
    ExpectKeyAvailable(
        c, ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING);
    ExpectKeyAvailable(
        c, ANDROID_TONEMAP_MAX_CURVE_POINTS,
        ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING);
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(c, ANDROID_SENSOR_REFERENCE_ILLUMINANT2,
                                      &entry) == 0) {
      ExpectKeyAvailable(c, ANDROID_SENSOR_REFERENCE_ILLUMINANT2,
                         IGNORE_HARDWARE_LEVEL,
                         ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
      ExpectKeyAvailable(c, ANDROID_SENSOR_COLOR_TRANSFORM2,
                         IGNORE_HARDWARE_LEVEL,
                         ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
      ExpectKeyAvailable(c, ANDROID_SENSOR_CALIBRATION_TRANSFORM2,
                         IGNORE_HARDWARE_LEVEL,
                         ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
      ExpectKeyAvailable(c, ANDROID_SENSOR_FORWARD_MATRIX2,
                         IGNORE_HARDWARE_LEVEL,
                         ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
    }
  }
}

TEST_F(Camera3ModuleFixture, StreamConfigurationMapTest) {
  // Reference:
  // camera2/cts/ExtendedCameraCharacteristicsTest.java#testStreamConfigurationMap
  const int64_t kToleranceFactor = 2;
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera_info info;
    ASSERT_EQ(0, cam_module_.GetCameraInfo(cam_id, &info))
        << "Can't get camera info for " << cam_id;

    std::vector<int32_t> available_formats =
        cam_module_.GetOutputFormats(cam_id);
    for (const auto& format : available_formats) {
      std::vector<ResolutionInfo> available_resolutions =
          cam_module_.GetSortedOutputResolutions(cam_id, format);
      size_t resolution_count = available_resolutions.size();
      for (size_t i = 0; i < resolution_count; i++) {
        int64_t stall_duration = cam_module_.GetOutputStallDuration(
            cam_id, format, available_resolutions[i]);
        if (stall_duration >= 0) {
          if (format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
            EXPECT_EQ(0, stall_duration)
                << "YUV_420_888 may not have a non-zero stall duration";
          } else if (format == HAL_PIXEL_FORMAT_BLOB) {
            // Stall duration should be in a reasonable range: larger size
            // should normally have larger stall duration
            if (i > 0) {
              int64_t prev_duration = cam_module_.GetOutputStallDuration(
                  cam_id, format, available_resolutions[i - 1]);
              EXPECT_LE(prev_duration / kToleranceFactor, stall_duration)
                  << "Stall duration (format " << format << " and size "
                  << available_resolutions[i].Width() << "x"
                  << available_resolutions[i].Height()
                  << ") is not in the right range";
            }
          }
        } else {
          ADD_FAILURE() << "Negative stall duration for format " << format;
        }

        int64_t min_duration = cam_module_.GetOutputMinFrameDuration(
            cam_id, format, available_resolutions[i]);
        if (AreAllCapabilitiesSupported(
                const_cast<camera_metadata_t*>(
                    info.static_camera_characteristics),
                std::vector<int32_t>(
                    {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR}))) {
          EXPECT_LT(0, min_duration)
              << "MANUAL_SENSOR capability, need positive min frame duration "
                 "for format "
              << format << " and size " << available_resolutions[i].Width()
              << "x" << available_resolutions[i].Height();
        } else {
          EXPECT_LE(0, min_duration)
              << "Need non-negative min frame duration for format " << format
              << " and size " << available_resolutions[i].Width() << "x"
              << available_resolutions[i].Height();
        }
      }
    }
  }
}

}  // namespace camera3_test

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;

  // Set up logging so we can enable VLOGs with -v / --vmodule.
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  LOG_ASSERT(logging::InitLogging(settings));

  // Open camera HAL and get module
  void* cam_hal_handle = NULL;
  camera3_test::InitCameraModule(cam_hal_handle);
  camera3_test::g_module_thread.Start();

  int result = EXIT_FAILURE;
  if (!testing::Test::HasFailure()) {
    // Initialize and run all tests
    ::testing::InitGoogleTest(&argc, argv);
    result = RUN_ALL_TESTS();
  }

  camera3_test::g_module_thread.Stop();
  if (cam_hal_handle) {
    // Close Camera HAL
    dlclose(cam_hal_handle);
  }

  return result;
}