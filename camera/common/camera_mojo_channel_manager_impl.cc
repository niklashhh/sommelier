/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_mojo_channel_manager_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/logging.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"

namespace cros {

CameraMojoChannelManager* CameraMojoChannelManager::GetInstance() {
  static CameraMojoChannelManagerImpl* instance =
      new CameraMojoChannelManagerImpl();

  if (!instance->Start())
    return nullptr;

  return instance;
}

CameraMojoChannelManagerImpl::CameraMojoChannelManagerImpl()
    : ipc_thread_("IpcThread"), is_started_(false) {
  VLOGF_ENTER();
}

CameraMojoChannelManagerImpl::~CameraMojoChannelManagerImpl() {
  VLOGF_ENTER();
  dispatcher_.reset();
  ipc_thread_.Stop();
  mojo::edk::ShutdownIPCSupport();
  VLOGF_EXIT();
}

bool CameraMojoChannelManagerImpl::Start() {
  base::AutoLock l(start_lock_);

  if (is_started_) {
    return true;
  }

  mojo::edk::Init();
  if (!ipc_thread_.StartWithOptions(
          base::Thread::Options(base::MessageLoop::TYPE_IO, 0))) {
    LOGF(ERROR) << "Failed to start IPC Thread";
    return false;
  }
  mojo::edk::InitIPCSupport(this, ipc_thread_.task_runner());

  is_started_ = true;
  return true;
}

void CameraMojoChannelManagerImpl::CreateJpegDecodeAccelerator(
    mojom::JpegDecodeAcceleratorRequest request) {
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &CameraMojoChannelManagerImpl::CreateJpegDecodeAcceleratorOnIpcThread,
          base::Unretained(this), base::Passed(std::move(request))));
}

mojom::CameraAlgorithmOpsPtr
CameraMojoChannelManagerImpl::CreateCameraAlgorithmOpsPtr() {
  VLOGF_ENTER();

  mojo::ScopedMessagePipeHandle parent_pipe;
  mojom::CameraAlgorithmOpsPtr algorithm_ops;

  base::FilePath socket_path(constants::kCrosCameraAlgoSocketPathString);
  MojoResult result = cros::CreateMojoChannelToChildByUnixDomainSocket(
      socket_path, &parent_pipe);
  if (result != MOJO_RESULT_OK) {
    LOGF(WARNING) << "Failed to create Mojo Channel to" << socket_path.value();
    return nullptr;
  }

  algorithm_ops.Bind(
      mojom::CameraAlgorithmOpsPtrInfo(std::move(parent_pipe), 0u));

  LOGF(INFO) << "Connected to CameraAlgorithmOps";

  VLOGF_EXIT();
  return algorithm_ops;
}

void CameraMojoChannelManagerImpl::CreateJpegDecodeAcceleratorOnIpcThread(
    mojom::JpegDecodeAcceleratorRequest request) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  EnsureDispatcherConnectedOnIpcThread();
  if (dispatcher_.is_bound()) {
    dispatcher_->GetJpegDecodeAccelerator(std::move(request));
  }
}

void CameraMojoChannelManagerImpl::EnsureDispatcherConnectedOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  if (dispatcher_.is_bound()) {
    return;
  }

  mojo::ScopedMessagePipeHandle child_pipe;

  base::FilePath socket_path(constants::kCrosCameraSocketPathString);
  MojoResult result = cros::CreateMojoChannelToParentByUnixDomainSocket(
      socket_path, &child_pipe);
  if (result != MOJO_RESULT_OK) {
    LOGF(WARNING) << "Failed to create Mojo Channel to" << socket_path.value();
    return;
  }

  dispatcher_ = mojo::MakeProxy(
      mojom::CameraHalDispatcherPtrInfo(std::move(child_pipe), 0u),
      ipc_thread_.task_runner());
  dispatcher_.set_connection_error_handler(
      base::Bind(&CameraMojoChannelManagerImpl::OnDispatcherError,
                 base::Unretained(this)));

  LOGF(INFO) << "Connected to CameraHalDispatcher";

  VLOGF_EXIT();
}

void CameraMojoChannelManagerImpl::OnDispatcherError() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  LOGF(ERROR) << "Mojo channel to CameraHalDispatcher is broken";
  dispatcher_.reset();
  VLOGF_EXIT();
}

}  // namespace cros