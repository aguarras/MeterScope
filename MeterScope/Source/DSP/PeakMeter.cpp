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

#include "PeakMeter.h"
#include <cmath>

namespace meterscope
{
    void processPeakSample(PeakChannelState& state, PeakMode mode, double rawSample) noexcept
    {
        double pspl = 0.0;

        // jsfx: `cfg_peak == 1 || cfg_peak == 2 ? ( ...true peak sinc... ) :
        //        cfg_peak == 3 || cfg_peak == 4 ? ( pspl = abs(lspl); );`
        if (mode == PeakMode::TruePeak || mode == PeakMode::TruePeakClips)
        {
            pspl = state.tp.pushAndGetPeak(rawSample);
        }
        else if (mode == PeakMode::Peak || mode == PeakMode::PeakClips)
        {
            pspl = std::abs(rawSample);
        }
        // PeakMode::Off: neither branch runs, matching "Off computes nothing"
        // (architecture.md Component 2) - pspl stays 0.0 and is never used
        // below since the whole update block is gated on `mode != Off`.

        if (mode != PeakMode::Off)
        {
            // jsfx: `pspl > pkval ? (pkval=pspl; pspl>hipkval?(hipkval=pspl;...);); `
            if (pspl > state.pkval)
            {
                state.pkval = pspl;
                if (pspl > state.hipkval)
                    state.hipkval = pspl;
            }

            // jsfx: `pspl > 1.0 ? clips += 1;` - sibling to the pkval update,
            // not nested inside it; counts once per INPUT sample whose
            // (possibly interpolated) max exceeds 1.0, not once per
            // interpolated sub-position (errata #3) - already satisfied here
            // since pushAndGetPeak() is called exactly once per input sample.
            if (pspl > 1.0)
                state.clips += 1;
        }
    }
}
