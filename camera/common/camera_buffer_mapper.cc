/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "arc/camera_buffer_mapper.h"

#include <vector>

#include <linux/videodev2.h>
#include <sys/mman.h>

#include <drm_fourcc.h>
#include <gbm.h>
#include <system/graphics.h>

#include "arc/common.h"
#include "common/camera_buffer_handle.h"
#include "common/camera_buffer_mapper_internal.h"

namespace arc {

GbmDevice::GbmDevice() : device_(internal::CreateGbmDevice()) {}

GbmDevice::~GbmDevice() {
  if (device_) {
    close(gbm_device_get_fd(device_));
    gbm_device_destroy(device_);
  }
}

int GbmDevice::IsFormatSupported(uint32_t format, uint32_t usage) {
  return gbm_device_is_format_supported(device_, format, usage);
}

struct gbm_bo* GbmDevice::CreateBo(uint32_t width,
                                   uint32_t height,
                                   uint32_t format,
                                   uint32_t flags) {
  return gbm_bo_create(device_, width, height, format, flags);
}

void GbmDevice::Deleter::operator()(GbmDevice* device) {
  delete device;
}

// static
CameraBufferMapper* CameraBufferMapper::GetInstance() {
  static CameraBufferMapper instance;
  if (!instance.gbm_device_->device_) {
    LOGF(ERROR) << "Failed to create GBM device for CameraBufferMapper";
    return nullptr;
  }
  return &instance;
}

CameraBufferMapper::CameraBufferMapper() : gbm_device_(new GbmDevice()) {}

int CameraBufferMapper::Register(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }

  base::AutoLock l(lock_);

  auto context_it = buffer_context_.find(buffer);
  if (context_it != buffer_context_.end()) {
    context_it->second->usage++;
    return 0;
  }

  std::unique_ptr<BufferContext> buffer_context(new struct BufferContext);

  if (handle->type == GRALLOC) {
    // Import the buffer if we haven't done so.
    struct gbm_import_fd_planar_data import_data;
    memset(&import_data, 0, sizeof(import_data));
    import_data.width = handle->width;
    import_data.height = handle->height;
    import_data.format = handle->drm_format;
    uint32_t num_planes = GetNumPlanes(buffer);
    if (num_planes <= 0) {
      return -EINVAL;
    }
    for (size_t i = 0; i < num_planes; ++i) {
      import_data.fds[i] = handle->fds[i].get();
      import_data.strides[i] = handle->strides[i];
      import_data.offsets[i] = handle->offsets[i];
    }

    uint32_t usage =
        GBM_BO_USE_LINEAR | GBM_BO_USE_CAMERA_READ | GBM_BO_USE_CAMERA_WRITE;
    buffer_context->bo = gbm_bo_import(
        gbm_device_->device_, GBM_BO_IMPORT_FD_PLANAR, &import_data, usage);
    if (!buffer_context->bo) {
      LOGF(ERROR) << "Failed to import buffer 0x" << std::hex
                  << handle->buffer_id;
      return -EIO;
    }
  } else if (handle->type == SHM) {
    // The shared memory buffer is a contiguous area of memory which is large
    // enough to hold all the physical planes.  We mmap the buffer on Register
    // and munmap on Deregister.
    off_t size = lseek(handle->fds[0].get(), 0, SEEK_END);
    if (size == -1) {
      LOGF(ERROR) << "Failed to get shm buffer size through lseek: "
                  << strerror(errno);
      return -errno;
    }
    buffer_context->shm_buffer_size = static_cast<uint32_t>(size);
    lseek(handle->fds[0].get(), 0, SEEK_SET);
    buffer_context->mapped_addr =
        mmap(nullptr, buffer_context->shm_buffer_size, PROT_READ | PROT_WRITE,
             MAP_SHARED, handle->fds[0].get(), 0);
    if (buffer_context->mapped_addr == MAP_FAILED) {
      LOGF(ERROR) << "Failed to mmap shm buffer: " << strerror(errno);
      return -errno;
    }
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return -EINVAL;
  }

  buffer_context->usage = 1;
  buffer_context_[buffer] = std::move(buffer_context);
  return 0;
}

