#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstddef>
#include <cstdint>

#include <rex/graphics/xenos.h>

namespace rex::graphics {

// Guest memory helpers for occlusion query ZPD reports.
struct XenosZPDReport {
  static constexpr uint32_t kRecordSizeBytes = 0x20;
  static constexpr uint32_t kRecordAlignMask = ~(kRecordSizeBytes - 1);

  // Each slot holds one BEGIN record and one END record.
  // END is at the slot base, and BEGIN is +0x20.
  static constexpr uint32_t kSlotSizeBytes = 0x40;
  static constexpr uint32_t kSlotAlignMask = ~(kSlotSizeBytes - 1);

  static constexpr uint32_t kBatchPageSizeBytes = 0x1000;
  static constexpr uint32_t kBatchPageAlignMask = ~(kBatchPageSizeBytes - 1);

  static constexpr uint32_t GetRecordBase(uint32_t address) {
    return address & kRecordAlignMask;
  }

  static constexpr uint32_t GetSlotBase(uint32_t address) {
    return address & kSlotAlignMask;
  }

  static constexpr uint32_t GetBatchPageBase(uint32_t address) {
    return GetRecordBase(address) & kBatchPageAlignMask;
  }

  static constexpr uint32_t GetBeginRecordBase(uint32_t address) {
    return GetSlotBase(address) + kRecordSizeBytes;
  }

  static constexpr uint32_t GetEndRecordBase(uint32_t address) {
    return GetSlotBase(address);
  }

  static constexpr bool IsBeginRecord(uint32_t address) {
    uint32_t record_base = GetRecordBase(address);
    return record_base && record_base == GetBeginRecordBase(record_base);
  }

  static constexpr bool IsEndRecord(uint32_t address) {
    uint32_t record_base = GetRecordBase(address);
    return record_base && record_base == GetEndRecordBase(record_base);
  }

  static constexpr bool IsBatchStep(uint32_t last_record_base,
                                    uint32_t record_base) {
    return last_record_base != 0 &&
           record_base == last_record_base + kRecordSizeBytes;
  }

  // Boundary detection only looks at ZPass_A first, then ZFail_A.
  static bool HasPendingSentinel(
      const xenos::xe_gpu_depth_sample_counts* report) {
    constexpr uint32_t kSentinelLE = 0xEDFEFFFFu;
    constexpr uint32_t kSentinelBE = 0xFFFFFEEDu;

    if (report->ZPass_A == kSentinelLE || report->ZPass_A == kSentinelBE) {
      return true;
    }
    if (report->ZFail_A == kSentinelLE || report->ZFail_A == kSentinelBE) {
      return true;
    }
    return false;
  }

  // Total_A mirrors ZPass_A and the rest are zeroed out since host queries can
  // only provide a passing count.
  static void WriteSampleCount(xenos::xe_gpu_depth_sample_counts* report,
                               uint32_t sample_count) {
    report->Total_A = sample_count;
    report->Total_B = 0;
    report->ZFail_A = 0;
    report->ZFail_B = 0;
    report->ZPass_A = sample_count;
    report->ZPass_B = 0;
    report->StencilFail_A = 0;
    report->StencilFail_B = 0;
  }

  static constexpr uint32_t AddSamples(uint32_t value, uint32_t step) {
    return value > UINT32_MAX - step ? UINT32_MAX : value + step;
  }

  static void WriteReportDelta(xenos::xe_gpu_depth_sample_counts* begin_report,
                               xenos::xe_gpu_depth_sample_counts* end_report,
                               uint32_t begin_value, uint32_t delta_value,
                               bool write_begin_report) {
    uint64_t end_value =
        static_cast<uint64_t>(begin_value) + static_cast<uint64_t>(delta_value);
    uint32_t clamped_value =
        end_value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(end_value);

    if (write_begin_report && begin_report && end_report != begin_report) {
      WriteSampleCount(begin_report, begin_value);
    }
    WriteSampleCount(end_report, clamped_value);
  }
};

}  // namespace rex::graphics
