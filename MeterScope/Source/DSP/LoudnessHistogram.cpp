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

#include "LoudnessHistogram.h"
#include "JsfxMath.h"
#include <algorithm>

namespace meterscope
{
    void LufsIEngine::reset() noexcept
    {
        binCount.fill(0);
        binEnergy.fill(0.0);
        lufsASum = 0.0;
        lufsASumCnt = 0;
        lufsIDb = -100.0;
        dbHist.fill(0);
        dbHistMax = 0;
    }

    void LufsIEngine::pushMomentaryBlock(double lufsMDb, double lufsMSumRaw, bool isPlayingOrRecording) noexcept
    {
        // jsfx: `a = WANT_INTEGRATED_ALWAYS || (play_state&1) ? ((lufs_m_db+70)*BINS_PER_DB)|0 : -1;`
        // errata #1: `|0` truncates toward zero, not floor - the effective
        // absolute gate is -70.1 LUFS, not -70.0 (a value between -70.1 and
        // -70.0 lands in bin 0 and is NOT discarded).
        const int a = isPlayingOrRecording ? truncToInt((lufsMDb + 70.0) * kBinsPerDb) : -1;
        if (a < 0)
            return; // matches jsfx's `a >= 0 ? ( ... )` - no accumulation, nothing changes.

        const int aIdx = (a >= kNumBins) ? (kNumBins - 1) : a;

        lufsASum += lufsMSumRaw;
        lufsASumCnt += 1;
        binCount[(size_t) aIdx] += 1;
        binEnergy[(size_t) aIdx] += lufsMSumRaw;

        // jsfx: `db_hist_max = max(db_hist_max, db_hist[min(a*DB_PER_BIN,74)] += 1);`
        // EEL2 array indexing truncates a non-integer index toward zero, same
        // as static_cast<int> here (a*kDbPerBin is always >= 0).
        const double dbHistIndexF = std::min((double) aIdx * kDbPerBin, 74.0);
        const int dbHistIndex = (int) dbHistIndexF;
        dbHist[(size_t) dbHistIndex] += 1;
        dbHistMax = std::max(dbHistMax, dbHist[(size_t) dbHistIndex]);

        // jsfx: `lufs_a_db=-0.691+log(lufs_a_sum/lufs_a_sum_cnt)*10/log(10);`
        const double lufsADb = -0.691 + eel2PowerRatioToDb(lufsASum / (double) lufsASumCnt);

        // jsfx: `lufs_a_gate=((lufs_a_db-10+70)*BINS_PER_DB)|0;`
        const int lufsAGate = truncToInt((lufsADb - 10.0 + 70.0) * kBinsPerDb);
        const int gateBin = std::max(lufsAGate, 0);

        double sumEnergy = 0.0;
        std::uint64_t sumCount = 0;
        for (int bin = gateBin; bin < kNumBins; ++bin)
        {
            sumCount += binCount[(size_t) bin];
            sumEnergy += binEnergy[(size_t) bin];
        }

        lufsIDb = sumEnergy > 0.0 ? (-0.691 + eel2PowerRatioToDb(sumEnergy / (double) sumCount)) : -100.0;
    }
}
