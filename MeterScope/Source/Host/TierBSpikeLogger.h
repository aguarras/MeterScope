// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Phase 2.2 Step 1 (Tier B spike) debug logging. Compiled entirely out unless
// the CMake option METERSCOPE_TIER_B_SPIKE_LOG is ON (default OFF) - see
// plugins/MeterScope/CMakeLists.txt and plan.md's Phase 2.2 "Step 1: Tier B
// spike (do first)" and its checks (a)-(d). This class exists purely to
// confirm host behaviour empirically in a real REAPER instance before the
// rest of Phase 2.2 (SharedMeterRegistry, projectConfigs, persistence, the
// Fallback tier) is built on top of it - it is not part of the shipped
// production feature set.
#if METERSCOPE_TIER_B_SPIKE_LOG

#include <juce_events/juce_events.h>
#include "ReaperHostBridge.h"

class MeterScopeAudioProcessor;

namespace meterscope
{
    /** Owned by MeterScopeAudioProcessor ("the processor owns a message-thread
        juce::Timer", per plan.md). Every ~2s, appends one line describing this
        instance's Tier B host-identity findings to a single log file shared by
        every MeterScope instance in the host process
        (%TEMP%\MeterScope_TierB_spike.log), guarded by a static
        juce::CriticalSection so concurrent instances in the same process
        never interleave a single line's write ("a static mutex is fine for
        this debug-only code", per plan.md).

        Never calls any REAPER API from the audio thread: isPlaying is read
        from an atomic bool the processor's own processBlock() maintains
        (MeterScopeAudioProcessor::getLastKnownIsPlayingForSpikeLog()), not
        from AudioPlayHead directly - AudioPlayHead::getPosition()'s own
        contract restricts it to the audio thread ("You can ONLY call this
        from your processBlock() method!", juce_AudioProcessor.h), and this
        Timer runs on the message thread.
    */
    class TierBSpikeLogger final : private juce::Timer
    {
    public:
        explicit TierBSpikeLogger (MeterScopeAudioProcessor& ownerProcessor);
        ~TierBSpikeLogger() override;

    private:
        void timerCallback() override;

        MeterScopeAudioProcessor& owner;
        juce::String instanceId;
    };
}

#endif // METERSCOPE_TIER_B_SPIKE_LOG
