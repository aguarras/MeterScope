// MeterScope - loudness, peak and RMS meter
// Copyright (C) 2026 aguarras (TES Lab)
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.

#pragma once

// Phase 2.2 Part B, task item 5: "Debug registry dump". Compiled entirely out
// unless the CMake option METERSCOPE_REGISTRY_DUMP_LOG is ON (default OFF) -
// replaces plan.md's original "DBG dump" idea, which is silent in Release
// builds; this writes a plain text file instead so it works in every build
// configuration.
//
// "Write it once per process, not once per instance" (this phase's task) is
// implemented via juce::SharedResourcePointer<RegistryDumpLogger>
// (verified: C:\JUCE\modules\juce_core\memory\juce_SharedResourcePointer.h) -
// every MeterScopeAudioProcessor instance holds one as a member (see
// PluginProcessor.h), but SharedResourcePointer guarantees exactly one
// RegistryDumpLogger (and therefore exactly one ~2s Timer) exists at a time
// across the whole process for as long as at least one instance is loaded,
// and it is destroyed automatically when the last one unloads - no manual
// reference counting needed.
#if METERSCOPE_REGISTRY_DUMP_LOG

#include <juce_events/juce_events.h>

namespace meterscope
{
    class RegistryDumpLogger final : private juce::Timer
    {
    public:
        RegistryDumpLogger();
        ~RegistryDumpLogger() override;

    private:
        void timerCallback() override;
    };
}

#endif // METERSCOPE_REGISTRY_DUMP_LOG