int CameraBufferMapper::Deregister(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }

  base::AutoLock l(lock_);

  auto context_it = buffer_context_.find(buffer);
  if (context_it == buffer_context_.end()) {
    LOGF(ERROR) << "Unknown buffer 0x" << std::hex << handle->buffer_id;
    return -EINVAL;
  }
  auto buffer_context = context_it->second.get();
  if (handle->type == GRALLOC) {
    if (!--buffer_context->usage) {
      // Unmap all the existing mapping of bo.
      for (auto it = buffer_info_.begin(); it != buffer_info_.end();) {
        if (it->second->bo == buffer_context->bo) {
          it = buffer_info_.erase(it);
        } else {
          ++it;
        }
      }
      buffer_context_.erase(context_it);
    }
    return 0;
  } else if (handle->type == SHM) {
    if (!--buffer_context->usage) {
      int ret =
          munmap(buffer_context->mapped_addr, buffer_context->shm_buffer_size);
      if (ret == -1) {
        LOGF(ERROR) << "Failed to munmap shm buffer: " << strerror(errno);
      }
      buffer_context_.erase(context_it);
    }
    return 0;
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return -EINVAL;
  }
}

int CameraBufferMapper::Lock(buffer_handle_t buffer,
                             uint32_t flags,
                             uint32_t x,
                             uint32_t y,
                             uint32_t width,
                             uint32_t height,
                             void** out_addr) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }
  uint32_t num_planes = GetNumPlanes(buffer);
  if (!num_planes) {
    return -EINVAL;
  }
  if (num_planes > 1) {
    LOGF(ERROR) << "Lock called on multi-planar buffer 0x" << std::hex
                << handle->buffer_id;
    return -EINVAL;
  }

  *out_addr = Map(buffer, flags, 0);
  if (*out_addr == MAP_FAILED) {
    return -EINVAL;
  }
  return 0;
}

int CameraBufferMapper::LockYCbCr(buffer_handle_t buffer,
                                  uint32_t flags,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t width,
                                  uint32_t height,
                                  struct android_ycbcr* out_ycbcr) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }
  uint32_t num_planes = GetNumPlanes(buffer);
  if (!num_planes) {
    return -EINVAL;
  }
  if (num_planes < 2) {
    LOGF(ERROR) << "LockYCbCr called on single-planar buffer 0x" << std::hex
                << handle->buffer_id;
    return -EINVAL;
  }

  DCHECK_LE(num_planes, 3u);
  std::vector<uint8_t*> addr(3);
  for (size_t i = 0; i < num_planes; ++i) {
    void* a = Map(buffer, flags, i);
    if (a == MAP_FAILED) {
      return -EINVAL;
    }
    addr[i] = reinterpret_cast<uint8_t*>(a);
  }
  out_ycbcr->y = addr[0];
  out_ycbcr->ystride = handle->strides[0];
  out_ycbcr->cstride = handle->strides[1];

  if (num_planes == 2) {
    out_ycbcr->chroma_step = 2;
    switch (handle->drm_format) {
      case DRM_FORMAT_NV12:
        out_ycbcr->cb = addr[1];
        out_ycbcr->cr = addr[1] + 1;
        break;

      case DRM_FORMAT_NV21:
        out_ycbcr->cb = addr[1] + 1;
        out_ycbcr->cr = addr[1];
        break;

      default:
        LOGF(ERROR) << "Unsupported semi-planar format: "
                    << FormatToString(handle->drm_format);
        return -EINVAL;
    }
  } else {  // num_planes == 3
    out_ycbcr->chroma_step = 1;
    switch (handle->drm_format) {
      case DRM_FORMAT_YUV420:
        out_ycbcr->cb = addr[1];
        out_ycbcr->cr = addr[2];
        break;

      case DRM_FORMAT_YVU420:
        out_ycbcr->cb = addr[2];
        out_ycbcr->cr = addr[1];
        break;

      default:
        LOGF(ERROR) << "Unsupported planar format: "
                    << FormatToString(handle->drm_format);
        return -EINVAL;
    }
  }
  return 0;
}

int CameraBufferMapper::Unlock(buffer_handle_t buffer) {
  for (size_t i = 0; i < GetNumPlanes(buffer); ++i) {
    int ret = Unmap(buffer, i);
    if (ret) {
      return ret;
    }
  }
  return 0;
}

