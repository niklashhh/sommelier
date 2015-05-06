// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/test_perf_data.h"

#include <ostream>  // NOLINT

#include "base/logging.h"

#include "chromiumos-wide-profiling/kernel/perf_internals.h"
#include "chromiumos-wide-profiling/utils.h"

namespace quipper {
namespace testing {

ExamplePerfDataFileHeader::ExamplePerfDataFileHeader(
    const size_t attr_count,
    const u64 data_size,
    const unsigned long features) {  // NOLINT
  CHECK_EQ(112U, sizeof(perf_file_attr)) << "perf_file_attr has changed size!";
  const size_t attrs_size = attr_count * sizeof(perf_file_attr);
  header_ = {
    .magic = kPerfMagic,
    .size = 104,
    .attr_size = sizeof(perf_file_attr),
    .attrs = {.offset = 104, .size = attrs_size},
    .data = {.offset = 104 + attrs_size, .size = data_size},
    .event_types = {0},
    .adds_features = {features, 0, 0, 0},
  };
}

void ExamplePerfDataFileHeader::WriteTo(std::ostream* out) const {
  out->write(reinterpret_cast<const char*>(&header_), sizeof(header_));
  CHECK_EQ(static_cast<u64>(out->tellp()), header_.size);
  CHECK_EQ(static_cast<u64>(out->tellp()), header_.attrs.offset);
}

void ExamplePipedPerfDataFileHeader::WriteTo(std::ostream* out) const {
  const perf_pipe_file_header header = {
    .magic = kPerfMagic,
    .size = 16,
  };
  out->write(reinterpret_cast<const char*>(&header), sizeof(header));
  CHECK_EQ(static_cast<u64>(out->tellp()), header.size);
}

void ExamplePerfEventAttrEvent_Hardware::WriteTo(std::ostream* out) const {
  // Due to the unnamed union fields (eg, sample_period), this structure can't
  // be initialized with designated initializers.
  perf_event_attr attr = {};
  attr.type = PERF_TYPE_HARDWARE;
  attr.size = sizeof(perf_event_attr);
  attr.config = config_;
  attr.sample_period = 100001;
  attr.sample_type = sample_type_;
  attr.sample_id_all = sample_id_all_;

  const attr_event event = {
    .header = {
      .type = PERF_RECORD_HEADER_ATTR,
      .misc = 0,
      .size = sizeof(attr_event),  // No ids to add to size.
    },
    .attr = attr,
  };

  out->write(reinterpret_cast<const char*>(&event), sizeof(event));
}

void ExamplePerfFileAttr_Hardware::WriteTo(std::ostream* out) const {
  // Due to the unnamed union fields (eg, sample_period), this structure can't
  // be initialized with designated initializers.
  perf_event_attr attr = {};
  attr.type = PERF_TYPE_HARDWARE;
  attr.size = sizeof(perf_event_attr);
  attr.config = config_;
  attr.sample_period = 1;
  attr.sample_type = sample_type_;
  attr.sample_id_all = sample_id_all_;

  const perf_file_attr file_attr = {
    .attr = attr,
    .ids = {.offset = 104, .size = 0},
  };
  out->write(reinterpret_cast<const char*>(&file_attr), sizeof(file_attr));
}

void ExamplePerfFileAttr_Tracepoint::WriteTo(std::ostream* out) const {
  // Due to the unnamed union fields (eg, sample_period), this structure can't
  // be initialized with designated initializers.
  perf_event_attr attr = {};
  // See kernel src: tools/perf/util/evsel.c perf_evsel__newtp()
  attr.type = PERF_TYPE_TRACEPOINT;
  attr.size = sizeof(perf_event_attr);
  attr.config = tracepoint_event_id_;
  attr.sample_period = 1;
  attr.sample_type = (PERF_SAMPLE_IP |
                      PERF_SAMPLE_TID |
                      PERF_SAMPLE_TIME |
                      PERF_SAMPLE_CPU |
                      PERF_SAMPLE_PERIOD |
                      PERF_SAMPLE_RAW);

  const perf_file_attr file_attr = {
    .attr = attr,
    .ids = {.offset = 104, .size = 0},
  };
  out->write(reinterpret_cast<const char*>(&file_attr), sizeof(file_attr));
}

void ExampleMmapEvent::WriteTo(std::ostream* out) const {
  const size_t filename_aligned_length =
      GetUint64AlignedStringLength(filename_);
  const size_t event_size =
      offsetof(struct mmap_event, filename) +
      filename_aligned_length +
      sample_id_.size();  // sample_id_all

  struct mmap_event event = {
    .header = {
      .type = PERF_RECORD_MMAP,
      .misc = 0,
      .size = static_cast<u16>(event_size),
    },
    .pid = pid_, .tid = pid_,
    .start = start_,
    .len = len_,
    .pgoff = pgoff_,
    // .filename = ..., // written separately
  };

  const size_t pre_mmap_offset = out->tellp();
  out->write(reinterpret_cast<const char*>(&event),
             offsetof(struct mmap_event, filename));
  *out << filename_
       << string(filename_aligned_length - filename_.size(), '\0');
  out->write(sample_id_.data(), sample_id_.size());
  const size_t written_event_size =
      static_cast<size_t>(out->tellp()) - pre_mmap_offset;
  CHECK_EQ(event.header.size,
           static_cast<u64>(written_event_size));
}

void ExampleMmap2Event::WriteTo(std::ostream* out) const {
  const size_t filename_aligned_length =
      GetUint64AlignedStringLength(filename_);
  const size_t event_size =
      offsetof(struct mmap2_event, filename) +
      filename_aligned_length +
      sample_id_.size();  // sample_id_all

  struct mmap2_event event = {
    .header = {
      .type = PERF_RECORD_MMAP2,
      .misc = 0,
      .size = static_cast<u16>(event_size),
    },
    .pid = pid_, .tid = pid_,
    .start = start_,
    .len = len_,
    .pgoff = pgoff_,
    .maj = 6,
    .min = 7,
    .ino = 8,
    .ino_generation = 9,
    .prot = 1|2,  // == PROT_READ | PROT_WRITE
    .flags = 2,   // == MAP_PRIVATE
    // .filename = ..., // written separately
  };

  const size_t pre_mmap_offset = out->tellp();
  out->write(reinterpret_cast<const char*>(&event),
             offsetof(struct mmap2_event, filename));
  *out << filename_
       << string(filename_aligned_length - filename_.size(), '\0');
  out->write(sample_id_.data(), sample_id_.size());
  const size_t written_event_size =
      static_cast<size_t>(out->tellp()) - pre_mmap_offset;
  CHECK_EQ(event.header.size,
           static_cast<u64>(written_event_size));
}

void FinishedRoundEvent::WriteTo(std::ostream* out) const {
  const perf_event_header event = {
    .type = PERF_RECORD_FINISHED_ROUND,
    .misc = 0,
    .size = sizeof(struct perf_event_header),
  };
  out->write(reinterpret_cast<const char*>(&event), sizeof(event));
}

void ExamplePerfSampleEvent::WriteTo(std::ostream* out) const {
  const size_t event_size =
      sizeof(struct sample_event) +
      sample_info_.size();
  const sample_event event = {
    .header = {
      .type = PERF_RECORD_SAMPLE,
      .misc = PERF_RECORD_MISC_USER,
      .size = static_cast<u16>(event_size),
    }
  };
  out->write(reinterpret_cast<const char*>(&event), sizeof(event));
  out->write(sample_info_.data(), sample_info_.size());
}

ExamplePerfSampleEvent_BranchStack::ExamplePerfSampleEvent_BranchStack()
  : ExamplePerfSampleEvent(
      SampleInfo()
      .BranchStack_nr(16)
      .BranchStack_lbr(0x00007f4a313bb8cc, 0x00007f4a313bdb40, 0x02)
      .BranchStack_lbr(0x00007f4a30ce4de2, 0x00007f4a313bb8b3, 0x02)
      .BranchStack_lbr(0x00007f4a313bb8b0, 0x00007f4a30ce4de0, 0x01)
      .BranchStack_lbr(0x00007f4a30ff45c1, 0x00007f4a313bb8a0, 0x02)
      .BranchStack_lbr(0x00007f4a30ff49f2, 0x00007f4a30ff45bb, 0x02)
      .BranchStack_lbr(0x00007f4a30ff4a98, 0x00007f4a30ff49ed, 0x02)
      .BranchStack_lbr(0x00007f4a30ff4a7c, 0x00007f4a30ff4a91, 0x02)
      .BranchStack_lbr(0x00007f4a30ff4a34, 0x00007f4a30ff4a46, 0x02)
      .BranchStack_lbr(0x00007f4a30ff4c22, 0x00007f4a30ff4a0e, 0x02)
      .BranchStack_lbr(0x00007f4a30ff4bb3, 0x00007f4a30ff4c1b, 0x01)
      .BranchStack_lbr(0x00007f4a30ff4a09, 0x00007f4a30ff4b60, 0x02)
      .BranchStack_lbr(0x00007f4a30ff49e8, 0x00007f4a30ff4a00, 0x02)
      .BranchStack_lbr(0x00007f4a30ff42db, 0x00007f4a30ff49e0, 0x02)
      .BranchStack_lbr(0x00007f4a30ff42bb, 0x00007f4a30ff42d4, 0x02)
      .BranchStack_lbr(0x00007f4a333bf88b, 0x00007f4a30ff42ac, 0x02)
      .BranchStack_lbr(0x00007f4a333bf853, 0x00007f4a333bf885, 0x02)) {
}

// Event size matching the event produced above
const size_t ExamplePerfSampleEvent_BranchStack::kEventSize =
    (1 /*perf_event_header*/ + 1 /*nr*/ + 16*3 /*lbr*/) * sizeof(u64);

void ExamplePerfSampleEvent_Tracepoint::WriteTo(std::ostream* out) const {
  const sample_event event = {
    .header = {
      .type = PERF_RECORD_SAMPLE,
      .misc = PERF_RECORD_MISC_USER,
      .size = 0x0078,
    }
  };
  const u64 sample_event_array[] = {
    0x00007f999c38d15a,  // IP
    0x0000068d0000068d,  // TID (u32 pid, tid)
    0x0001e0211cbab7b9,  // TIME
    0x0000000000000000,  // CPU
    0x0000000000000001,  // PERIOD
    0x0000004900000044,  // RAW (u32 size = 0x44 = 68 = 4 + 8*sizeof(u64))
    0x000000090000068d,  //  .
    0x0000000000000000,  //  .
    0x0000100000000000,  //  .
    0x0000000300000000,  //  .
    0x0000002200000000,  //  .
    0xffffffff00000000,  //  .
    0x0000000000000000,  //  .
    0x0000000000000000,  //  .
  };
  CHECK_EQ(event.header.size,
           sizeof(event.header) + sizeof(sample_event_array));
  out->write(reinterpret_cast<const char*>(&event), sizeof(event));
  out->write(reinterpret_cast<const char*>(sample_event_array),
             sizeof(sample_event_array));
}

// Event size matching the event produced above
const size_t ExamplePerfSampleEvent_Tracepoint::kEventSize =
    (1 /*perf_event_header*/ + 14 /*sample array*/) * sizeof(u64);

static const char kTraceMetadataValue[] =
    "\x17\x08\x44tracing0.5BLAHBLAHBLAH....";

const std::vector<char> ExampleTracingMetadata::Data::kTraceMetadata(
    kTraceMetadataValue, kTraceMetadataValue+sizeof(kTraceMetadataValue)-1);

void ExampleTracingMetadata::Data::WriteTo(std::ostream* out) const {
  const perf_file_section &index_entry = parent_->index_entry_.index_entry_;
  CHECK_EQ(static_cast<u64>(out->tellp()), index_entry.offset);
  out->write(kTraceMetadata.data(), kTraceMetadata.size());
  CHECK_EQ(static_cast<u64>(out->tellp()),
           index_entry.offset + index_entry.size);
}

}  // namespace testing
}  // namespace quipper
