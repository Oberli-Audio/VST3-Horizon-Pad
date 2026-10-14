cmake_minimum_required(VERSION 3.22)

project(HorizonPad VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")

    # Universal binary: one .vst3 bundle that runs natively on both Apple
    # Silicon and Intel Macs (CMake/lipo does the fat-binary linking). Only
    # set a default if the caller hasn't already picked architectures
    # explicitly (e.g. a single-arch local dev build for faster iteration).
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
# JUCE
#
# Fetched with FetchContent so a fresh clone of this repository plus CMake is
# everything you need. To build against a JUCE checkout you already have on
# disk (much faster for repeat CI / local work), configure with:
#
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(HorizonPad
    COMPANY_NAME                "Quelle Music"
    BUNDLE_ID                   "com.quellemusic.horizonpad"
    PLUGIN_MANUFACTURER_CODE    Qumu
    PLUGIN_CODE                 Hzpd
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Horizon Pad"
    VST3_CATEGORIES             "Instrument" "Synth"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

juce_generate_juce_header(HorizonPad)

target_sources(HorizonPad PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp

    Source/dsp/LayerBase.cpp
    Source/dsp/WarmFoundationLayer.cpp
    Source/dsp/AnalogEnsembleLayer.cpp
    Source/dsp/AiryChoirLayer.cpp
    Source/dsp/MotionPadLayer.cpp
    Source/dsp/FxChain.cpp

    Source/presets/Presets.cpp
    Source/presets/UserPresetStore.cpp

    Source/gui/HorizonLookAndFeel.cpp
    Source/gui/HorizonScene.cpp
    Source/gui/AnimatedKnob.cpp
    Source/gui/TitleBanner.cpp
    Source/gui/PresetBar.cpp
    Source/gui/PadKnob.cpp
    Source/gui/MacrosPanel.cpp
    Source/gui/WheelSlider.cpp
    Source/gui/OutputMeter.cpp
    Source/gui/FooterBar.cpp)

target_include_directories(HorizonPad PRIVATE Source)

target_compile_definitions(HorizonPad PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(HorizonPad
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# HorizonPadSoundTool
#
# An offline render+analysis CLI for the sound-design iteration loop (see
# tools/sound_tool/Main.cpp and .claude/agents/sound-designer.md). It links
# directly against the "HorizonPad" target - the same static "SharedCode"
# library (its build output is literally named libHorizon Pad_SharedCode.a,
# via juce_add_plugin()'s internal _juce_set_output_name call) that the VST3
# and Standalone targets above link against - rather than recompiling any
# DSP or plugin-processor code. (juce_add_plugin(HorizonPad ...) makes
# "HorizonPad" itself the shared-code static library target; HorizonPad_VST3
# / HorizonPad_Standalone are the separate per-format targets that link
# against it - that's why target_sources/target_include_directories/
# target_link_libraries above are called directly on "HorizonPad".)
#
# HorizonPad (SharedCode) already contains a full compiled copy of every
# JUCE module it needs (juce_core, juce_dsp, juce_audio_processors, etc. -
# see its object list in the generated build files), so this target must
# NOT also link those juce:: module targets directly: that would recompile
# each module's implementation a second time into this target and produce
# duplicate-symbol linker errors. It only needs:
#   - the same include paths / preprocessor defines PluginProcessor.cpp was
#     compiled with (so the headers it includes resolve identically), and
#   - the same platform frameworks/system libraries the plugin formats above
#     link against (SharedCode's compiled JUCE modules call into these, but
#     a static library itself doesn't pull them in - the final linked
#     executable has to supply them).
# ---------------------------------------------------------------------------
# Shared setup for the tools that link the plugin's shared code (see above).
function(horizon_configure_tool tool_target)
    target_include_directories(${tool_target} PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/HorizonPad_artefacts/JuceLibraryCode"
        "${juce_SOURCE_DIR}/modules")

    target_compile_definitions(${tool_target} PRIVATE
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_MODULE_AVAILABLE_juce_audio_basics=1
        JUCE_MODULE_AVAILABLE_juce_audio_devices=1
        JUCE_MODULE_AVAILABLE_juce_audio_formats=1
        JUCE_MODULE_AVAILABLE_juce_audio_processors=1
        JUCE_MODULE_AVAILABLE_juce_audio_utils=1
        JUCE_MODULE_AVAILABLE_juce_core=1
        JUCE_MODULE_AVAILABLE_juce_data_structures=1
        JUCE_MODULE_AVAILABLE_juce_dsp=1
        JUCE_MODULE_AVAILABLE_juce_events=1
        JUCE_MODULE_AVAILABLE_juce_graphics=1
        JUCE_MODULE_AVAILABLE_juce_gui_basics=1
        JUCE_MODULE_AVAILABLE_juce_gui_extra=1
        # PluginProcessor.h's getName() override returns JucePlugin_Name inline;
        # a handful of other JucePlugin_* macros are referenced the same way
        # across Source/. These mirror the values juce_add_plugin() generates
        # for the real plugin targets above (PRODUCT_NAME / COMPANY_NAME /
        # PLUGIN_CODE / PLUGIN_MANUFACTURER_CODE / IS_SYNTH), so this tool's
        # translation units see identical values to PluginProcessor.cpp's.
        "JucePlugin_Name=\"Horizon Pad\""
        "JucePlugin_Desc=\"Horizon Pad\""
        "JucePlugin_Manufacturer=\"Quelle Music\""
        JucePlugin_ManufacturerCode=0x51756d75
        JucePlugin_PluginCode=0x487a7064
        JucePlugin_VersionCode=0x10000
        "JucePlugin_VersionString=\"1.0.0\""
        JucePlugin_Version=1.0.0
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=0
        JucePlugin_Build_VST3=1
        JucePlugin_Build_Standalone=1
        JucePlugin_Build_VST=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AUv3=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_LV2=0
        JucePlugin_Build_Unity=0
        JucePlugin_VSTUniqueID=JucePlugin_PluginCode
        JucePlugin_VSTCategory=kPlugCategSynth
        "JucePlugin_Vst3Category=\"Instrument|Synth\""
        "JucePlugin_AUMainType='aumu'"
        JucePlugin_AUSubType=JucePlugin_PluginCode
        JucePlugin_AUManufacturerCode=JucePlugin_ManufacturerCode
        JucePlugin_AUExportPrefix=Horizon_PadAU
        "JucePlugin_AUExportPrefixQuoted=\"Horizon_PadAU\""
        JucePlugin_CFBundleIdentifier=com.quellemusic.horizonpad
        JucePlugin_AAXIdentifier=com.quellemusic.horizonpad
        JucePlugin_AAXManufacturerCode=JucePlugin_ManufacturerCode
        JucePlugin_AAXProductId=JucePlugin_PluginCode
        JucePlugin_AAXCategory=2048
        JucePlugin_AAXDisableBypass=0
        JucePlugin_AAXDisableMultiMono=0
        JucePlugin_VSTNumMidiInputs=16
        JucePlugin_VSTNumMidiOutputs=16
        JucePlugin_Enable_ARA=0
        JucePlugin_ARAContentTypes=0
        JucePlugin_ARATransformationFlags=0
        "JucePlugin_ARAFactoryID=\"com.quellemusic.horizonpad.arafactory.1.0.0\""
        "JucePlugin_ARADocumentArchiveID=\"com.quellemusic.horizonpad.aradocumentarchive.1\""
        "JucePlugin_ARACompatibleArchiveIDs=\"\""
        "JucePlugin_ManufacturerWebsite=\"\""
        "JucePlugin_ManufacturerEmail=\"\"")

    target_compile_features(${tool_target} PRIVATE cxx_std_17)

    target_link_libraries(${tool_target} PRIVATE HorizonPad)

    # HorizonPad (SharedCode)'s own object compiles depend (order-only) on the
    # generated JuceHeader.h; make sure it exists before we compile Main.cpp too
    # (target_link_libraries alone only orders the *link* step, not our own
    # source compilation, against HorizonPad).
    add_dependencies(${tool_target} HorizonPad)


    if (APPLE)
        target_link_libraries(${tool_target} PRIVATE
            "-framework Cocoa"
            "-framework Foundation"
            "-framework IOKit"
            "-framework Security"
            "-framework QuartzCore"
            "-framework Accelerate"
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework CoreMIDI"
            "-framework CoreAudioKit"
            "-framework DiscRecording"
            "-weak_framework Metal"
            "-weak_framework MetalKit"
            "-framework WebKit")
    endif()
endfunction()

add_executable(HorizonPadSoundTool tools/sound_tool/Main.cpp)
horizon_configure_tool(HorizonPadSoundTool)

# Real-time safety (playbook rule 35): build the tool with RealtimeSanitizer so
# the test suite runs every processBlock as a real-time context. Needs an
# upstream LLVM 20+ toolchain, e.g.
#   cmake -B build-rtsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHORIZON_RTSAN=ON \
#         -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
#         -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
option(HORIZON_RTSAN "Build HorizonPadSoundTool with RealtimeSanitizer" OFF)
if (HORIZON_RTSAN)
    target_compile_options(HorizonPadSoundTool PRIVATE -fsanitize=realtime)
    target_link_options(HorizonPadSoundTool PRIVATE -fsanitize=realtime)
endif()

# HorizonPadGuiSnapshot (tools/gui_snapshot/Main.cpp): the editor rendered
# offscreen to PNG at any parameter state, with real audio behind its meters -
# the GUI's counterpart to the sound tool. Not built by default or in CI:
#   cmake --build build-release --target HorizonPadGuiSnapshot
add_executable(HorizonPadGuiSnapshot EXCLUDE_FROM_ALL tools/gui_snapshot/Main.cpp)
horizon_configure_tool(HorizonPadGuiSnapshot)

# ---------------------------------------------------------------------------
# Tests (G6)
#
# The suite drives the real plugin DSP through HorizonPadSoundTool rather than
# unit-testing classes in isolation: the things worth asserting here are
# end-to-end properties (latency, determinism from reset, behaviour across
# sample rates and block sizes, no denormal storm after silence), plus one
# regression test per bug that measurement has caught.
#
#   cmake --build build-release --target HorizonPadSoundTool
#   ctest --test-dir build-release --output-on-failure
#
# Needs python3 with numpy and scipy. If they are missing the tests are not
# registered, so a plain plugin build never fails for want of them.
# ---------------------------------------------------------------------------
enable_testing()

find_package(Python3 COMPONENTS Interpreter)

if (Python3_Interpreter_FOUND)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import numpy, scipy"
        RESULT_VARIABLE HORIZON_TEST_DEPS_MISSING
        OUTPUT_QUIET ERROR_QUIET)
else()
    set(HORIZON_TEST_DEPS_MISSING 1)
endif()

if (HORIZON_TEST_DEPS_MISSING)
    message(STATUS "HorizonPad: skipping test registration (needs python3 + numpy + scipy)")
else()
    set(HORIZON_TEST_NAMES
        latency_zero
        startup_program_matches_defaults
        root_bass_starts_open
        expanse_shimmer_recedes_at_top
        reset_determinism
        rate_block_matrix
        silence_after_note
        all_presets_safe
        shimmer_octave_present
        width_is_rate_invariant
        voice_steal_declick
        root_sub_present
        clearing_ensemble_present
        bloom_tremolo_survives_chords
        keyboard_level_span
        width_mono_safe_and_level_flat
        reverb_level_flat
        sustain_pedal_holds
        attack_release_span
        presets_have_worship_envelopes
        low_register_stays_musical
        bass_is_mono
        width_profile_staggered
        detune_spreads_every_stack)

    foreach(test_name IN LISTS HORIZON_TEST_NAMES)
        add_test(NAME ${test_name}
                 COMMAND "${Python3_EXECUTABLE}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/dsp_tests.py"
                         --tool "$<TARGET_FILE:HorizonPadSoundTool>"
                         ${test_name})
        # The matrix and preset sweeps render a lot of audio.
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 900)
    endforeach()
endif()

# Host-compatibility validation (playbook G6, D.3-D.5): the Steinberg validator and
# pluginval at strictness 10, run on the built VST3 by tools/validate/validate.py,
# which builds the validator from the commit pinned in tools/validate/versions.json.
# Labelled "validation" so the fast loop can leave them out:
#   ctest -LE validation          DSP tests only (~1 min)
#   ctest -L validation           validators only
#   ctest                         everything
# Exit code 77 (no bundle built, or pluginval not installed) is reported as skipped;
# `python3 tools/validate/validate.py --fetch-pluginval` downloads the pinned pluginval.
if (Python3_Interpreter_FOUND)
    foreach(step validator pluginval)
        add_test(NAME ${step}_vst3
                 COMMAND "${Python3_EXECUTABLE}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate/validate.py"
                         --only ${step} --no-build
                         --plugin "$<TARGET_FILE_DIR:HorizonPad_VST3>"
                         --out "${CMAKE_CURRENT_BINARY_DIR}/validation/${step}")
        set_tests_properties(${step}_vst3 PROPERTIES
                             LABELS validation
                             SKIP_RETURN_CODE 77
                             TIMEOUT 3600)
    endforeach()
endif()
