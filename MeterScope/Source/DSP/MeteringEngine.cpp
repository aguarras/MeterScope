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

#include "MeteringEngine.h"
#include "JsfxMath.h"
#include <cmath>
#include <algorithm>

namespace meterscope
{
    void MeteringEngine::prepare(double sampleRate, int numChannels) noexcept
    {
        sampleRateHz = sampleRate;
        activeNumChannels = numChannels;
        resetController.prepare(numChannels, sampleRate);
        reset();
    }

    void MeteringEngine::reset() noexcept
    {
        // jsfx Reset(): init_lufs_filters() + sinc.sinc_init() run every time,
        // even on a reset NOT caused by a sample-rate change - see
        // architecture.md Component 1/2 and "Sample Rate Handling".
        kwCoeffs = computeKWeightingCoefficients(sampleRateHz);

        ++resetCount; // TEMP (Phase 2.1 validation)

        for (auto& f : kWeighting)
            f.reset();

        for (auto& p : peakState)
        {
            p.prepare(sampleRateHz);
            p.reset();
        }

        chRmsSum.fill(0.0);
        chLufsSum.fill(0.0);

        // jsfx: `win_len=(LOUD_METER_UPDATE*srate)|0;` LOUD_METER_UPDATE=0.1.
        winLenSamples = std::max(1, truncToInt(0.1 * sampleRateHz));
        winPos = 0;
        winCnt = 0;

        blockAccumulator.prepare(winLenSamples);
        blockAccumulator.reset();

        lufsIEngine.reset();
        lraEngine.reset();

        rmsMDb = -100.0;
        rmsMDbMax = -100.0;

        rmsISum = 0.0;
        rmsISumCnt = 0;
        rmsIDb = -100.0;

        lufsMDb = -100.0;
        lufsMSumMax = 0.0;

        lufsSDb = -100.0;
        lufsSSumMax = 0.0;
    }

    void MeteringEngine::applyHeldMaxReset(unsigned partialBits) noexcept
    {
        // Peak and True Peak share ONE register (errata #7): clear held max +
        // clips on every channel; leave pkval and the TP history alone.
        if (partialBits & (unsigned) HeldMaxTarget::PeakRegister)
        {
            for (auto& p : peakState)
            {
                p.hipkval = 0.0;
                p.clips = 0;
            }
        }
        if (partialBits & (unsigned) HeldMaxTarget::RmsMMax)
            rmsMDbMax = -100.0;
        if (partialBits & (unsigned) HeldMaxTarget::LufsMMax)
            lufsMSumMax = 0.0;
        if (partialBits & (unsigned) HeldMaxTarget::LufsSMax)
            lufsSSumMax = 0.0;
    }

    void MeteringEngine::onBlockBoundary(double rval, double lval, bool isPlayingOrRecording) noexcept
    {
        ++winCnt;

        blockAccumulator.pushBlock(rval, lval);

        const double rmsMSumRaw = blockAccumulator.rmsMSumRaw;
        const double lufsMSumRaw = blockAccumulator.lufsMSumRaw;
        const double lufsSSumRaw = blockAccumulator.lufsSSumRaw;

        // RMS-I (jsfx lines 379-387). Errata #2: accumulates the RAW sliding
        // sum from the very first 100ms block (including the first mWinCnt-1
        // blocks where the 400ms window still has zeros) - the win_cnt>=mWinCnt
        // condition only gates when rmsIDb is SHOWN, not when accumulation
        // starts. The whole block, including the `-100` branch, is nested
        // inside the isPlayingOrRecording gate, so rmsIDb FREEZES (is left
        // untouched) while not playing, rather than being reset to -100.
        if (isPlayingOrRecording)
        {
            rmsISum += rmsMSumRaw;
            rmsISumCnt += 1;

            if (rmsISum > 0.0 && rmsISumCnt >= (std::uint64_t) BlockAccumulator::mWinCnt)
                rmsIDb = eel2PowerRatioToDb(rmsISum / (double) rmsISumCnt);
            else
                rmsIDb = -100.0;
        }

        // RMS-M (jsfx lines 389-394).
        if (rmsMSumRaw > 0.0 && winCnt >= (std::uint64_t) BlockAccumulator::mWinCnt)
        {
            rmsMDb = eel2PowerRatioToDb(rmsMSumRaw);
            if (rmsMDb > rmsMDbMax)
                rmsMDbMax = rmsMDb;
        }
        else
        {
            rmsMDb = -100.0;
        }

        // LUFS-M + LUFS-I (jsfx lines 396-423).
        const bool lufsMValid = lufsMSumRaw > 0.0 && winCnt >= (std::uint64_t) BlockAccumulator::mWinCnt;
        if (lufsMValid)
        {
            if (lufsMSumRaw > lufsMSumMax)
                lufsMSumMax = lufsMSumRaw;

            lufsMDb = -0.691 + eel2PowerRatioToDb(lufsMSumRaw);

            lufsIEngine.pushMomentaryBlock(lufsMDb, lufsMSumRaw, isPlayingOrRecording);
        }
        else
        {
            lufsMDb = -100.0;
        }

        // LUFS-S + LRA (jsfx lines 425-468).
        const bool lufsSValid = lufsSSumRaw > 0.0 && winCnt >= (std::uint64_t) BlockAccumulator::sWinCnt;
        if (lufsSValid)
        {
            if (lufsSSumRaw > lufsSSumMax)
                lufsSSumMax = lufsSSumRaw;

            lufsSDb = -0.691 + eel2PowerRatioToDb(lufsSSumRaw);

            lraEngine.pushShortTermBlock(lufsSDb, lufsSSumRaw, isPlayingOrRecording);
        }
        else
        {
            lufsSDb = -100.0;
        }

       #if METERSCOPE_VALIDATION_DUMP
        if (validationDumpStream && validationDumpStream->is_open())
        {
            const double peakLDb = linearToDbOrSentinel(peakState[0].pkval);
            const double peakLHeldDb = linearToDbOrSentinel(peakState[0].hipkval);
            const double peakRDb = activeNumChannels > 1 ? linearToDbOrSentinel(peakState[1].pkval) : peakLDb;
            const double peakRHeldDb = activeNumChannels > 1 ? linearToDbOrSentinel(peakState[1].hipkval) : peakLHeldDb;

            *validationDumpStream
                << validationBlockIndex++ << ','
                << peakLDb << ',' << peakRDb << ',' << peakLHeldDb << ',' << peakRHeldDb << ','
                << rmsMDb << ',' << rmsIDb << ','
                << lufsMDb << ',' << lufsSDb << ',' << lufsIEngine.lufsIDb << ','
                << lraEngine.lraLoDb << ',' << lraEngine.lraHiDb << ',' << lraEngine.lraDiff << ','
                // Raw linear held peaks + clip counters + win_cnt, for the
                // bit-level comparison against the JSFX probe
                // (references/loudness-meter/loudness_meter_probe.jsfx).
                << peakState[0].hipkval << ','
                << (activeNumChannels > 1 ? peakState[1].hipkval : peakState[0].hipkval) << ','
                << peakState[0].clips << ','
                << (activeNumChannels > 1 ? peakState[1].clips : peakState[0].clips) << ','
                << winCnt
                << '\n';
            validationDumpStream->flush();
        }
       #endif
    }

