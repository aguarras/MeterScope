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

#include "TruePeakInterpolator.h"
#include "JsfxMath.h"
#include <cmath>
#include <algorithm>

namespace meterscope
{
    double sincGenVal(double pos) noexcept
    {
        // jsfx: windowpos = (2*pi/sinc_sz) * slice_pos;  (RAW position)
        //       sincpos   = pi * (slice_pos - sinc_sz*0.5);  (CENTRED position)
        //       result    = (0.53836 - cos(windowpos)*0.46164) * sin(sincpos) / sincpos;
        const double windowpos = (2.0 * kPi / (double) kTruePeakTapCount) * pos;
        const double sincpos = kPi * (pos - (double) kTruePeakTapCount * 0.5);
        return (0.53836 - std::cos(windowpos) * 0.46164) * std::sin(sincpos) / sincpos;
    }

    SincTapSet sincGenSlice(double cs) noexcept
    {
        SincTapSet taps {};
        for (int x = 0; x < kTruePeakTapCount; ++x)
            taps[(size_t) x] = sincGenVal(cs + (double) x);
        return taps;
    }

    void TruePeakInterpolator::prepare(double sampleRate) noexcept
    {
        // jsfx sinc_init() (lines 116-122).
        oversample4x = sampleRate < 96000.0;

        if (oversample4x)
        {
            s1 = sincGenSlice(0.25);
            s2 = sincGenSlice(0.5);
            s3 = sincGenSlice(0.75);
        }
        else
        {
            s1 = sincGenSlice(0.5);
            // s2/s3 unused in the >=96000 branch - left zero-initialized.
        }
    }

    void TruePeakInterpolator::reset() noexcept
    {
        history.fill(0.0);
    }

    double TruePeakInterpolator::dot(const std::array<double, kTruePeakTapCount>& h,
                                      const SincTapSet& taps) noexcept
    {
        double sum = 0.0;
        for (int x = 0; x < kTruePeakTapCount; ++x)
            sum += h[(size_t) x] * taps[(size_t) x];
        return sum;
    }

    double TruePeakInterpolator::pushAndGetPeak(double rawSample) noexcept
    {
        // jsfx lines 178-182: shift history outward, newest sample enters h[0].
        for (int x = kTruePeakTapCount - 1; x > 0; --x)
            history[(size_t) x] = history[(size_t) (x - 1)];
        history[0] = rawSample;

        // jsfx line 184: h[16] (16-sample-delayed raw sample), NOT h[0].
        double pspl = std::abs(history[16]);
        pspl = std::max(pspl, std::abs(dot(history, s1)));

        if (oversample4x)
        {
            pspl = std::max(pspl, std::abs(dot(history, s2)));
            pspl = std::max(pspl, std::abs(dot(history, s3)));
        }

        return pspl;
    }
}
