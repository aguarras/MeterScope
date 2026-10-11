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

#include "ResetController.h"
#include <cmath>

namespace meterscope
{
    void ResetController::prepare(int numChannels, double sampleRate) noexcept
    {
        lastNumChannels = numChannels;
        lastSampleRate = sampleRate;
        lastPlayPosition = 0.0;
        pendingReset.store(0, std::memory_order_relaxed);
    }

    bool ResetController::checkAndConsume(int numChannels,
                                           double sampleRate,
                                           bool resetOnPlayEnabled,
                                           bool isPlayingOrRecording,
                                           bool hasPlayPosition,
                                           double playPositionSeconds,
                                           int numSamplesThisBlock,
                                           unsigned* partialBitsOut) noexcept
    {
        // jsfx: `want_reset = (num_ch>0 && num_ch!=last_nch) || srate!=last_sr;
        //        last_nch=num_ch; last_sr=srate;`
        bool wantReset = (numChannels > 0 && numChannels != lastNumChannels) || (sampleRate != lastSampleRate);
        lastNumChannels = numChannels;
        lastSampleRate = sampleRate;

        // jsfx: `cfg_reinit && (play_state&1) ? (
        //          abs(play_position-last_play_pos) > 0.1 ? want_reset=1;
        //          last_play_pos = play_position + samplesblock/srate; );`
        // Errata #4: this single condition covers playback start, cursor
        // jumps, AND every loop wrap - no special-casing needed.
        if (resetOnPlayEnabled && isPlayingOrRecording && hasPlayPosition)
        {
            if (std::abs(playPositionSeconds - lastPlayPosition) > 0.1)
                wantReset = true;

            lastPlayPosition = playPositionSeconds
                + (sampleRate > 0.0 ? (double) numSamplesThisBlock / sampleRate : 0.0);
        }

        // Manual/remote reset request (message thread -> audio thread).
        // ONE exchange consumes both the full bit and the partial bits.
        const unsigned pending = pendingReset.exchange(0, std::memory_order_acquire);
        if ((pending & kFullBit) != 0)
            wantReset = true;
        if (partialBitsOut != nullptr)
            *partialBitsOut = pending & kPartialMask;

        return wantReset;
    }
}