    void MeteringEngine::processBlock(const BlockInput& input) noexcept
    {
        activeNumChannels = input.numChannels;
        sampleRateHz = input.sampleRate;

        unsigned partialBits = 0;
        const bool doReset = resetController.checkAndConsume(
            input.numChannels, input.sampleRate, input.resetOnPlayEnabled,
            input.isPlayingOrRecording, input.hasPlayPosition,
            input.playPositionSeconds, input.numSamples, &partialBits);

        if (doReset)
            reset(); // full reset subsumes any partial bits
        else if (partialBits != 0)
            applyHeldMaxReset(partialBits);

        const auto peakMode = static_cast<PeakMode>(input.peakMode);

        // Component 7: Display-Layer Peak Decay - once per block, BEFORE the
        // per-sample loop, using the ACTUAL host buffer size (jsfx line
        // 327-329, `pk_decay=pow(0.5, samplesblock/srate/PEAK_METER_DECAY);
        // ch#.decay();` - runs unconditionally for every channel regardless
        // of peakMode).
        const double pkDecay = input.sampleRate > 0.0
            ? std::pow(0.5, (double) input.numSamples / input.sampleRate / 0.150)
            : 1.0;
        for (auto& p : peakState)
            p.decay(pkDecay);

        for (int i = 0; i < input.numSamples; ++i)
        {
            // jsfx: `win_pos == 0 ? ch_rms_sum=ch_lufs_sum=0;` - checked
            // BEFORE this sample's per-channel accumulation, keyed on the
            // shared win_pos (not yet incremented for this sample).
            if (winPos == 0)
            {
                for (int ch = 0; ch < input.numChannels; ++ch)
                {
                    chRmsSum[(size_t) ch] = 0.0;
                    chLufsSum[(size_t) ch] = 0.0;
                }
            }

            double rval = 0.0;
            double lval = 0.0;

            for (int ch = 0; ch < input.numChannels; ++ch)
            {
                const double raw = (double) input.channelData[ch][i];

                // Step 1: Peak / True-Peak candidate from the RAW, unweighted
                // sample (before K-weighting) - Processing Order Requirements.
                processPeakSample(peakState[(size_t) ch], peakMode, raw);

                // Step 2: RMS accumulation from the RAW sample, in parallel
                // with step 1 (not sequentially dependent on it).
                chRmsSum[(size_t) ch] += raw * raw;
                rval += chRmsSum[(size_t) ch];

                // Step 3: K-weighting cascade (channel weight wt=1.0 always,
                // mono/stereo scope - see architecture.md Component 1).
                const double weighted = kWeighting[(size_t) ch].process(raw, kwCoeffs);

                // Step 4: LUFS accumulation from the Stage-2 output.
                chLufsSum[(size_t) ch] += weighted * weighted;
                lval += chLufsSum[(size_t) ch];
            }

            if (++winPos >= winLenSamples)
            {
                winPos = 0;
                onBlockBoundary(rval, lval, input.isPlayingOrRecording);
            }
        }

        publishSnapshot();
    }

