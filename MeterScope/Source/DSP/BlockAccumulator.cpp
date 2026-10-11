// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// Derived from the "Loudness Meter Peak/RMS/LUFS" JSFX,
// Copyright (C) 2021 and later Cockos Incorporated (LGPL).
//
// SPDX-License-Identifier: LGPL-3.0-or-later
// This file is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See COPYING.LESSER and COPYING.

#include "BlockAccumulator.h"

namespace meterscope
{
    void BlockAccumulator::prepare(int winLenSamples) noexcept
    {
        // jsfx: i_win_len=1/(m_win_cnt*win_len); i_win_len2=1/(s_win_cnt*win_len);
        const double winLen = (double) winLenSamples;
        invWinLen = 1.0 / ((double) mWinCnt * winLen);
        invWinLen2 = 1.0 / ((double) sWinCnt * winLen);
    }

    void BlockAccumulator::reset() noexcept
    {
        curBuf = 0;
        curBuf2 = 0;
        rmsBuf.fill(0.0);
        lufsBuf.fill(0.0);
        lufsBuf2.fill(0.0);
        rmsMSumRaw = 0.0;
        lufsMSumRaw = 0.0;
        lufsSSumRaw = 0.0;
    }

    void BlockAccumulator::pushBlock(double rval, double lval) noexcept
    {
        // jsfx lines 363-377 (ring-buffer push + incremental sliding-sum
        // update). The SAME lval feeds both the 400ms window (lufsBuf, used
        // for LUFS-M) and the 3s window (lufsBuf2, used for LUFS-S) - this is
        // deliberate in the JSFX, not a duplication bug.
        const double prevRval = rmsBuf[(size_t) curBuf];
        rmsBuf[(size_t) curBuf] = rval;

        const double prevLval = lufsBuf[(size_t) curBuf];
        lufsBuf[(size_t) curBuf] = lval;

        const double prevLval2 = lufsBuf2[(size_t) curBuf2];
        lufsBuf2[(size_t) curBuf2] = lval;

        curBuf = (curBuf + 1 >= mWinCnt) ? 0 : curBuf + 1;
        curBuf2 = (curBuf2 + 1 >= sWinCnt) ? 0 : curBuf2 + 1;

        // Incremental form - REQUIRED for exact numeric replication
        // (architecture.md: "must replicate this incremental form, including
        // its double-precision rounding drift, rather than recomputing sums
        // from scratch each block").
        rmsMSumRaw += (rval - prevRval) * invWinLen;
        lufsMSumRaw += (lval - prevLval) * invWinLen;
        lufsSSumRaw += (lval - prevLval2) * invWinLen2;
    }
}
