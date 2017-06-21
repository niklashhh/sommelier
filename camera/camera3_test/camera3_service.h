// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef CAMERA3_TEST_CAMERA3_SERVICE_H_
#define CAMERA3_TEST_CAMERA3_SERVICE_H_

#include <semaphore.h>

#include "camera3_device_fixture.h"

namespace camera3_test {

const int32_t kNumberOfOutputStreamBuffers = 2;
const int32_t kPreviewOutputStreamIdx = 0;
const int32_t kStillCaptureOutputStreamIdx = 1;
const int32_t kWaitForStopPreviewTimeoutMs = 3000;
const int32_t kWaitForFocusDoneTimeoutMs = 6000;
const int32_t kWaitForAWBConvergedTimeoutMs = 3000;
enum { PREVIEW_STOPPED, PREVIEW_STARTED, PREVIEW_STOPPING };
#define INCREASE_INDEX(idx) \
  (idx) = (idx == number_of_capture_requests_ - 1) ? 0 : (idx) + 1

struct MetadataKeyValue {
  int32_t key;
  const void* data;
  size_t data_count;
  MetadataKeyValue(int32_t k, const void* d, size_t c)
      : key(k), data(d), data_count(c) {}
};

class Camera3Service {
 public:
  explicit Camera3Service(std::vector<int> cam_ids)
      : cam_ids_(cam_ids), initialized_(false) {}

  ~Camera3Service();

  typedef base::Callback<void(int cam_id,
                              uint32_t frame_number,
                              CameraMetadataUniquePtr metadata,
                              BufferHandleUniquePtr buffer)>
      ProcessStillCaptureResultCallback;

  // Initialize service and corresponding devices
  int Initialize();

  // Initialize service and corresponding devices and register processing
  // still capture result callback
  int Initialize(ProcessStillCaptureResultCallback cb);

  // Destroy service and corresponding devices
  void Destroy();

  // Start camera preview with given preview resolution |preview_resolution|
  int StartPreview(int cam_id, const ResolutionInfo& preview_resolution);

  // Configure still capture with given resolution |still_capture_resolution|
  // and start camera preview with |preview_resolution|.
  int PrepareStillCaptureAndStartPreview(
      int cam_id,
      const ResolutionInfo& still_capture_resolution,
      const ResolutionInfo& preview_resolution);

  // Stop camera preview
  void StopPreview(int cam_id);

  // Start auto focus
  void StartAutoFocus(int cam_id);

  // Wait for auto focus done
  int WaitForAutoFocusDone(int cam_id);

  // Wait for AWB converged and lock AWB
  int WaitForAWBConvergedAndLock(int cam_id);

  // Start AE precapture
  void StartAEPrecapture(int cam_id);

  // Wait for AE stable
  int WaitForAEStable(int cam_id);

  // Take still capture with settings |metadata|
  void TakeStillCapture(int cam_id, const camera_metadata_t* metadata);

  // Wait for |num_frames| number of preview frames with |timeout_ms|
  // milliseconds of timeout for each frame.
  int WaitForPreviewFrames(int cam_id,
                           uint32_t num_frames,
                           uint32_t timeout_ms);

  // Get device static information
  const Camera3Device::StaticInfo* GetStaticInfo(int cam_id);

  // Get device default request settings
  const camera_metadata_t* ConstructDefaultRequestSettings(int cam_id,
                                                           int type);

 private:
  std::vector<int> cam_ids_;

  base::Lock lock_;

  bool initialized_;

  class Camera3DeviceService;
  std::unordered_map<int, std::unique_ptr<Camera3DeviceService>>
      cam_dev_service_map_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Camera3Service);
};

class Camera3Service::Camera3DeviceService {
 public:
  Camera3DeviceService(int cam_id, ProcessStillCaptureResultCallback cb)
      : cam_id_(cam_id),
        cam_device_(cam_id),
        service_thread_("Camera3 Test Service Thread"),
        process_still_capture_result_cb_(cb),
        preview_state_(PREVIEW_STOPPED),
        number_of_capture_requests_(0),
        capture_request_idx_(0),
        number_of_in_flight_requests_(0),
        still_capture_metadata_(nullptr) {}

  int Initialize();

  void Destroy();

