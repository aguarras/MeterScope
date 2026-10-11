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

#include "LraEngine.h"
#include "JsfxMath.h"
#include <algorithm>

namespace meterscope
{
    void LraEngine::reset() noexcept
    {
        binCount.fill(0);
        lufsBSum = 0.0;
        lufsBSumCnt = 0;
        // jsfx Reset(): `lra_db_diff = 0; lra_db_hi = lra_db_lo = ... = -100;`
        // LRA lo/hi/diff ARE cleared by reset - unlike the "hold previous
        // value on insufficient gated count" behaviour below, which only
        // applies to the running (non-reset) state.
        lraLoDb = -100.0;
        lraHiDb = -100.0;
        lraDiff = 0.0;
    }

    void LraEngine::pushShortTermBlock(double lufsSDb, double lufsSSumRaw, bool isPlayingOrRecording) noexcept
    {
        // jsfx: `b = WANT_INTEGRATED_ALWAYS || (play_state&1) ? ((lufs_s_db+70)*BINS_PER_DB)|0 : -1;`
        const int b = isPlayingOrRecording ? truncToInt((lufsSDb + 70.0) * kBinsPerDb) : -1;
        if (b < 0)
            return;

        const int bIdx = (b >= kNumBins) ? (kNumBins - 1) : b;

        lufsBSum += lufsSSumRaw;
        lufsBSumCnt += 1;
        binCount[(size_t) bIdx] += 1;

        // jsfx: `lufs_b_db=-0.691+log(lufs_b_sum/lufs_b_sum_cnt)*10/log(10);`
        const double lufsBDb = -0.691 + eel2PowerRatioToDb(lufsBSum / (double) lufsBSumCnt);

        // jsfx: `lufs_b_gate=((lufs_b_db-20+70)*BINS_PER_DB)|0;`
        const int lufsBGateRaw = truncToInt((lufsBDb - 20.0 + 70.0) * kBinsPerDb);
        // Errata #6b: clamp to 0 for every downstream use (count loop AND both
        // percentile searches) - replicates the JSFX's own empirically-zero
        // out-of-bounds read for a negative gate bin.
        const int gate = std::max(lufsBGateRaw, 0);

        std::uint64_t lraCnt = 0;
        for (int bin = gate; bin < kNumBins; ++bin)
            lraCnt += binCount[(size_t) bin];

        if (lraCnt < 20)
            return; // errata #6a: hold previous lraLoDb/lraHiDb/lraDiff unchanged.

        std::uint64_t lraCntLo = 0;
        std::uint64_t lraCntHi = 0;

        int bin = gate;
        while (bin < kNumBins && (double) lraCntLo < (double) lraCnt * 0.10)
        {
            lraCntLo += binCount[(size_t) bin];
            bin += 1;
        }
        const int binLo = bin - 1;

        bin = kNumBins - 1;
        while (bin >= gate && (double) lraCntHi < (double) lraCnt * 0.05)
        {
            lraCntHi += binCount[(size_t) bin];
            bin -= 1;
        }
        const int binHi = bin + 1;

        lraLoDb = (double) binLo * kDbPerBin - 70.0;
        lraHiDb = (double) binHi * kDbPerBin - 70.0;
        lraDiff = lraHiDb - lraLoDb;
    }
}