// static
uint32_t CameraBufferMapper::GetNumPlanes(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }

  switch (handle->drm_format) {
    case DRM_FORMAT_ABGR1555:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_ABGR4444:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_ARGB1555:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_ARGB4444:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_AYUV:
    case DRM_FORMAT_BGR233:
    case DRM_FORMAT_BGR565:
    case DRM_FORMAT_BGR888:
    case DRM_FORMAT_BGRA1010102:
    case DRM_FORMAT_BGRA4444:
    case DRM_FORMAT_BGRA5551:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_BGRX1010102:
    case DRM_FORMAT_BGRX4444:
    case DRM_FORMAT_BGRX5551:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_C8:
    case DRM_FORMAT_GR88:
    case DRM_FORMAT_R8:
    case DRM_FORMAT_RG88:
    case DRM_FORMAT_RGB332:
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_RGB888:
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_RGBA4444:
    case DRM_FORMAT_RGBA5551:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_RGBX1010102:
    case DRM_FORMAT_RGBX4444:
    case DRM_FORMAT_RGBX5551:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_VYUY:
    case DRM_FORMAT_XBGR1555:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_XBGR4444:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB1555:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_XRGB4444:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_YUYV:
    case DRM_FORMAT_YVYU:
      return 1;
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
      return 2;
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
      return 3;
  }

  LOGF(ERROR) << "Unknown format: " << FormatToString(handle->drm_format);
  return 0;
}

// static
uint32_t CameraBufferMapper::GetV4L2PixelFormat(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }

  switch (handle->drm_format) {
    case DRM_FORMAT_ARGB8888:
      return V4L2_PIX_FMT_ABGR32;

    // There is no standard V4L2 pixel format corresponding to
    // DRM_FORMAT_xBGR8888.  We use our own V4L2 format extension
    // V4L2_PIX_FMT_RGBX32 here.
    case DRM_FORMAT_ABGR8888:
      return V4L2_PIX_FMT_RGBX32;
    case DRM_FORMAT_XBGR8888:
      return V4L2_PIX_FMT_RGBX32;

    // DRM_FORMAT_R8 is used as the underlying buffer format for
    // HAL_PIXEL_FORMAT_BLOB which corresponds to JPEG buffer.
    case DRM_FORMAT_R8:
      return V4L2_PIX_FMT_JPEG;

    // Semi-planar formats.
    case DRM_FORMAT_NV12:
      return V4L2_PIX_FMT_NV12M;
    case DRM_FORMAT_NV21:
      return V4L2_PIX_FMT_NV21M;

    // Multi-planar formats.
    case DRM_FORMAT_YUV420:
      return V4L2_PIX_FMT_YUV420M;
    case DRM_FORMAT_YVU420:
      return V4L2_PIX_FMT_YVU420M;
  }

  LOGF(ERROR) << "Could not convert format "
              << FormatToString(handle->drm_format) << " to V4L2 pixel format";
  return 0;
}

// static
size_t CameraBufferMapper::GetPlaneStride(buffer_handle_t buffer,
                                          size_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }
  if (plane >= GetNumPlanes(buffer)) {
    LOGF(ERROR) << "Invalid plane: " << plane;
    return 0;
  }
  return handle->strides[plane];
}

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

// static
size_t CameraBufferMapper::GetPlaneSize(buffer_handle_t buffer, size_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }
  if (plane >= GetNumPlanes(buffer)) {
    LOGF(ERROR) << "Invalid plane: " << plane;
    return 0;
  }
  uint32_t vertical_subsampling;
  switch (handle->drm_format) {
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
      vertical_subsampling = (plane == 0) ? 1 : 2;
      break;
    default:
      vertical_subsampling = 1;
  }
  return (handle->strides[plane] *
          DIV_ROUND_UP(handle->height, vertical_subsampling));
}

GbmDevice* CameraBufferMapper::GetGbmDevice() {
  return gbm_device_.get();
}

