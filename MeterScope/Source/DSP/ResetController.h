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

#include <atomic>

// Component 6: Reset & Transport Controller (architecture.md Core Components
// #6). Ported from loudness_meter_cockos.jsfx's @block section (lines
// 312-325, `want_reset`/`Reset()`).
//
// Also owns the lock-free manual/remote reset request flag (architecture.md:
// "a single std::atomic<int> 'pending reset kind' per instance, written by
// the message thread (exchange), consumed by the audio thread (exchange) at
// the top of processBlock" - mirrors the JSFX's own `want_reset` flag).
// The pending flag is a bitmask: bit 0 = FULL reset (requestReset(); same
// semantics as the JSFX's Reset()), plus one bit per PARTIAL held-max reset
// target (requestHeldMaxReset()). Message thread ORs bits in (fetch_or,
// release); the audio thread consumes the whole mask with ONE exchange(0,
// acquire) per block, so a racing request is never lost.
//
// DELIBERATE DIFFERENCE FROM THE JSFX: the JSFX (line 873 `has_click ?
// Reset();`) runs the full Reset() on any meter click except Mono/"?". Here,
// clicking a held max clears only that measurement's held max (and, for the
// shared Peak/True-Peak register, its clip counters), both channels. Reset
// track / Reset all / all auto resets remain FULL.

namespace meterscope
{
    // Partial held-max reset targets (one bit each in the pending mask).
    enum class HeldMaxTarget : unsigned
    {
        PeakRegister = 1u << 1, // hipkval + clips, all channels (shared Peak/TruePeak register)
        RmsMMax      = 1u << 2,
        LufsMMax     = 1u << 3,
        LufsSMax     = 1u << 4
    };

    class ResetController
    {
    public:
        static constexpr unsigned kFullBit = 1u << 0;
        static constexpr unsigned kPartialMask = (unsigned) HeldMaxTarget::PeakRegister
                                               | (unsigned) HeldMaxTarget::RmsMMax
                                               | (unsigned) HeldMaxTarget::LufsMMax
                                               | (unsigned) HeldMaxTarget::LufsSMax;

        // Establishes the baseline (last-seen channel count / sample rate)
        // used by the auto-reset triggers below, so the very next
        // checkAndConsume() call does not spuriously report a change merely
        // because these were previously unset. Does NOT itself request a
        // reset - the caller (MeteringEngine::prepare()) is expected to call
        // reset() unconditionally once, matching the JSFX's own unconditional
        // `Reset();` call at @init.
        void prepare(int numChannels, double sampleRate) noexcept;

        // Checked once per processBlock, BEFORE any per-sample processing -
        // matches the JSFX's own @block ordering (want_reset check precedes
        // pk_decay/ch#.decay()). Returns true if a reset should occur this
        // block (any of: channel count changed, sample rate changed,
        // resetOnPlay-triggered position jump > 0.1s, or a pending manual/
        // remote reset request).
        //
        // `hasPlayPosition` must be false if the host provided no valid
        // transport time this block (e.g. no AudioPlayHead, or
        // getTimeInSeconds() returned nullopt) - in that case trigger #3 is
        // skipped for this block without disturbing the jump-detection
        // baseline, since there is nothing valid to compare against. This
        // guard is a documented interpretation: the JSFX always has a valid
        // REAPER transport position, so this case does not exist in the
        // JSFX's own hosting environment (see the Phase 2.1 report's
        // deviations list).
        bool checkAndConsume(int numChannels,
                              double sampleRate,
                              bool resetOnPlayEnabled,
                              bool isPlayingOrRecording,
                              bool hasPlayPosition,
                              double playPositionSeconds,
                              int numSamplesThisBlock,
                              unsigned* partialBitsOut = nullptr) noexcept;
        // Return value keeps full-reset semantics (auto trigger OR full bit).
        // `partialBitsOut` (optional) receives the partial held-max bits
        // consumed by the same single exchange (0 if none). If the return
        // value is true the partial bits are redundant (full reset clears them).

        // Message-thread API: requests an all-measurements reset, consumed
        // (exchange) at the top of this instance's own next processBlock.
        // Lock-free, real-time-safe to call concurrently with the audio
        // thread's checkAndConsume().
        void requestReset() noexcept
        {
            pendingReset.fetch_or(kFullBit, std::memory_order_release);
        }

        // Message-thread API: requests a PARTIAL reset of one measurement's
        // held max (see HeldMaxTarget). Same lock-free contract.
        void requestHeldMaxReset(HeldMaxTarget target) noexcept
        {
            pendingReset.fetch_or((unsigned) target, std::memory_order_release);
        }

    private:
        std::atomic<unsigned> pendingReset { 0 };

        int lastNumChannels = -1;
        double lastSampleRate = -1.0;
        double lastPlayPosition = 0.0;
    };
}