  // Configure still capture with given resolution |still_capture_resolution|
  // and start camera preview with |preview_resolution|.
  int PrepareStillCaptureAndStartPreview(
      const ResolutionInfo& still_capture_resolution,
      const ResolutionInfo& preview_resolution);

  // Stop camera preview
  void StopPreview();

  // Start auto focus
  void StartAutoFocus();

  // Wait for auto focus done
  int WaitForAutoFocusDone();

  // Wait for AWB converged and lock AWB
  int WaitForAWBConvergedAndLock();

  // Start AE precapture
  void StartAEPrecapture();

  // Wait for AE stable
  int WaitForAEStable();

  // Take still capture with settings |metadata|
  void TakeStillCapture(const camera_metadata_t* metadata);

  // Wait for |num_frames| number of preview frames with |timeout_ms|
  // milliseconds of timeout for each frame.
  int WaitForPreviewFrames(uint32_t num_frames, uint32_t timeout_ms);

  // Get static information
  const Camera3Device::StaticInfo* GetStaticInfo() const;

  // Get default request settings
  const camera_metadata_t* ConstructDefaultRequestSettings(int type);

 private:
  // Process result metadata and output buffers
  void ProcessResultMetadataOutputBuffers(
      uint32_t frame_number,
      CameraMetadataUniquePtr metadata,
      std::vector<BufferHandleUniquePtr> buffers);

  void PrepareStillCaptureAndStartPreviewOnServiceThread(
      const ResolutionInfo still_capture_resolution,
      const ResolutionInfo preview_resolution,
      int* result);

  void StartAutoFocusOnServiceThread();

  void StopPreviewOnServiceThread(base::Callback<void()> cb);

  void AddMetadataListenerOnServiceThread(int32_t key,
                                          int32_t value,
                                          base::Callback<void()> cb);

  void LockAWBOnServiceThread();

  void StartAEPrecaptureOnServiceThread();

  void TakeStillCaptureOnServiceThread(const camera_metadata_t* metadata,
                                       base::Callback<void()> cb);

  // This function can be called by PrepareStillCaptureAndStartPreview() or
  // ProcessResultMetadataOutputBuffers() to process one preview request.
  // It will check whether there was a still capture request or preview
  // repeating/one-shot setting changes and construct the capture request
  // accordingly.
  void ProcessPreviewRequestOnServiceThread();

  void ProcessResultMetadataOutputBuffersOnServiceThread(
      uint32_t frame_number,
      CameraMetadataUniquePtr metadata,
      std::vector<BufferHandleUniquePtr> buffers);

  int cam_id_;

  Camera3Device cam_device_;

  Camera3TestThread service_thread_;

  ProcessStillCaptureResultCallback process_still_capture_result_cb_;

  int32_t preview_state_;

  base::Callback<void()> stop_preview_cb_;

  std::vector<const camera3_stream_t*> streams_;

  size_t number_of_capture_requests_;

  // Keep |number_of_capture_requests_| number of capture request
  std::vector<camera3_capture_request_t> capture_requests_;

  // Keep track of two stream buffers for each capture request. The preview
  // buffer is at index 0 while still capture one at index 1.
  std::vector<std::vector<camera3_stream_buffer_t>> output_stream_buffers_;

  // The index of capture request that is going to have its corresponding
  // capture result returned
  size_t capture_request_idx_;

  // Number of capture requests that are being processed by HAL
  size_t number_of_in_flight_requests_;

  // Metadata for repeating preview requests
  CameraMetadataUniquePtr repeating_preview_metadata_;

  // Metadata for one-shot preview requests. It can be used to trigger AE
  // precapture and auto focus.
  CameraMetadataUniquePtr oneshot_preview_metadata_;

  // Metadata for still capture requests
  const camera_metadata_t* still_capture_metadata_;

  base::Callback<void()> still_capture_cb_;

  struct MetadataListener {
    int32_t key;
    int32_t value;
    base::Callback<void()> cb;
    MetadataListener(int32_t k, int32_t v, base::Callback<void()> c)
        : key(k), value(v), cb(c) {}
  };

  std::list<MetadataListener> metadata_listener_list_;

  sem_t preview_frame_sem_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Camera3DeviceService);
};

}  // namespace camera3_test

#endif  // CAMERA3_TEST_CAMERA3_SERVICE_H_