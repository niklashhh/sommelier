/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/image_processor.h"

#include <errno.h>
#include <libyuv.h>
#include <time.h>

#include "arc/common.h"
#include "arc/exif_utils.h"
#include "arc/jpeg_compressor.h"
#include "hal/usb/common_types.h"

namespace arc {

/*
 * Formats have different names in different header files. Here is the mapping
 * table:
 *
 * android_pixel_format_t          videodev2.h           FOURCC in libyuv
 * -----------------------------------------------------------------------------
 * HAL_PIXEL_FORMAT_YV12         = V4L2_PIX_FMT_YVU420 = FOURCC_YV12
 * HAL_PIXEL_FORMAT_YCrCb_420_SP = V4L2_PIX_FMT_NV21   = FOURCC_NV21
 * HAL_PIXEL_FORMAT_RGBA_8888    = V4L2_PIX_FMT_RGBX32 = FOURCC_ABGR
 * HAL_PIXEL_FORMAT_YCbCr_422_I  = V4L2_PIX_FMT_YUYV   = FOURCC_YUYV
 *                                                     = FOURCC_YUY2
 *                                 V4L2_PIX_FMT_YUV420 = FOURCC_I420
 *                                                     = FOURCC_YU12
 *                                 V4L2_PIX_FMT_MJPEG  = FOURCC_MJPG
 *
 * Camera device generates FOURCC_YUYV and FOURCC_MJPG.
 * Preview needs FOURCC_ARGB format.
 * Software video encoder needs FOURCC_YU12.
 * CTS requires FOURCC_YV12 and FOURCC_NV21 for applications.
 *
 * Android stride requirement:
 * YV12 horizontal stride should be a multiple of 16 pixels. See
 * android.graphics.ImageFormat.YV12.
 * The stride of ARGB, YU12, and NV21 are always equal to the width.
 *
 * Conversion Path:
 * MJPG/YUYV (from camera) -> YU12 -> ARGB (preview)
 *                                 -> NV21 (apps)
 *                                 -> YV12 (apps)
 *                                 -> YU12 (video encoder)
 */

// YV12 horizontal stride should be a multiple of 16 pixels for each plane.
// |dst_stride_uv| is the pixel stride of u or v plane.
static int YU12ToYV12(const void* yv12,
                      void* yu12,
                      int width,
                      int height,
                      int dst_stride_y,
                      int dst_stride_uv);
static int YU12ToNV21(const void* yv12, void* nv21, int width, int height);
static bool ConvertToJpeg(const CameraMetadata& metadata,
                          const FrameBuffer& in_frame,
                          FrameBuffer* out_frame);
static bool SetExifTags(const CameraMetadata& metadata, ExifUtils* utils);

// How precise the float-to-rational conversion for EXIF tags would be.
static const int kRationalPrecision = 10000;

inline static size_t Align16(size_t value) {
  return (value + 15) & ~15;
}

size_t ImageProcessor::GetConvertedSize(int fourcc,
                                        uint32_t width,
                                        uint32_t height) {
  if ((width % 2) || (height % 2)) {
    LOGF(ERROR) << "Width or height is not even (" << width << " x " << height
                << ")";
    return 0;
  }

  switch (fourcc) {
    case V4L2_PIX_FMT_YVU420:  // YV12
      return Align16(width) * height + Align16(width / 2) * height;
    case V4L2_PIX_FMT_YUV420:  // YU12
    // Fall-through.
    case V4L2_PIX_FMT_NV21:  // NV21
      return width * height * 3 / 2;
    case V4L2_PIX_FMT_RGBX32:
      return width * height * 4;
    default:
      LOGF(ERROR) << "Pixel format " << FormatToString(fourcc)
                  << " is unsupported.";
      return 0;
  }
}

int ImageProcessor::ConvertFormat(const CameraMetadata& metadata,
                                  const FrameBuffer& in_frame,
                                  FrameBuffer* out_frame) {
  if ((in_frame.GetWidth() % 2) || (in_frame.GetHeight() % 2)) {
    LOGF(ERROR) << "Width or height is not even (" << in_frame.GetWidth()
                << " x " << in_frame.GetHeight() << ")";
    return -EINVAL;
  }

  if (out_frame->GetFourcc() != V4L2_PIX_FMT_JPEG) {
    size_t data_size = GetConvertedSize(
        out_frame->GetFourcc(), in_frame.GetWidth(), in_frame.GetHeight());

    if (out_frame->SetDataSize(data_size)) {
      LOGF(ERROR) << "Set data size failed";
      return -EINVAL;
    }
  }

  if (in_frame.GetFourcc() == V4L2_PIX_FMT_YUYV) {
    switch (out_frame->GetFourcc()) {
      case V4L2_PIX_FMT_YUV420:  // YU12
      {
        int res = libyuv::YUY2ToI420(
            in_frame.GetData(), in_frame.GetWidth() * 2, out_frame->GetData(),
            out_frame->GetWidth(),
            out_frame->GetData() +
                out_frame->GetWidth() * out_frame->GetHeight(),
            out_frame->GetWidth() / 2,
            out_frame->GetData() +
                out_frame->GetWidth() * out_frame->GetHeight() * 5 / 4,
            out_frame->GetWidth() / 2, in_frame.GetWidth(),
            in_frame.GetHeight());
        LOGF_IF(ERROR, res) << "YUY2ToI420() for YU12 returns " << res;
        return res ? -EINVAL : 0;
      }
      default:
        LOGF(ERROR) << "Destination pixel format "
                    << FormatToString(out_frame->GetFourcc())
                    << " is unsupported for YUYV source format.";
        return -EINVAL;
    }
  } else if (in_frame.GetFourcc() == V4L2_PIX_FMT_YUV420) {
    // V4L2_PIX_FMT_YVU420 is YV12. I420 is usually referred to YU12
    // (V4L2_PIX_FMT_YUV420), and YV12 is similar to YU12 except that U/V
    // planes are swapped.
    switch (out_frame->GetFourcc()) {
      case V4L2_PIX_FMT_YVU420:  // YV12
      {
        int ystride = Align16(in_frame.GetWidth());
        int uvstride = Align16(in_frame.GetWidth() / 2);
        int res = YU12ToYV12(in_frame.GetData(), out_frame->GetData(),
                             in_frame.GetWidth(), in_frame.GetHeight(), ystride,
                             uvstride);
        LOGF_IF(ERROR, res) << "YU12ToYV12() returns " << res;
        return res ? -EINVAL : 0;
      }
      case V4L2_PIX_FMT_YUV420:  // YU12
      {
        memcpy(out_frame->GetData(), in_frame.GetData(),
               in_frame.GetDataSize());
        return 0;
      }
      case V4L2_PIX_FMT_NV21:  // NV21
      {
        // TODO(henryhsu): Use libyuv::I420ToNV21.
        int res = YU12ToNV21(in_frame.GetData(), out_frame->GetData(),
                             in_frame.GetWidth(), in_frame.GetHeight());
        LOGF_IF(ERROR, res) << "YU12ToNV21() returns " << res;
        return res ? -EINVAL : 0;
      }
      case V4L2_PIX_FMT_RGBX32: {
        int res = libyuv::I420ToABGR(
            in_frame.GetData(), in_frame.GetWidth(),
            in_frame.GetData() + in_frame.GetWidth() * in_frame.GetHeight(),
            in_frame.GetWidth() / 2,
            in_frame.GetData() +
                in_frame.GetWidth() * in_frame.GetHeight() * 5 / 4,
            in_frame.GetWidth() / 2, out_frame->GetData(),
            out_frame->GetWidth() * 4, in_frame.GetWidth(),
            in_frame.GetHeight());
        LOGF_IF(ERROR, res) << "I420ToABGR() returns " << res;
        return res ? -EINVAL : 0;
      }
      case V4L2_PIX_FMT_JPEG: {
        int res = ConvertToJpeg(metadata, in_frame, out_frame);
        LOGF_IF(ERROR, res) << "ConvertToJpeg() returns " << res;
        return res ? -EINVAL : 0;
      }
      default:
        LOGF(ERROR) << "Destination pixel format "
                    << FormatToString(out_frame->GetFourcc())
                    << " is unsupported for YU12 source format.";
        return -EINVAL;
    }
  } else if (in_frame.GetFourcc() == V4L2_PIX_FMT_MJPEG) {
    switch (out_frame->GetFourcc()) {
      case V4L2_PIX_FMT_YUV420:  // YU12
      {
        int res = libyuv::MJPGToI420(
            in_frame.GetData(), in_frame.GetDataSize(), out_frame->GetData(),
            out_frame->GetWidth(),
            out_frame->GetData() +
                out_frame->GetWidth() * out_frame->GetHeight(),
            out_frame->GetWidth() / 2,
            out_frame->GetData() +
                out_frame->GetWidth() * out_frame->GetHeight() * 5 / 4,
            out_frame->GetWidth() / 2, in_frame.GetWidth(),
            in_frame.GetHeight(), out_frame->GetWidth(),
            out_frame->GetHeight());
        LOGF_IF(ERROR, res) << "MJPEGToI420() returns " << res;
        return res ? -EINVAL : 0;
      }
      default:
        LOGF(ERROR) << "Destination pixel format "
                    << FormatToString(out_frame->GetFourcc())
                    << " is unsupported for MJPEG source format.";
        return -EINVAL;
    }
  } else {
    LOGF(ERROR) << "Convert format doesn't support source format "
                << FormatToString(in_frame.GetFourcc());
    return -EINVAL;
  }
}

int ImageProcessor::Scale(const FrameBuffer& in_frame, FrameBuffer* out_frame) {
  if (in_frame.GetFourcc() != V4L2_PIX_FMT_YUV420) {
    LOGF(ERROR) << "Pixel format " << FormatToString(in_frame.GetFourcc())
                << " is unsupported.";
    return -EINVAL;
  }

  size_t data_size = GetConvertedSize(
      in_frame.GetFourcc(), out_frame->GetWidth(), out_frame->GetHeight());

  if (out_frame->SetDataSize(data_size)) {
    LOGF(ERROR) << "Set data size failed";
    return -EINVAL;
  }
  out_frame->SetFourcc(in_frame.GetFourcc());

  VLOGF(1) << "Scale image from " << in_frame.GetWidth() << "x"
           << in_frame.GetHeight() << " to " << out_frame->GetWidth() << "x"
           << out_frame->GetHeight();

  int ret = libyuv::I420Scale(
      in_frame.GetData(), in_frame.GetWidth(),
      in_frame.GetData() + in_frame.GetWidth() * in_frame.GetHeight(),
      in_frame.GetWidth() / 2,
      in_frame.GetData() + in_frame.GetWidth() * in_frame.GetHeight() * 5 / 4,
      in_frame.GetWidth() / 2, in_frame.GetWidth(), in_frame.GetHeight(),
      out_frame->GetData(), out_frame->GetWidth(),
      out_frame->GetData() + out_frame->GetWidth() * out_frame->GetHeight(),
      out_frame->GetWidth() / 2,
      out_frame->GetData() +
          out_frame->GetWidth() * out_frame->GetHeight() * 5 / 4,
      out_frame->GetWidth() / 2, out_frame->GetWidth(), out_frame->GetHeight(),
      libyuv::FilterMode::kFilterNone);
  LOGF_IF(ERROR, ret) << "I420Scale failed: " << ret;
  return ret;
}

static int YU12ToYV12(const void* yu12,
                      void* yv12,
                      int width,
                      int height,
                      int dst_stride_y,
                      int dst_stride_uv) {
  if ((width % 2) || (height % 2)) {
    LOGF(ERROR) << "Width or height is not even (" << width << " x " << height
                << ")";
    return -EINVAL;
  }
  if (dst_stride_y < width || dst_stride_uv < width / 2) {
    LOGF(ERROR) << "Y plane stride (" << dst_stride_y
                << ") or U/V plane stride (" << dst_stride_uv
                << ") is invalid for width " << width;
    return -EINVAL;
  }

  const uint8_t* src = reinterpret_cast<const uint8_t*>(yu12);
  uint8_t* dst = reinterpret_cast<uint8_t*>(yv12);
  const uint8_t* u_src = src + width * height;
  uint8_t* u_dst = dst + dst_stride_y * height + dst_stride_uv * height / 2;
  const uint8_t* v_src = src + width * height * 5 / 4;
  uint8_t* v_dst = dst + dst_stride_y * height;

  return libyuv::I420Copy(src, width, u_src, width / 2, v_src, width / 2, dst,
                          dst_stride_y, u_dst, dst_stride_uv, v_dst,
                          dst_stride_uv, width, height);
}

static int YU12ToNV21(const void* yu12, void* nv21, int width, int height) {
  if ((width % 2) || (height % 2)) {
    LOGF(ERROR) << "Width or height is not even (" << width << " x " << height
                << ")";
    return -EINVAL;
  }

  const uint8_t* src = reinterpret_cast<const uint8_t*>(yu12);
  uint8_t* dst = reinterpret_cast<uint8_t*>(nv21);
  const uint8_t* u_src = src + width * height;
  const uint8_t* v_src = src + width * height * 5 / 4;
  uint8_t* vu_dst = dst + width * height;

  memcpy(dst, src, width * height);

  for (int i = 0; i < height / 2; i++) {
    for (int j = 0; j < width / 2; j++) {
      *vu_dst++ = *v_src++;
      *vu_dst++ = *u_src++;
    }
  }
  return 0;
}

static bool ConvertToJpeg(const CameraMetadata& metadata,
                          const FrameBuffer& in_frame,
                          FrameBuffer* out_frame) {
  ExifUtils utils;
  int jpeg_quality, thumbnail_jpeg_quality;
  camera_metadata_ro_entry entry = metadata.Find(ANDROID_JPEG_QUALITY);
  if (entry.count) {
    jpeg_quality = entry.data.u8[0];
  } else {
    LOGF(ERROR) << "Cannot find jpeg quality in metadata.";
    return false;
  }
  if (metadata.Exists(ANDROID_JPEG_THUMBNAIL_QUALITY)) {
    entry = metadata.Find(ANDROID_JPEG_THUMBNAIL_QUALITY);
    thumbnail_jpeg_quality = entry.data.u8[0];
  } else {
    thumbnail_jpeg_quality = jpeg_quality;
  }

  if (!utils.Initialize(in_frame.GetData(), in_frame.GetWidth(),
                        in_frame.GetHeight(), thumbnail_jpeg_quality)) {
    LOGF(ERROR) << "ExifUtils initialization failed.";
    return false;
  }
  if (!SetExifTags(metadata, &utils)) {
    LOGF(ERROR) << "Setting Exif tags failed.";
    return false;
  }
  if (!utils.GenerateApp1()) {
    LOGF(ERROR) << "Generating APP1 segment failed.";
    return false;
  }
  JpegCompressor compressor;
  if (!compressor.CompressImage(in_frame.GetData(), in_frame.GetWidth(),
                                in_frame.GetHeight(), jpeg_quality,
                                utils.GetApp1Buffer(), utils.GetApp1Length())) {
    LOGF(ERROR) << "JPEG image compression failed";
    return false;
  }
  size_t buffer_length = compressor.GetCompressedImageSize();
  memcpy(out_frame->GetData(), compressor.GetCompressedImagePtr(),
         buffer_length);

  camera3_jpeg_blob_t blob;
  blob.jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
  blob.jpeg_size = buffer_length;
  memcpy(out_frame->GetData() + out_frame->GetBufferSize() - sizeof(blob),
         &blob, sizeof(blob));
  return true;
}

static bool SetExifTags(const CameraMetadata& metadata, ExifUtils* utils) {
  time_t raw_time = 0;
  struct tm time_info;
  bool time_available = time(&raw_time) != -1;
  localtime_r(&raw_time, &time_info);
  if (!utils->SetDateTime(time_info)) {
    LOGF(ERROR) << "Setting data time failed.";
    return false;
  }

  float focal_length;
  camera_metadata_ro_entry entry = metadata.Find(ANDROID_LENS_FOCAL_LENGTH);
  if (entry.count) {
    focal_length = entry.data.f[0];
  } else {
    LOGF(ERROR) << "Cannot find focal length in metadata.";
    return false;
  }
  if (!utils->SetFocalLength(
          static_cast<uint32_t>(focal_length * kRationalPrecision),
          kRationalPrecision)) {
    LOGF(ERROR) << "Setting focal length failed.";
    return false;
  }

  if (metadata.Exists(ANDROID_JPEG_GPS_COORDINATES)) {
    entry = metadata.Find(ANDROID_JPEG_GPS_COORDINATES);
    if (entry.count < 3) {
      LOGF(ERROR) << "Gps coordinates in metadata is not complete.";
      return false;
    }
    if (!utils->SetGpsLatitude(entry.data.d[0])) {
      LOGF(ERROR) << "Setting gps latitude failed.";
      return false;
    }
    if (!utils->SetGpsLongitude(entry.data.d[1])) {
      LOGF(ERROR) << "Setting gps longitude failed.";
      return false;
    }
    if (!utils->SetGpsAltitude(entry.data.d[2])) {
      LOGF(ERROR) << "Setting gps altitude failed.";
      return false;
    }
  }

  if (metadata.Exists(ANDROID_JPEG_GPS_PROCESSING_METHOD)) {
    entry = metadata.Find(ANDROID_JPEG_GPS_PROCESSING_METHOD);
    std::string method_str(reinterpret_cast<const char*>(entry.data.u8));
    if (!utils->SetGpsProcessingMethod(method_str)) {
      LOGF(ERROR) << "Setting gps processing method failed.";
      return false;
    }
  }

  if (time_available && metadata.Exists(ANDROID_JPEG_GPS_TIMESTAMP)) {
    entry = metadata.Find(ANDROID_JPEG_GPS_TIMESTAMP);
    time_t timestamp = static_cast<time_t>(entry.data.i64[0]);
    if (gmtime_r(&timestamp, &time_info)) {
      if (!utils->SetGpsTimestamp(time_info)) {
        LOGF(ERROR) << "Setting gps timestamp failed.";
        return false;
      }
    } else {
      LOGF(ERROR) << "Time tranformation failed.";
      return false;
    }
  }

  if (metadata.Exists(ANDROID_JPEG_ORIENTATION)) {
    entry = metadata.Find(ANDROID_JPEG_ORIENTATION);
    if (!utils->SetOrientation(entry.data.i32[0])) {
      LOGF(ERROR) << "Setting orientation failed.";
      return false;
    }
  }

  if (metadata.Exists(ANDROID_JPEG_THUMBNAIL_SIZE)) {
    entry = metadata.Find(ANDROID_JPEG_THUMBNAIL_SIZE);
    if (entry.count < 2) {
      LOGF(ERROR) << "Thumbnail size in metadata is not complete.";
      return false;
    }
    int thumbnail_width = entry.data.i32[0];
    int thumbnail_height = entry.data.i32[1];
    if (thumbnail_width > 0 && thumbnail_height > 0) {
      if (!utils->SetThumbnailSize(static_cast<uint16_t>(thumbnail_width),
                                   static_cast<uint16_t>(thumbnail_height))) {
        LOGF(ERROR) << "Setting thumbnail size failed.";
        return false;
      }
    }
  }
  return true;
}

}  // namespace arc