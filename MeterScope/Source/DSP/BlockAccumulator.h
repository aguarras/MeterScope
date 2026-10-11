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

#pragma once

#include <array>
#include <cstdint>

// Component 3: 100ms Block Accumulator & Sliding-Window Sums (architecture.md
// Core Components #3). Ported from loudness_meter_cockos.jsfx lines 88-95
// (window-length/count constants), 203-219 (per-sample rval/lval
// accumulation - owned by MeteringEngine, NOT this class, see below) and
// 359-377 (block-boundary ring-buffer push + incremental sliding-sum update).
//
// Scope note: this class owns ONLY the three sliding-window sums (400ms RMS,
// 400ms LUFS-M, 3s LUFS-S) and their ring buffers - the exact incremental form
// `sum += (new-old)*invWinLen` (architecture.md: "must replicate this
// incremental form... rather than recomputing sums from scratch each block").
// The per-channel win_pos==0 reset of ch_rms_sum/ch_lufs_sum, and the
// win_cnt-gated dB/held-max conversion (which needs the block counter that
// MeteringEngine already owns for the Reset & Transport Controller), live in
// MeteringEngine itself - a structural split, not a numeric one: every value
// this class would otherwise expose in dB is bit-identical either way.

namespace meterscope
{
    class BlockAccumulator
    {
    public:
        static constexpr int mWinCnt = 4;  // 400ms = 4 * 100ms blocks (RMS-M, LUFS-M)
        static constexpr int sWinCnt = 30; // 3s    = 30 * 100ms blocks (LUFS-S)

        // winLenSamples = floor(0.1 * sampleRate) (jsfx: `win_len=(LOUD_METER_UPDATE*srate)|0`,
        // LOUD_METER_UPDATE=0.1). invWinLen/invWinLen2 are `i_win_len`/`i_win_len2`
        // in the JSFX (`1/(m_win_cnt*win_len)`, `1/(s_win_cnt*win_len)`).
        void prepare(int winLenSamples) noexcept;

        // Clears the ring buffers and sliding sums (part of Component 6's
        // Reset(), called from MeteringEngine::reset()).
        void reset() noexcept;

        // Called exactly once per 100ms block boundary, with this block's
        // total (summed-across-channels) raw RMS energy (rval) and K-weighted
        // LUFS energy (lval) - jsfx lines 359-377. Updates rmsMSumRaw/
        // lufsMSumRaw/lufsSSumRaw (read via the public members below).
        void pushBlock(double rval, double lval) noexcept;

        // Raw (linear energy, NOT dB) sliding-sum values, valid immediately
        // after pushBlock() returns. MeteringEngine applies the win_cnt gate
        // and the dB conversion (Component 3's `sum>0 && win_cnt>=N` pattern).
        double rmsMSumRaw = 0.0;
        double lufsMSumRaw = 0.0;
        double lufsSSumRaw = 0.0;

    private:
        double invWinLen = 0.0;
        double invWinLen2 = 0.0;

        int curBuf = 0;
        int curBuf2 = 0;

        std::array<double, mWinCnt> rmsBuf {};
        std::array<double, mWinCnt> lufsBuf {};
        std::array<double, sWinCnt> lufsBuf2 {};
    };
}
