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

#include "KWeighting.h"
#include "JsfxMath.h"

namespace meterscope
{
    // Ported verbatim from loudness_meter_cockos.jsfx's init_lufs_filters()
    // (lines 222-250). Constants are libebur128-style high-precision K-weighting
    // constants; do not round or simplify them.
    KWeightingCoefficients computeKWeightingCoefficients(double sampleRate)
    {
        KWeightingCoefficients c;

        // Stage 1: high shelf (lines 229-240)
        {
            const double db = 3.999843853973347;
            const double f0 = 1681.974450955533;
            const double Q  = 0.7071752369554196;
            const double K  = std::tan(kPi * f0 / sampleRate);
            const double Vh = std::pow(10.0, db / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;

            c.f1a1 = 2.0 * (K * K - 1.0) / a0;
            c.f1a2 = (1.0 - K / Q + K * K) / a0;
            c.f1b0 = (Vh + Vb * K / Q + K * K) / a0;
            c.f1b1 = 2.0 * (K * K - Vh) / a0;
            c.f1b2 = (Vh - Vb * K / Q + K * K) / a0;
        }

        // Stage 2: high pass (lines 242-249). DELIBERATELY not normalized by
        // its own a0 - b0=1, b1=-2, b2=1 verbatim (architecture.md Component 1).
        {
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;
            const double K  = std::tan(kPi * f0 / sampleRate);
            const double denom = 1.0 + K / Q + K * K;

            c.f2a1 = 2.0 * (K * K - 1.0) / denom;
            c.f2a2 = (1.0 - K / Q + K * K) / denom;
            c.f2b0 = 1.0;
            c.f2b1 = -2.0;
            c.f2b2 = 1.0;
        }

        return c;
    }
}
