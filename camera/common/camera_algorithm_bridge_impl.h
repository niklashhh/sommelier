/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INCLUDE_ARC_CAMERA_ALGORITHM_BRIDGE_IMPL_H_
#define INCLUDE_ARC_CAMERA_ALGORITHM_BRIDGE_IMPL_H_

#include "base/synchronization/lock.h"
#include "base/threading/thread.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/process_delegate.h"

#include "arc/camera_algorithm.h"
#include "arc/camera_algorithm_bridge.h"
#include "arc/future.h"
#include "common/camera_algorithm_callback_ops_impl.h"
#include "common/camera_algorithm_ops_impl.h"

namespace arc {

// This is the implementation of CameraAlgorithmBridge interface. It is used
// by the camera HAL process.

class CameraAlgorithmBridgeImpl : public CameraAlgorithmBridge,
                                  public mojo::edk::ProcessDelegate {
 public:
  CameraAlgorithmBridgeImpl();

  ~CameraAlgorithmBridgeImpl();

  // This method registers a callback function for buffer handle return.
  int32_t Initialize(const camera_algorithm_callback_ops_t* callback_ops);

  // Register a buffer to the camera algorithm library and gets
  // the handle associated with it.
  int32_t RegisterBuffer(int buffer_fd);

  // Post a request for the camera algorithm library to process the
  // given buffer.
  int32_t Request(const std::vector<uint8_t>& req_header,
                  int32_t buffer_handle);

  // Deregisters buffers to the camera algorithm library.
  void DeregisterBuffers(const std::vector<int32_t>& buffer_handles);

  // Handle IPC shutdown completion
  void OnShutdownComplete() {}

 private:
  void PreInitializeOnIpcThread(base::Callback<void(void)> cb);

  void InitializeOnIpcThread(
      const camera_algorithm_callback_ops_t* callback_ops,
      base::Callback<void(int32_t)> cb);

  void DestroyOnIpcThread();

  void RegisterBufferOnIpcThread(int buffer_fd,
                                 base::Callback<void(int32_t)> cb);

  void RequestOnIpcThread(const std::vector<uint8_t>& req_header,
                          int32_t buffer_handle,
                          base::Callback<void(int32_t)> cb);

  void DeregisterBuffersOnIpcThread(const std::vector<int32_t>& buffer_handles,
                                    base::Callback<void(void)> cb);

  // Pointer to local proxy of remote CameraAlgorithmOps interface
  // implementation.
  mojom::CameraAlgorithmOpsPtr interface_ptr_;

  // Pointer to CameraAlgorithmCallbackOpss interface implementation.
  std::unique_ptr<CameraAlgorithmCallbackOpsImpl> cb_impl_;

  // Thread for IPC chores
  base::Thread ipc_thread_;

  // Store observers for future locks
  internal::CancellationRelay relay_;

  DISALLOW_COPY_AND_ASSIGN(CameraAlgorithmBridgeImpl);
};

}  // namespace arc

#endif  // INCLUDE_ARC_CAMERA_ALGORITHM_BRIDGE_IMPL_H_