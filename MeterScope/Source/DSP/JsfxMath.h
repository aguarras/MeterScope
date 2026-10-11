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

// Small, pure, header-only helpers shared by every Phase 2.1 DSP component.
// Deliberately framework-free (no JUCE includes) so each one is trivially
// unit-testable in isolation, per architecture.md's Mitigation strategy #1
// ("implement Components 1-7 as small, independently unit-testable
// functions/classes... verified against hand-computed values from the JSFX
// source with zero audio I/O").
//
// Every function here exists because the JSFX (EEL2) source relies on an
// EEL2-specific operator/builtin whose C++ equivalent is easy to get subtly
// wrong - see references/loudness-meter/analisis-jsfx-loudness-meter.md
// Section 12 (errata) and architecture.md's "Algorithm Details" section.

#include <cmath>

namespace meterscope
{
    constexpr double kPi = 3.14159265358979323846;

    // EEL2's built-in log() is NATURAL log, not log10 - the JSFX has no
    // log10() builtin, so every dB conversion is written as the literal
    // expression `log(x)*10/log(10)` (power ratio -> dB) or
    // `log(x)*20/log(10)` (amplitude ratio -> dB). These two helpers
    // replicate that EXACT operation order (multiply-by-K, THEN divide by
    // log(10)) rather than an equivalent-looking `(log(x)/log(10)) * K` or
    // `std::log10(x) * K` - floating-point multiplication-before-division is
    // NOT guaranteed to produce a bit-identical result to division-before-
    // multiplication, even though both are mathematically equivalent to
    // log10(x)*K. "Exact numeric replication" (architecture.md's Priority #1)
    // means matching the JSFX's literal operation order, not just an
    // equivalent formula - do not "simplify" these.
    inline double eel2PowerRatioToDb(double x) noexcept
    {
        return std::log(x) * 10.0 / std::log(10.0);
    }

    inline double eel2AmplitudeRatioToDb(double x) noexcept
    {
        return std::log(x) * 20.0 / std::log(10.0);
    }

    // EEL2's `|0` operator truncates toward zero (NOT floor). For values
    // representable in `int` range, `static_cast<int>` already truncates
    // toward zero in C++, so this is a documented alias rather than a
    // different implementation - see architecture.md's "Algorithm Details"
    // note and errata #1 (trunc, not floor - the -70.1 LUFS absolute gate
    // edge case).
    inline int truncToInt(double x) noexcept
    {
        return static_cast<int>(x);
    }

    // Power-ratio-to-dB helper matching the JSFX's `-0.691 + log(sum)*10/log(10)`
    // idiom used for every LUFS value (Components 3/4/5), applied to a raw
    // sliding-window/held-max energy SUM (not a single sample). Returns the
    // JSFX's own -100.0 "not valid yet" sentinel when sum <= 0 - matches
    // Reset()'s own -100 initial values and the JSFX's `sum > 0 ? ... : -100`
    // gating pattern used identically for the current value and the held max.
    inline double lufsDbFromSumOrSentinel(double sum) noexcept
    {
        return sum > 0.0 ? (-0.691 + eel2PowerRatioToDb(sum)) : -100.0;
    }

    // Power-ratio-to-dB helper for RMS (no -0.691 K-weighting offset, unlike
    // LUFS) - matches the JSFX's `log(sum)*10/log(10)` for rms_m_db/rms_i_db.
    inline double rmsDbFromSumOrSentinel(double sum) noexcept
    {
        return sum > 0.0 ? eel2PowerRatioToDb(sum) : -100.0;
    }

    // Linear-amplitude-to-dB helper for Peak/True-Peak values, matching the
    // JSFX's own display floor (`format_db`: `v <= 10^(-72/20) ? "" : ...`).
    // architecture.md's SharedMeterSnapshot documents a -72.0 sentinel default
    // for every peak/TP field; this function is this port's interpretation of
    // "what raw dB value gets published when a channel hasn't peaked above
    // that floor yet" - architecture.md specifies the -72.0 sentinel's
    // *default* but does not spell out this exact linear->dB conversion point
    // for the raw (non-display) snapshot value. Documented as an
    // interpretation in the Phase 2.1 report.
    inline double linearToDbOrSentinel(double linear, double sentinel = -72.0) noexcept
    {
        const double floorLinear = std::pow(10.0, -72.0 / 20.0);
        return linear <= floorLinear ? sentinel : eel2AmplitudeRatioToDb(linear);
    }
}
