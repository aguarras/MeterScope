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

#include "KWeighting.h"
#include "PeakMeter.h"
#include "BlockAccumulator.h"
#include "LoudnessHistogram.h"
#include "LraEngine.h"
#include "ResetController.h"
#include "SharedMeterSnapshot.h"

#include <array>
#include <cstdint>

#if METERSCOPE_VALIDATION_DUMP
 #include <memory>
 #include <fstream>
 #include <string>
 #include <iomanip>
#endif

// MeteringEngine: owns and orchestrates architecture.md Components 1-7 for a
// SINGLE instance (Phase 2.1 scope - no multi-instance registry, no REAPER
// host integration; that is Phase 2.2). This class is deliberately
// framework-free (no JUCE includes) so it can be constructed and driven in a
// standalone unit test; PluginProcessor.cpp is the only place that talks to
// juce::AudioPlayHead/APVTS and translates them into this class's plain
// BlockInput struct.
//
// Real-time safety: prepare()/reset() are the ONLY places that touch sizes -
// processBlock() never allocates, locks, or logs (juce::ScopedNoDenormals is
// applied by the caller, PluginProcessor::processBlock).

namespace meterscope
{
    class MeteringEngine
    {
    public:
        static constexpr int kMaxChannels = 2; // mono/stereo only, per Stage 1's bus layout

        // Called once from PluginProcessor::prepareToPlay(). Performs a full
        // Reset() (matches the JSFX's own unconditional `Reset();` at @init).
        void prepare(double sampleRate, int numChannels) noexcept;

        // Full Reset() - architecture.md Component 6: clears ALL Component
        // 1-5 state (filter memory, peak/TP registers + history, all ring
        // buffers and sliding sums, both histograms and their scalar
        // accumulators, all held maxima, LRA lo/hi/diff), and recomputes the
        // K-weighting and True-Peak sinc coefficients for the current sample
        // rate (matches jsfx's Reset() calling init_lufs_filters() and
        // sinc.sinc_init() every time, including on an in-place reset that
        // did not itself come from a sample-rate change).
        void reset() noexcept;

        // Message-thread API (forwarded from MeterScopeAudioProcessor::requestReset()).
        void requestReset() noexcept { resetController.requestReset(); }

        // Message-thread API: PARTIAL reset of ONE measurement's held max
        // (+ clip counters for the Peak register), consumed at the top of the
        // next processBlock together with the full-reset check. Touches
        // nothing else (no integrated values, histograms, windows, filter
        // state, current/decaying values, or other held maxima).
        // DELIBERATE DIFFERENCE FROM THE JSFX (whose any-click Reset() is full).
        void requestHeldMaxReset(HeldMaxTarget target) noexcept { resetController.requestHeldMaxReset(target); }

        struct BlockInput
        {
            int numChannels = 2;
            int numSamples = 0;
            const float* const* channelData = nullptr; // read-only, NEVER written (passthrough contract)
            int peakMode = 0;                            // raw AudioParameterChoice index (0..4)
            bool resetOnPlayEnabled = false;
            bool isPlayingOrRecording = false;
            bool hasPlayPosition = false;
            double playPositionSeconds = 0.0;
            double sampleRate = 44100.0;
        };

        // Runs Components 1-7 for one host buffer. Never reads or writes
        // `input.channelData` beyond bounds [0, numChannels) x [0, numSamples);
        // never allocates.
        void processBlock(const BlockInput& input) noexcept;

        const SharedMeterSnapshot& getSnapshot() const noexcept { return snapshot; }

        // Phase 2.2 Part B: lets PluginProcessor hand the registry a stable
        // pointer to THIS instance's own snapshot at registration time
        // (SharedMeterRegistry::Entry::snapshot references it rather than
        // owning a second copy - see architecture.md's Multi-Instance Shared
        // Store and this phase's task instructions: "make the Entry reference
        // or own it without changing the DSP math and without adding locks or
        // allocation to processBlock"). Purely additive - no DSP change.
        const SharedMeterSnapshot* getSnapshotPointer() const noexcept { return &snapshot; }

       #if METERSCOPE_VALIDATION_DUMP
        // Task 7 (validation hook): only compiled in when
        // METERSCOPE_VALIDATION_DUMP is ON. Writes from the audio thread are
        // explicitly accepted for this validation-only build.
        void openValidationDump(const std::string& csvPath);
        void closeValidationDump();
       #endif

    private:
        // Audio-thread only; applies consumed partial held-max bits.
        void applyHeldMaxReset(unsigned partialBits) noexcept;

        void onBlockBoundary(double rval, double lval, bool isPlayingOrRecording) noexcept;

        std::uint32_t resetCount = 0; // TEMP (Phase 2.1 validation) - see SharedMeterSnapshot::resetCount

        double sampleRateHz = 44100.0;
        int winLenSamples = 4410;
        int winPos = 0;
        std::uint64_t winCnt = 0;
        int activeNumChannels = 2;

        KWeightingCoefficients kwCoeffs;
        std::array<KWeightingFilter, kMaxChannels> kWeighting;
        std::array<PeakChannelState, kMaxChannels> peakState;
        std::array<double, kMaxChannels> chRmsSum {};
        std::array<double, kMaxChannels> chLufsSum {};

        BlockAccumulator blockAccumulator;
        LufsIEngine lufsIEngine;
        LraEngine lraEngine;
        ResetController resetController;

        double rmsMDb = -100.0;
        double rmsMDbMax = -100.0;

        double rmsISum = 0.0;
        std::uint64_t rmsISumCnt = 0;
        double rmsIDb = -100.0;

        double lufsMDb = -100.0;
        double lufsMSumMax = 0.0;

        double lufsSDb = -100.0;
        double lufsSSumMax = 0.0;

        SharedMeterSnapshot snapshot;

        void publishSnapshot() noexcept;

       #if METERSCOPE_VALIDATION_DUMP
        std::unique_ptr<std::ofstream> validationDumpStream;
        long long validationBlockIndex = 0;
       #endif
    };
}