    void MeteringEngine::publishSnapshot() noexcept
    {
        using std::memory_order_relaxed;

        const double peakDbL = linearToDbOrSentinel(peakState[0].pkval);
        const double peakHeldDbL = linearToDbOrSentinel(peakState[0].hipkval);
        const int clipsL = peakState[0].clips;

        // Mono-bus interpretation: when this instance is running on a mono
        // bus (1 physical channel), there is no channel R to publish - R
        // mirrors L. This is distinct from the `monoMode` PARAMETER (which
        // assumes a genuine stereo signal and is entirely a client-side
        // display concern per Component 8) - documented as an interpretation
        // in the Phase 2.1 report, since architecture.md's "both channels
        // unconditionally" instruction implicitly assumes a stereo bus.
        const double peakDbR = activeNumChannels > 1 ? linearToDbOrSentinel(peakState[1].pkval) : peakDbL;
        const double peakHeldDbR = activeNumChannels > 1 ? linearToDbOrSentinel(peakState[1].hipkval) : peakHeldDbL;
        const int clipsR = activeNumChannels > 1 ? peakState[1].clips : clipsL;

        snapshot.peakL.store(peakDbL, memory_order_relaxed);
        snapshot.peakR.store(peakDbR, memory_order_relaxed);
        snapshot.peakLHeld.store(peakHeldDbL, memory_order_relaxed);
        snapshot.peakRHeld.store(peakHeldDbR, memory_order_relaxed);
        snapshot.peakLClips.store(clipsL, memory_order_relaxed);
        snapshot.peakRClips.store(clipsR, memory_order_relaxed);

        // Peak/True-Peak Payload Correction (architecture.md): tp* mirrors
        // the SAME shared register as peak* - never a second, independently
        // tracked value.
        snapshot.tpL.store(peakDbL, memory_order_relaxed);
        snapshot.tpR.store(peakDbR, memory_order_relaxed);
        snapshot.tpLHeld.store(peakHeldDbL, memory_order_relaxed);
        snapshot.tpRHeld.store(peakHeldDbR, memory_order_relaxed);
        snapshot.tpLClips.store(clipsL, memory_order_relaxed);
        snapshot.tpRClips.store(clipsR, memory_order_relaxed);

        snapshot.rmsM.store(rmsMDb, memory_order_relaxed);
        snapshot.rmsMHeld.store(rmsMDbMax, memory_order_relaxed);
        snapshot.rmsI.store(rmsIDb, memory_order_relaxed);

        snapshot.lufsM.store(lufsMDb, memory_order_relaxed);
        snapshot.lufsMHeld.store(lufsDbFromSumOrSentinel(lufsMSumMax), memory_order_relaxed);
        snapshot.lufsS.store(lufsSDb, memory_order_relaxed);
        snapshot.lufsSHeld.store(lufsDbFromSumOrSentinel(lufsSSumMax), memory_order_relaxed);
        snapshot.lufsI.store(lufsIEngine.lufsIDb, memory_order_relaxed);

        snapshot.lraLo.store(lraEngine.lraLoDb, memory_order_relaxed);
        snapshot.lraHi.store(lraEngine.lraHiDb, memory_order_relaxed);
        snapshot.lraDiff.store(lraEngine.lraDiff, memory_order_relaxed);

        for (int i = 0; i < SharedMeterSnapshot::kLufsMHistBins; ++i)
            snapshot.lufsMHistBins[(size_t) i].store(lufsIEngine.dbHist[(size_t) i], memory_order_relaxed);

        snapshot.resetCount.store(resetCount, memory_order_relaxed); // TEMP (Phase 2.1 validation)

        snapshot.version.fetch_add(1, memory_order_relaxed);
    }

   #if METERSCOPE_VALIDATION_DUMP
    void MeteringEngine::openValidationDump(const std::string& csvPath)
    {
        validationDumpStream = std::make_unique<std::ofstream>(csvPath, std::ios::out | std::ios::trunc);
        if (validationDumpStream->is_open())
        {
            // Full double precision (17 significant digits - enough for exact
            // round-trip of any IEEE 754 double) so values can be compared
            // bit-for-bit against the JSFX reference.
            *validationDumpStream << std::setprecision(17);
            *validationDumpStream
                << "blockIndex,peakL,peakR,peakLHeld,peakRHeld,rmsM,rmsI,lufsM,lufsS,lufsI,lraLo,lraHi,lraDiff,peakLHeldLin,peakRHeldLin,clipsL,clipsR,winCnt\n";
        }
        validationBlockIndex = 0;
    }

    void MeteringEngine::closeValidationDump()
    {
        if (validationDumpStream)
        {
            validationDumpStream->close();
            validationDumpStream.reset();
        }
    }
   #endif
}