void* CameraBufferMapper::Map(buffer_handle_t buffer,
                              uint32_t flags,
                              uint32_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return MAP_FAILED;
  }

  uint32_t num_planes = GetNumPlanes(buffer);
  if (!num_planes) {
    return MAP_FAILED;
  }
  if (!(plane < kMaxPlanes && plane < num_planes)) {
    LOGF(ERROR) << "Invalid plane: " << plane;
    return MAP_FAILED;
  }

  VLOGF(2) << "buffer info:";
  VLOGF(2) << "\tfd: " << handle->fds[plane].get();
  VLOGF(2) << "\tbuffer_id: 0x" << std::hex << handle->buffer_id;
  VLOGF(2) << "\ttype: " << handle->type;
  VLOGF(2) << "\tformat: " << FormatToString(handle->drm_format);
  VLOGF(2) << "\twidth: " << handle->width;
  VLOGF(2) << "\theight: " << handle->height;
  VLOGF(2) << "\tstride: " << handle->strides[plane];
  VLOGF(2) << "\toffset: " << handle->offsets[plane];

  base::AutoLock l(lock_);

  if (handle->type == GRALLOC) {
    auto key = MappedGrallocBufferInfoCache::key_type(buffer, plane);
    auto info_cache = buffer_info_.find(key);
    if (info_cache == buffer_info_.end()) {
      // We haven't mapped |plane| of |buffer| yet.
      std::unique_ptr<MappedGrallocBufferInfo> info(
          new MappedGrallocBufferInfo);
      auto context_it = buffer_context_.find(buffer);
      if (context_it == buffer_context_.end()) {
        LOGF(ERROR) << "Buffer 0x" << std::hex << handle->buffer_id
                    << " is not registered";
        return MAP_FAILED;
      }
      info->bo = context_it->second->bo;
      // Since |flags| is reserved we don't expect user to pass any non-zero
      // value, we simply override |flags| here.
      flags = GBM_BO_TRANSFER_READ_WRITE;
      uint32_t stride;
      info->addr = gbm_bo_map(info->bo, 0, 0, handle->width, handle->height,
                              flags, &stride, &info->map_data, plane);
      if (info->addr == MAP_FAILED) {
        LOGF(ERROR) << "Failed to map buffer: " << strerror(errno);
        return MAP_FAILED;
      }
      info->usage = 1;
      buffer_info_[key] = std::move(info);
    } else {
      // We have mapped |plane| on |buffer| before: we can simply call
      // gbm_bo_map() on the existing bo.
      DCHECK(buffer_context_.find(buffer) != buffer_context_.end());
      info_cache->second->usage++;
    }
    struct MappedGrallocBufferInfo* info = buffer_info_[key].get();
    VLOGF(2) << "Plane " << plane << " of gralloc buffer 0x" << std::hex
             << handle->buffer_id << " mapped to "
             << reinterpret_cast<uintptr_t>(info->addr);
    return info->addr;
  } else if (handle->type == SHM) {
    // We can't call mmap() here because each mmap call may return different
    // mapped virtual addresses and may lead to virtual memory address leak.
    // Instead we call mmap() only once in Register.
    auto context_it = buffer_context_.find(buffer);
    if (context_it == buffer_context_.end()) {
      LOGF(ERROR) << "Unknown buffer 0x" << std::hex << handle->buffer_id;
      return MAP_FAILED;
    }
    auto buffer_context = context_it->second.get();
    void* out_addr = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(buffer_context->mapped_addr) +
        handle->offsets[plane]);
    VLOGF(2) << "Plane " << plane << " of shm buffer 0x" << std::hex
             << handle->buffer_id << " mapped to "
             << reinterpret_cast<uintptr_t>(out_addr);
    return out_addr;
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return MAP_FAILED;
  }
}

int CameraBufferMapper::Unmap(buffer_handle_t buffer, uint32_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }

  if (handle->type == GRALLOC) {
    base::AutoLock l(lock_);
    auto key = MappedGrallocBufferInfoCache::key_type(buffer, plane);
    auto info_cache = buffer_info_.find(key);
    if (info_cache == buffer_info_.end()) {
      LOGF(ERROR) << "Plane " << plane << " of buffer 0x" << std::hex
                  << handle->buffer_id << " was not mapped";
      return -EINVAL;
    }
    auto& info = info_cache->second;
    if (!--info->usage) {
      buffer_info_.erase(info_cache);
    }
  } else if (handle->type == SHM) {
    // No-op for SHM buffers.
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return -EINVAL;
  }
  VLOGF(2) << "buffer 0x" << std::hex << handle->buffer_id << " unmapped";
  return 0;
}

}  // namespace arc
