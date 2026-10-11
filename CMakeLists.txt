# MeterScope - loudness, peak and RMS meter
# Copyright (C) 2026 aguarras (TES Lab)
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)
project(MeterScope VERSION 1.0.0 LANGUAGES C CXX)

# JUCE 9.0.2 with patches/juce applied. Either point JUCE_PATH at a patched local
# checkout, or leave it empty to download the tag and apply the patch here.
set(JUCE_PATH "" CACHE PATH "Path to a JUCE 9.0.2 checkout with patches/juce applied (empty = download)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    find_package(Git REQUIRED)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.2
        GIT_SHALLOW    TRUE
        PATCH_COMMAND  ${GIT_EXECUTABLE} apply --ignore-whitespace
                       ${CMAKE_CURRENT_SOURCE_DIR}/patches/juce/0001-webview2-smartscreen-and-dark-fallback.patch
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

add_subdirectory(MeterScope)
