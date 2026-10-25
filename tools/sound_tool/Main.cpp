/*
    HorizonPadSoundTool
    ====================

    A small, deterministic-as-possible offline render + analysis CLI for the
    Horizon Pad synth's DSP. It links directly against the same
    HorizonPad_SharedCode static library the real VST3/Standalone plugin
    targets use (see the root CMakeLists.txt), instantiates the real
    HorizonPadAudioProcessor, feeds it synthetic MIDI, renders audio by
    calling processBlock() repeatedly (no audio device, no host, no editor),
    and prints a single JSON object of objective metrics to stdout.

    This exists so a sound-design iteration loop (change DSP source -> build
    -> render -> analyze -> compare -> repeat) can run unattended, without a
    human listening to every candidate render.

    Usage examples
    --------------
      # Solo the Root (warmFoundation) layer on a single note, default timing.
      HorizonPadSoundTool --solo=root --notes=60

      # Solo Expanse (airy choir) on a chord, write a WAV for manual listening.
      HorizonPadSoundTool --solo=expanse --notes=48,52,55 --out=/tmp/expanse.wav

      # All four layers at their default blend (no solo), with a custom macro.
      HorizonPadSoundTool --notes=60 --param=filterMacro=0.8

      # Discover the factory presets without rendering anything.
      HorizonPadSoundTool --list-presets

    Run with --help for the full flag list.

    A note on determinism: each DSP layer seeds a juce::Random per instance
    from system entropy (LayerBase::rng), used only to pick each voice's
    starting oscillator/LFO phase (an intentional anti-"digital-static"
    design choice - see WarmFoundationLayer.h etc.) so runs are NOT
    bit-exact identical. Aggregate metrics (peak, RMS, envelope shape,
    spectral energy ratios) are stable run-to-run to a small fraction of a
    percent; that stability, not byte-identical audio, is what before/after
    comparisons should rely on.
*/

#include <JuceHeader.h>
#include "BuildInfo.h"

#include "PluginProcessor.h"
#include "Diagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <new>
#include <atomic>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <vector>

// Real-time safety check (playbook rule 35, D.1.6). Configured with
// -DHORIZON_RTSAN=ON and an upstream LLVM 20+ clang (Apple clang has no
// RealtimeSanitizer), every processBlock call below runs inside a
// [[clang::nonblocking]] function, so RTSan aborts with a stack trace on any
// allocation, lock or system call the audio path reaches. In a normal build
// the attribute is absent and this is a plain call.
//
// HORIZON_RTSAN_SELFTEST=1 in the environment proves the check is live
// (rule 28): one heap allocation inside the real-time context, which must
// abort with "unsafe-library-call ... malloc" on the first block.
#if defined(__has_feature)
 #if __has_feature(realtime_sanitizer)
  #define HORIZON_NONBLOCKING [[clang::nonblocking]]
  #define HORIZON_HAS_RTSAN 1
 #endif
#endif
#ifndef HORIZON_NONBLOCKING
 #define HORIZON_NONBLOCKING
 #define HORIZON_HAS_RTSAN 0
#endif

#if HORIZON_HAS_RTSAN
static const bool rtsanSelfTest = std::getenv ("HORIZON_RTSAN_SELFTEST") != nullptr; // read before any block
static void* volatile rtsanSelfTestSink = nullptr;  // volatile: an unused new/delete pair may be elided

static void rtsanSelfTestAllocation()
{
    rtsanSelfTestSink = std::malloc (64);
    std::free (rtsanSelfTestSink);
}
#endif

// The malloc trap (playbook D.1.6, the Tier P check; RTSan above is Tier F):
// in every other macOS build this executable defines malloc, calloc, realloc,
// posix_memalign and operator new/delete itself, forwarding to the default
// malloc zone, and an allocation made by the thread inside processBlock aborts
// the run with a message - so every test that renders through this tool is
// also an allocation test. The plugin and JUCE are linked in statically, so
// their calls bind to these definitions. (A hook on malloc_default_zone()'s
// function pointers, the playbook's suggestion, saw none of them on macOS 26:
// the allocator's fast path bypasses the zone table - measured 2026-10-22.)
// Armed only around processBlock; HORIZON_MALLOC_TRAP=0 switches it off,
// HORIZON_MALLOC_TRAP_SELFTEST=1 allocates inside the armed region once and
// must abort (rule 28: the check is live). Misses what the playbook says it
// misses: locks, system calls, page faults, unbounded loops.
#if JUCE_MAC && ! HORIZON_HAS_RTSAN
 #include <malloc/malloc.h>
 #include <pthread.h>
 #include <unistd.h>

namespace mallocTrap
{
    static std::atomic<bool> armed { false };   // constant-initialised: safe before main()
    static pthread_t owner;

    [[noreturn]] static void trip (const char* what) noexcept
    {
        armed.store (false);
        static const char prefix[] = "malloc trap: ";
        static const char suffix[] = " inside processBlock (rule 1: no allocation on the audio thread)\n";
        ::write (2, prefix, sizeof (prefix) - 1);
        ::write (2, what, std::strlen (what));
        ::write (2, suffix, sizeof (suffix) - 1);
        std::abort();
    }

    static inline void check (const char* what) noexcept
    {
        if (armed.load (std::memory_order_relaxed) && pthread_equal (pthread_self(), owner))
            trip (what);
    }

    static bool enabled()
    {
        const char* v = std::getenv ("HORIZON_MALLOC_TRAP");
        return v == nullptr || std::strcmp (v, "0") != 0;
    }

    static const bool selfTest = std::getenv ("HORIZON_MALLOC_TRAP_SELFTEST") != nullptr;
    static void* volatile selfTestSink = nullptr;
}

extern "C"
{
    void* malloc (size_t n)            { mallocTrap::check ("malloc");  return malloc_zone_malloc (malloc_default_zone(), n); }
    void* calloc (size_t c, size_t n)  { mallocTrap::check ("calloc");  return malloc_zone_calloc (malloc_default_zone(), c, n); }
    void* realloc (void* p, size_t n)  { mallocTrap::check ("realloc"); return malloc_zone_realloc (malloc_default_zone(), p, n); }

    int posix_memalign (void** out, size_t alignment, size_t n)
    {
        mallocTrap::check ("posix_memalign");
        *out = malloc_zone_memalign (malloc_default_zone(), alignment, n);
        return *out != nullptr ? 0 : ENOMEM;
    }
}

void* operator new (size_t n)                  { mallocTrap::check ("operator new");   if (auto* p = malloc_zone_malloc (malloc_default_zone(), n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (size_t n)                { mallocTrap::check ("operator new[]"); if (auto* p = malloc_zone_malloc (malloc_default_zone(), n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new (size_t n, const std::nothrow_t&) noexcept   { mallocTrap::check ("operator new");   return malloc_zone_malloc (malloc_default_zone(), n ? n : 1); }
void* operator new[] (size_t n, const std::nothrow_t&) noexcept { mallocTrap::check ("operator new[]"); return malloc_zone_malloc (malloc_default_zone(), n ? n : 1); }
void operator delete (void* p) noexcept                { std::free (p); }
void operator delete[] (void* p) noexcept              { std::free (p); }
void operator delete (void* p, size_t) noexcept        { std::free (p); }
void operator delete[] (void* p, size_t) noexcept      { std::free (p); }
#endif

static void processBlockRealtime (juce::AudioProcessor& processor,
                                  juce::AudioBuffer<float>& block,
                                  juce::MidiBuffer& midi) HORIZON_NONBLOCKING
{
   #if HORIZON_HAS_RTSAN
    if (rtsanSelfTest)
        rtsanSelfTestAllocation();
   #endif

   #if JUCE_MAC && ! HORIZON_HAS_RTSAN
    static const bool trapOn = mallocTrap::enabled();

    if (trapOn)
    {
        mallocTrap::owner = pthread_self();
        mallocTrap::armed.store (true, std::memory_order_relaxed);

        if (mallocTrap::selfTest)
            mallocTrap::selfTestSink = std::malloc (64);

        processor.processBlock (block, midi);
        mallocTrap::armed.store (false, std::memory_order_relaxed);
        return;
    }
   #endif

    processor.processBlock (block, midi);
}

using namespace horizon;

//==============================================================================
namespace
{
    //--------------------------------------------------------------------
    // CLI argument parsing: a flat --key=value (or bare --flag) map.
    //--------------------------------------------------------------------
    struct Args
    {
        std::map<juce::String, juce::String> values;

        bool has (const juce::String& key) const { return values.find (key) != values.end(); }

        juce::String getString (const juce::String& key, const juce::String& fallback) const
        {
            auto it = values.find (key);
            return it == values.end() ? fallback : it->second;
        }

        float getFloat (const juce::String& key, float fallback) const
        {
            auto it = values.find (key);
            return it == values.end() || it->second.isEmpty() ? fallback : it->second.getFloatValue();
        }

        int getInt (const juce::String& key, int fallback) const
        {
            auto it = values.find (key);
            return it == values.end() || it->second.isEmpty() ? fallback : it->second.getIntValue();
        }

        std::vector<juce::String> getList (const juce::String& key, const juce::String& fallback) const
        {
            auto raw = getString (key, fallback);
            std::vector<juce::String> out;

            for (auto token : juce::StringArray::fromTokens (raw, ",", ""))
            {
                token = token.trim();
                if (token.isNotEmpty())
                    out.push_back (token);
            }

            return out;
        }
    };

    Args parseArgs (int argc, char* argv[])
    {
        Args args;

        for (int i = 1; i < argc; ++i)
        {
            // fromUTF8, not juce::String's const char* constructor: that
            // constructor asserts its input is plain ASCII, so a UTF-8 argument
            // (--preset=Alpengluehen with a real umlaut, or an --out path with
            // one) was silently mangled into a string that matched no preset.
            auto arg = juce::String::fromUTF8 (argv[i]);

            if (! arg.startsWith ("--"))
                continue;

            arg = arg.substring (2);
            const auto eq = arg.indexOfChar ('=');

            if (eq >= 0)
            {
                const auto key = arg.substring (0, eq);
                const auto value = arg.substring (eq + 1);

                // --param is repeatable. It used to be stored like every other
                // flag, so a repeated --param silently kept only the LAST one:
                // `--param=reverb=0 --param=filter=0.5` rendered with the
                // reverb on. Every script that passed several (the G5 null
                // test, the alias and EDR measurements, two regression tests)
                // measured a different patch than it asked for. Accumulate
                // instead, in the same ';'-separated form the batch syntax uses.
                if ((key == "param" || key == "automate" || key == "preset-at" || key == "reprepare-at") && args.has (key))
                    args.values[key] = args.values[key] + ";" + value;
                else
                    args.values[key] = value;
            }
            else
                args.values[arg] = "1"; // bare flag
        }

        return args;
    }

    void printUsage()
    {
        std::cout <<
            "HorizonPadSoundTool - render + analyze Horizon Pad's DSP offline\n\n"
            "Usage: HorizonPadSoundTool [options]\n\n"
            "Rendering:\n"
            "  --notes=60[,64,67]     MIDI note numbers to play as a chord (default 60)\n"
            "  --velocity=0.85        Note-on velocity, 0..1 (default 0.85)\n"
            "  --hold=4.0             Seconds from note-on to note-off (default 4.0)\n"
            "  --tail=6.0             Seconds to keep rendering after note-off,\n"
            "                         to capture release + reverb tail (default 6.0)\n"
            "  --sample-rate=48000    Render sample rate in Hz (default 48000)\n"
            "  --block=512            Block size passed to processBlock (default 512)\n"
            "  --seed=1               Pin every layer's oscillator start-phase RNG so\n"
            "                         the render is reproducible. Off by default: the\n"
            "                         plugin randomises phases per voice deliberately.\n"
            "                         Required for null tests and regression baselines.\n"
            "  --note-onsets=0,0,3    Per-note onset times in seconds, matched positionally\n"
            "                         to --notes. Missing entries default to 0.\n"
            "  --note-holds=6,6,2     Per-note hold durations in seconds, matched positionally\n"
            "                         to --notes. Missing entries default to --hold.\n"
            "                         Together these make voice stealing testable, e.g.\n"
            "                         8 notes at 0 plus a 9th at 3s on an 8-slot engine.\n"
            "  --pedal=2,5[,6,8]      Sustain pedal (CC64) down/up times in seconds, in\n"
            "                         pairs. The render extends past the last pedal-up.\n"
            "  --bend=t:v[,t:v]       Pitch-wheel messages: at t seconds, wheel position v\n"
            "                         (-1..+1, 0 = centre), sent as real 14-bit MIDI.\n"
            "  --mod=t:v[,t:v]        Mod-wheel (CC1) messages: at t seconds, amount v (0..1).\n\n"
            "Patch selection (applied in this order - each stage can override the last):\n"
            "  --preset=<name|index>  Apply a factory preset first (see --list-presets)\n"
            "  --solo=root,clearing   Zero every OTHER layer's volume, set these to 1.0\n"
            "                         (aliases: warmFoundation/analogEnsemble/airyChoir/\n"
            "                         motionPad, or root/clearing/expanse/bloom)\n"
            "  --param=id=value       Raw APVTS override, repeatable, e.g.\n"
            "                         --param=filterMacro=0.8 --param=reverbMacro=0\n"
            "                         Accepts either the raw ParamID or a short alias:\n"
            "                         root/clearing/expanse/bloom (volume),\n"
            "                         attack/release/filter/reverb/width/detune (macros)\n"
            "  --automate=id:t0=v0,t1=v1[,...]\n"
            "                         Host-style automation (harness contract): a linear\n"
            "                         ramp through the points (seconds = value 0..1),\n"
            "                         applied once per block the way JUCE hosts deliver\n"
            "                         it - the worst case for zipper noise. Repeatable;\n"
            "                         same id aliases as --param.\n\n"
            "Analysis:\n"
            "  --window-ms=50         Envelope RMS window size in ms (default 50)\n"
            "  --fft-size=8192        Spectral analysis window length, rounded to the\n"
            "                         nearest power of two (default 8192)\n"
            "  --click-threshold=0.2  Per-sample delta above which a discontinuity is\n"
            "                         flagged as a possible click (default 0.2)\n"
            "  --probe-at=3.0[,5.5]   Extra times in seconds at which to measure a step\n"
            "                         discontinuity, reported under transients.probes.\n"
            "                         Use it for events that are not MIDI events -\n"
            "                         above all a voice steal.\n"
            "  --transient-window=100 Samples examined after the first note-on and after\n"
            "                         the last note-off for a step discontinuity, reported\n"
            "                         under \"transients\" both absolutely and relative to\n"
            "                         the signal level just before (default 100)\n"
            "  --true-peak            Also measure 4x-oversampled true peak. Off by\n"
            "                         default: it is the most expensive metric here and\n"
            "                         the iteration loop does not need it.\n"
            "  --partial-ratios=0.5,1,2\n"
            "                         The partial model the alias floor is scored against:\n"
            "                         harmonic series of each ratio x f0. Default covers\n"
            "                         Root's sub-octave (0.5), the fundamental (1) and\n"
            "                         Expanse's shimmer octave (2). Narrow it when soloing\n"
            "                         a layer that has no sub or no shimmer, or the alias\n"
            "                         floor is scored against partials that cannot exist.\n"
            "  --profile              Time every processBlock call (harness contract) and\n"
            "                         report p50, p99, p99.9 and max as % of the block's\n"
            "                         deadline under \"profile\" (rule 37). Use a Release\n"
            "                         build, a long --hold and several runs: a maximum\n"
            "                         that moves between runs is the OS, not the DSP.\n\n"
            "Output:\n"
            "  --out=<path.wav>       Also write the render to a 32-bit float WAV file\n"
            "  --list-presets         Print the factory preset table as JSON and exit\n"
            "  --list-params          Print the parameter layout (host order) as JSON and exit\n"
            "  --state-in=<file>      Load a session state first (any bytes; refused -> exit 2)\n"
            "  --state-out=<file>     Save the session state after the render\n"
            "  --diagnostics          Print the About box's diagnostics block and exit\n"
            "  --import-preset=<file> Import a .bobpreset first (as the preset bar does)\n"
            "  --export-preset=<file> Write the current sound as a .bobpreset after the render\n"
            "  --help                 Print this message and exit\n";
    }

    //--------------------------------------------------------------------
    // Parameter ID resolution (raw ParamID string or short alias).
    //--------------------------------------------------------------------
    juce::String resolveParamId (const juce::String& nameIn)
    {
        auto name = nameIn.trim();

        static const std::map<juce::String, juce::String> aliases {
            { "root",     ParamID::rootVolume },
            { "clearing", ParamID::clearingVolume },
            { "expanse",  ParamID::expanseVolume },
            { "bloom",    ParamID::bloomVolume },
            { "attack",   ParamID::attackMacro },
            { "release",  ParamID::releaseMacro },
            { "filter",   ParamID::filterMacro },
            { "reverb",   ParamID::reverbMacro },
            { "width",    ParamID::widthMacro },
            { "detune",   ParamID::detuneMacro },

            // LayerIndex-style names, also accepted for --solo.
            { "warmfoundation", ParamID::rootVolume },
            { "analogensemble", ParamID::clearingVolume },
            { "airychoir",      ParamID::expanseVolume },
            { "motionpad",      ParamID::bloomVolume },
        };

        auto it = aliases.find (name.toLowerCase());
        if (it != aliases.end())
            return it->second;

        return name; // assume it's already a raw ParamID
    }

    const std::array<const char*, (size_t) kNumLayers>& allVolumeIds()
    {
        static const std::array<const char*, (size_t) kNumLayers> ids {
            ParamID::rootVolume, ParamID::clearingVolume, ParamID::expanseVolume, ParamID::bloomVolume
        };
        return ids;
    }

    //--------------------------------------------------------------------
    // Preset lookup by bank index, slug (presets.csv), exact name, or a
    // case-insensitive substring of the name - in that order.
    //--------------------------------------------------------------------
    const Preset* findPreset (const juce::String& key)
    {
        const auto& presets = getFactoryPresets();

        if (key.containsOnly ("0123456789"))
        {
            const auto idx = key.getIntValue();
            if (juce::isPositiveAndBelow (idx, (int) presets.size()))
                return &presets[(size_t) idx];
            return nullptr;
        }

        for (auto& p : presets)
            if (p.slug == key || p.name.equalsIgnoreCase (key))
                return &p;

        for (auto& p : presets)
            if (p.name.containsIgnoreCase (key))
                return &p;

        return nullptr;
    }

    juce::var presetToVar (const Preset& p, int index)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("index", index);
        obj->setProperty ("slug", p.slug);
        obj->setProperty ("name", p.name);
        obj->setProperty ("category", p.category);
        obj->setProperty ("author", p.author);
        obj->setProperty ("description", p.description);

        auto* vols = new juce::DynamicObject();
        vols->setProperty ("root", toUnit (p.volumes[(size_t) LayerIndex::warmFoundation]));
        vols->setProperty ("clearing", toUnit (p.volumes[(size_t) LayerIndex::analogEnsemble]));
        vols->setProperty ("expanse", toUnit (p.volumes[(size_t) LayerIndex::airyChoir]));
        vols->setProperty ("bloom", toUnit (p.volumes[(size_t) LayerIndex::motionPad]));
        obj->setProperty ("volumes", juce::var (vols));

        auto* macros = new juce::DynamicObject();
        macros->setProperty ("attack", toUnit (p.macros[attackMacroIndex]));
        macros->setProperty ("release", toUnit (p.macros[releaseMacroIndex]));
        macros->setProperty ("filter", toUnit (p.macros[filterMacroIndex]));
        macros->setProperty ("reverb", toUnit (p.macros[reverbMacroIndex]));
        macros->setProperty ("width", toUnit (p.macros[widthMacroIndex]));
        macros->setProperty ("detune", toUnit (p.macros[detuneMacroIndex]));
        obj->setProperty ("macros", juce::var (macros));

        return juce::var (obj);
    }

    //--------------------------------------------------------------------
    // Metrics
    //--------------------------------------------------------------------
    struct SpectralResult
    {
        bool valid = false;
        double fundamentalHz = 0.0;
        double nonHarmonicRatio = 0.0;   // 0 = perfectly harmonic, 1 = all energy non-harmonic
        double highFreqEnergyRatio = 0.0; // energy above 12kHz / total energy
        double spectralCentroidHz = 0.0;
        int windowsAveraged = 0;
        int fftSizeUsed = 0;

        struct Peak { double freqHz; double magnitude; };
        std::vector<Peak> topNonHarmonicPeaks;
        std::vector<Peak> topResidualPeaks; // strongest bins the partial model does not explain

        // Alias floor, measured against the partial model (see analyzeSpectrum).
        double residualEnergyRatio = 0.0; // energy not explained by the model / total
        double asrDb = -300.0;            // 10*log10(residual / modelled partials)
        double nmrMaxDb = -300.0;         // worst critical band, < 0 dB = masked
        double nmrWorstBandHz = 0.0;
    };

    /** Traunmueller/Zwicker Bark scale - the critical-band axis NMR is computed on. */
    double hzToBark (double f) noexcept
    {
        return 13.0 * std::atan (0.00076 * f) + 3.5 * std::atan (std::pow (f / 7500.0, 2.0));
    }

    int nextPowerOfTwo (int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return juce::jmax (256, p);
    }

    /*  Alias floor for THIS instrument, and why it needs a partial model.

        Every Horizon Pad layer is a detuned stack, Root adds a sub-oscillator an
        octave down, and Expanse adds a +1-octave shimmer. So a large fraction of
        the output is inharmonic *by design*, and the plain non-harmonic energy
        ratio above reads 0.3-0.6 on a perfectly clean render - it cannot tell
        "intentionally detuned partial" from "alias".

        The alias metrics below therefore score against a declared partial model:
        every harmonic of (ratio * f0) for each ratio in partialRatios - by
        default 0.5 (sub), 1.0 (fundamental) and 2.0 (shimmer octave) - each with
        the same +/-2.5% (~43 cent) tolerance the detune/chorus spread needs.
        Energy outside all of that is residual: alias, plus noise, plus any
        partial the model does not know about. Solo a layer with no shimmer and
        pass --partial-ratios=0.5,1 to tighten the model to what it actually has.

        ASR is the summary number; NMR is the decisive one (playbook 4.4). NMR
        here is a documented simplification, not the full VA-literature model:
        1-Bark bands, masker energy = modelled partials in that band, spread to
        neighbours at -12 dB/Bark upward and -27 dB/Bark downward, a flat 15 dB
        masking depth (the conservative end of the 15-30 dB range), and an
        absolute floor of -100 dB relative to total energy so that empty bands
        cannot report an infinite ratio.
    */
    SpectralResult analyzeSpectrum (const juce::AudioBuffer<float>& buffer,
                                     double sampleRate,
                                     int regionStartSample,
                                     int regionEndSample,
                                     double fundamentalHz,
                                     int requestedFftSize,
                                     const std::vector<double>& partialRatios)
    {
        SpectralResult result;

        const auto fftSize = nextPowerOfTwo (requestedFftSize);
        const auto order = (int) std::round (std::log2 ((double) fftSize));

        const auto regionLen = regionEndSample - regionStartSample;
        if (regionLen < fftSize || fundamentalHz <= 0.0)
            return result; // not enough material - leave invalid

        // Up to 4 overlapping windows spread across the region, averaged.
        const int maxWindows = 4;
        const int numWindows = juce::jmin (maxWindows, juce::jmax (1, regionLen / (fftSize / 2)));

        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> accum ((size_t) fftSize, 0.0f);
        std::vector<float> scratch ((size_t) (fftSize * 2), 0.0f);

        const auto* readL = buffer.getReadPointer (0);
        const auto* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : readL;

        int windowsDone = 0;

        for (int w = 0; w < numWindows; ++w)
        {
            const auto step = numWindows > 1 ? (regionLen - fftSize) / (numWindows - 1) : 0;
            const auto start = regionStartSample + w * step;

            if (start < 0 || start + fftSize > buffer.getNumSamples())
                continue;

            std::fill (scratch.begin(), scratch.end(), 0.0f);

            for (int n = 0; n < fftSize; ++n)
                scratch[(size_t) n] = 0.5f * (readL[start + n] + readR[start + n]);

            window.multiplyWithWindowingTable (scratch.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (scratch.data());

            for (int n = 0; n < fftSize; ++n)
                accum[(size_t) n] += scratch[(size_t) n];

            ++windowsDone;
        }

        if (windowsDone == 0)
            return result;

        for (auto& v : accum)
            v /= (float) windowsDone;

        const int numBins = fftSize / 2;
        const double binHz = sampleRate / (double) fftSize;

        // Mark every bin that falls within tolerance of a harmonic of the
        // fundamental (accounting for the layers' own detune/drift, a few
        // cents to a few Hz, and chorus/shimmer spread on top of that).
        std::vector<bool> isHarmonic ((size_t) numBins, false);
        std::vector<bool> isPartial ((size_t) numBins, false);
        const auto nyquist = sampleRate * 0.5;

        const auto markSeries = [&] (double seriesF0, std::vector<bool>& mask)
        {
            if (seriesF0 <= 0.0)
                return;

            for (int k = 1; (double) k * seriesF0 < nyquist; ++k)
            {
                const auto partialHz = (double) k * seriesF0;
                const auto toleranceHz = juce::jmax (binHz * 2.0, partialHz * 0.025);
                const int loBin = juce::jmax (1, (int) std::floor ((partialHz - toleranceHz) / binHz));
                const int hiBin = juce::jmin (numBins - 1, (int) std::ceil ((partialHz + toleranceHz) / binHz));

                for (int b = loBin; b <= hiBin; ++b)
                    mask[(size_t) b] = true;
            }
        };

        // Legacy field: harmonics of f0 only.
        markSeries (fundamentalHz, isHarmonic);

        // Alias model: every declared ratio's own harmonic series.
        for (auto ratio : partialRatios)
            markSeries (fundamentalHz * ratio, isPartial);

        double totalEnergy = 0.0, harmonicEnergy = 0.0, highFreqEnergy = 0.0;
        double partialEnergy = 0.0, residualEnergy = 0.0;
        double centroidNum = 0.0, centroidDen = 0.0;

        // 1-Bark bands, 0..24 Bark covers 20 Hz - 20 kHz.
        constexpr int numBands = 25;
        std::array<double, numBands> bandPartial {};
        std::array<double, numBands> bandResidual {};
        bandPartial.fill (0.0);
        bandResidual.fill (0.0);

        struct BinPeak { double freqHz; double mag; };
        std::vector<BinPeak> nonHarmonicPeaks, residualPeaks;

        for (int b = 1; b < numBins; ++b)
        {
            const double mag = accum[(size_t) b];
            const double energy = mag * mag;
            const double freqHz = b * binHz;

            totalEnergy += energy;
            centroidNum += freqHz * mag;
            centroidDen += mag;

            if (isHarmonic[(size_t) b])
                harmonicEnergy += energy;
            else
                nonHarmonicPeaks.push_back ({ freqHz, mag });

            const int band = juce::jlimit (0, numBands - 1, (int) std::floor (hzToBark (freqHz)));

            if (isPartial[(size_t) b])
            {
                partialEnergy += energy;
                bandPartial[(size_t) band] += energy;
            }
            else
            {
                residualEnergy += energy;
                bandResidual[(size_t) band] += energy;
                residualPeaks.push_back ({ freqHz, mag });
            }

            if (freqHz > 12000.0)
                highFreqEnergy += energy;
        }

        std::sort (nonHarmonicPeaks.begin(), nonHarmonicPeaks.end(),
                   [] (const BinPeak& a, const BinPeak& b) { return a.mag > b.mag; });

        result.valid = true;
        result.fundamentalHz = fundamentalHz;
        result.nonHarmonicRatio = totalEnergy > 1.0e-12 ? juce::jlimit (0.0, 1.0, (totalEnergy - harmonicEnergy) / totalEnergy) : 0.0;
        result.highFreqEnergyRatio = totalEnergy > 1.0e-12 ? juce::jlimit (0.0, 1.0, highFreqEnergy / totalEnergy) : 0.0;
        result.spectralCentroidHz = centroidDen > 1.0e-9 ? centroidNum / centroidDen : 0.0;
        result.windowsAveraged = windowsDone;
        result.fftSizeUsed = fftSize;
        result.residualEnergyRatio = totalEnergy > 1.0e-12 ? juce::jlimit (0.0, 1.0, residualEnergy / totalEnergy) : 0.0;

        if (partialEnergy > 1.0e-15 && residualEnergy > 0.0)
            result.asrDb = 10.0 * std::log10 (residualEnergy / partialEnergy);

        // NMR per critical band - see the comment above this function for the
        // simplifications this makes and why.
        if (totalEnergy > 1.0e-12)
        {
            constexpr double maskingDepthDb = 15.0;
            constexpr double spreadUpDbPerBark = 12.0;   // masker below the band
            constexpr double spreadDownDbPerBark = 27.0; // masker above the band
            const double absFloor = totalEnergy * 1.0e-10;

            for (int i = 0; i < numBands; ++i)
            {
                if (bandResidual[(size_t) i] <= absFloor)
                    continue;

                double masker = 0.0;

                for (int j = 0; j < numBands; ++j)
                {
                    if (bandPartial[(size_t) j] <= 0.0)
                        continue;

                    const double slopeDb = j <= i ? -spreadUpDbPerBark * (i - j)
                                                  : -spreadDownDbPerBark * (j - i);
                    masker += bandPartial[(size_t) j] * std::pow (10.0, slopeDb / 10.0);
                }

                const double threshold = juce::jmax (absFloor, masker * std::pow (10.0, -maskingDepthDb / 10.0));
                const double nmrDb = 10.0 * std::log10 (bandResidual[(size_t) i] / threshold);

                if (nmrDb > result.nmrMaxDb)
                {
                    result.nmrMaxDb = nmrDb;
                    // Report the band by its centre frequency, inverting the Bark
                    // scale numerically (it has no closed form).
                    const double targetBark = (double) i + 0.5;
                    double lo = 20.0, hi = 20000.0;

                    for (int it = 0; it < 40; ++it)
                    {
                        const double mid = 0.5 * (lo + hi);
                        (hzToBark (mid) < targetBark ? lo : hi) = mid;
                    }

                    result.nmrWorstBandHz = 0.5 * (lo + hi);
                }
            }
        }

        for (size_t i = 0; i < nonHarmonicPeaks.size() && i < 5; ++i)
            result.topNonHarmonicPeaks.push_back ({ nonHarmonicPeaks[i].freqHz, nonHarmonicPeaks[i].mag });

        // Residual peaks are what the direction test needs: an aliased image of
        // harmonic k sits at |k*f0 - fs| and so moves DOWN as f0 moves up,
        // while detune sidebands, shimmer and reverb content move up with it.
        std::sort (residualPeaks.begin(), residualPeaks.end(),
                   [] (const BinPeak& a, const BinPeak& b) { return a.mag > b.mag; });

        for (size_t i = 0; i < residualPeaks.size() && i < 8; ++i)
            result.topResidualPeaks.push_back ({ residualPeaks[i].freqHz, residualPeaks[i].mag });

        return result;
    }

    struct ClickReport
    {
        int count = 0;
        std::vector<double> firstTimestampsSec; // up to 10
    };

    ClickReport detectClicks (const juce::AudioBuffer<float>& buffer, double sampleRate, float threshold)
    {
        ClickReport report;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int n = 1; n < buffer.getNumSamples(); ++n)
            {
                const auto delta = std::abs (data[n] - data[n - 1]);

                if (delta > threshold)
                {
                    ++report.count;

                    if (report.firstTimestampsSec.size() < 10)
                        report.firstTimestampsSec.push_back ((double) n / sampleRate);
                }
            }
        }

        return report;
    }

    //--------------------------------------------------------------------
    // Note-on / note-off transient windows.
    //
    // Playbook 4.5 asks for "single note, look at the first and last 100
    // samples". A click is a step that is large *relative to the signal
    // around it*, not one that is large absolutely - a 0.05 step into
    // silence is audible, the same step mid-chord is not - so the step is
    // reported both absolutely and against the RMS of the window just
    // before it.
    //--------------------------------------------------------------------
    struct TransientWindow
    {
        double maxAbsStep = 0.0;
        double rmsInWindow = 0.0;
        double rmsBefore = 0.0;
        double stepRelDb = -300.0;
    };

    TransientWindow analyzeTransient (const juce::AudioBuffer<float>& buffer, int atSample, int windowSamples)
    {
        TransientWindow t;

        const auto numSamples = buffer.getNumSamples();
        const auto numCh = buffer.getNumChannels();

        if (numSamples <= 1 || windowSamples < 2)
            return t;

        const int start    = juce::jlimit (0, numSamples - 1, atSample);
        const int end      = juce::jlimit (0, numSamples, start + windowSamples);
        const int preStart = juce::jmax (0, start - windowSamples);

        double ss = 0.0, ssPre = 0.0;
        int n1 = 0, n2 = 0;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* d = buffer.getReadPointer (ch);

            for (int n = juce::jmax (1, start); n < end; ++n)
            {
                t.maxAbsStep = juce::jmax (t.maxAbsStep, (double) std::abs (d[n] - d[n - 1]));
                ss += (double) d[n] * d[n];
                ++n1;
            }

            for (int n = preStart; n < start; ++n)
            {
                ssPre += (double) d[n] * d[n];
                ++n2;
            }
        }

        t.rmsInWindow = n1 > 0 ? std::sqrt (ss / n1) : 0.0;
        t.rmsBefore   = n2 > 0 ? std::sqrt (ssPre / n2) : 0.0;

        if (t.rmsBefore > 1.0e-9 && t.maxAbsStep > 0.0)
            t.stepRelDb = 20.0 * std::log10 (t.maxAbsStep / t.rmsBefore);

        return t;
    }

    //--------------------------------------------------------------------
    // ITU-R BS.1770-4 integrated loudness (LUFS) and 4x-oversampled true peak.
    //
    // Loudness matching is a LUFS job, not an RMS one (playbook 4.8: "Match
    // with integrated LUFS, not peak"). Whole-render RMS is especially
    // misleading for this instrument: it is diluted by however much reverb
    // tail --tail happened to include, so two identical patches rendered with
    // different tail lengths report different RMS.
    //
    // The K-weighting coefficients are derived per sample rate with the
    // standard libebur128 derivation, which reproduces the spec's 48 kHz
    // table exactly and generalises to the other rates this tool renders at.
    //--------------------------------------------------------------------
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    void makeKWeighting (double rate, Biquad& shelf, Biquad& hp)
    {
        {   // Stage 1: high shelf, +4 dB.
            const double f0 = 1681.974450955533;
            const double G  = 3.999843853973347;
            const double Q  = 0.7071752369554196;
            const double K  = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;

            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }

        {   // Stage 2: RLB high-pass. Numerator is [1, -2, 1] unnormalised, per the spec.
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;
            const double K  = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const double a0 = 1.0 + K / Q + K * K;

            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0;
            hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    struct LoudnessResult
    {
        bool valid = false;
        double integratedLufs = -300.0;
        double truePeak = 0.0;
        double truePeakDb = -300.0;
        int gatedBlocks = 0;
    };

    /*  truePeak is opt-in because it is by far the most expensive metric here:
        4x windowed-sinc upsampling of the whole render, per channel. The
        sound-design iteration loop runs hundreds of renders and only needs
        LUFS and sample peak, so paying for it every time would slow that loop
        down for nothing. Turn it on with --true-peak when a ceiling decision
        actually depends on it.
    */
    LoudnessResult measureLoudness (const juce::AudioBuffer<float>& buffer, int totalSamples, double sampleRate, bool wantTruePeak)
    {
        LoudnessResult r;

        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int blockSamples = (int) std::round (0.4 * sampleRate); // 400 ms
        const int hopSamples   = (int) std::round (0.1 * sampleRate); // 75% overlap

        if (numCh < 1 || totalSamples < blockSamples || hopSamples < 1)
            return r;

        // K-weight each channel.
        std::vector<std::vector<double>> weighted ((size_t) numCh);

        for (int ch = 0; ch < numCh; ++ch)
        {
            Biquad shelf, hp;
            makeKWeighting (sampleRate, shelf, hp);

            const auto* d = buffer.getReadPointer (ch);
            weighted[(size_t) ch].resize ((size_t) totalSamples);

            for (int n = 0; n < totalSamples; ++n)
                weighted[(size_t) ch][(size_t) n] = hp.process (shelf.process ((double) d[n]));
        }

        // Per-block mean square, summed over channels (G = 1.0 for L and R).
        std::vector<double> blockPower;

        for (int start = 0; start + blockSamples <= totalSamples; start += hopSamples)
        {
            double sum = 0.0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                double ss = 0.0;
                const auto& w = weighted[(size_t) ch];

                for (int n = start; n < start + blockSamples; ++n)
                    ss += w[(size_t) n] * w[(size_t) n];

                sum += ss / (double) blockSamples;
            }

            blockPower.push_back (sum);
        }

        if (blockPower.empty())
            return r;

        const auto toLufs = [] (double power) { return power > 0.0 ? -0.691 + 10.0 * std::log10 (power) : -300.0; };

        const auto meanOf = [] (const std::vector<double>& v)
        {
            double sum = 0.0;
            for (auto x : v) sum += x;
            return v.empty() ? 0.0 : sum / (double) v.size();
        };

        // Absolute gate at -70 LUFS, then the -10 LU relative gate.
        std::vector<double> pass1;
        for (auto power : blockPower)
            if (toLufs (power) > -70.0)
                pass1.push_back (power);

        if (pass1.empty())
            return r;

        const double relativeThreshold = toLufs (meanOf (pass1)) - 10.0;

        std::vector<double> pass2;
        for (auto power : pass1)
            if (toLufs (power) > relativeThreshold)
                pass2.push_back (power);

        if (pass2.empty())
            return r;

        r.integratedLufs = toLufs (meanOf (pass2));
        r.gatedBlocks = (int) pass2.size();

        r.valid = true;

        if (! wantTruePeak)
            return r;

        // True peak: 4x oversampled (BS.1770 Annex 2).
        const int upSamples = totalSamples * 4;
        std::vector<float> up ((size_t) upSamples, 0.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            juce::Interpolators::WindowedSinc interp;
            interp.reset();
            interp.process (0.25, buffer.getReadPointer (ch), up.data(), upSamples);

            for (auto v : up)
                r.truePeak = juce::jmax (r.truePeak, (double) std::abs (v));
        }

        r.truePeakDb = r.truePeak > 0.0 ? 20.0 * std::log10 (r.truePeak) : -300.0;

        return r;
    }

    /** Block RMS per window, used both for the reported envelope and for timing. */
    std::vector<double> computeEnvelope (const juce::AudioBuffer<float>& buffer, int totalSamples, int windowSamples)
    {
        std::vector<double> env;

        const auto* readL = buffer.getReadPointer (0);
        const auto* readR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : readL;

        for (int start = 0; start < totalSamples; start += windowSamples)
        {
            const int len = juce::jmin (windowSamples, totalSamples - start);
            double ss = 0.0;

            for (int n = start; n < start + len; ++n)
                ss += 0.5 * ((double) readL[n] * readL[n] + (double) readR[n] * readR[n]);

            env.push_back (std::sqrt (ss / (double) len));
        }

        return env;
    }

    //--------------------------------------------------------------------
    // Envelope timing (4.5: "measure actual attack/decay against the
    // displayed ms"). Measured on a 2 ms-resolution envelope, since the
    // onset JND is ~2 ms and finer numbers would be wasted work.
    //
    // The release figures are contaminated by the shared reverb tail -
    // measure release with --param=reverb=0 or they describe the room, not
    // the envelope.
    //--------------------------------------------------------------------
    struct EnvelopeTiming
    {
        bool valid = false;
        double resolutionMs = 0.0;
        double peakRms = 0.0;
        double timeToPeakSec = 0.0;
        double attack10to90Sec = -1.0;
        double sustainRms = 0.0;
        double sustainRatio = 0.0;       // sustain / peak
        double releaseToMinus20Sec = -1.0;
        double releaseToMinus60Sec = -1.0;
    };

    EnvelopeTiming analyzeEnvelopeTiming (const std::vector<double>& env,
                                          double windowSec,
                                          int firstOnSample,
                                          int firstOffSample,
                                          int lastOffSample,
                                          double sampleRate)
    {
        EnvelopeTiming t;

        if (env.size() < 4 || windowSec <= 0.0)
            return t;

        const auto idxOf = [&] (int sample)
        {
            return juce::jlimit (0, (int) env.size() - 1, (int) std::floor ((double) sample / (sampleRate * windowSec)));
        };

        const int onIdx  = idxOf (firstOnSample);
        const int offIdx = idxOf (firstOffSample);
        const int lastOffIdx = idxOf (lastOffSample);

        if (offIdx <= onIdx)
            return t;

        t.valid = true;
        t.resolutionMs = windowSec * 1000.0;

        int peakIdx = onIdx;

        for (int i = onIdx; i <= offIdx; ++i)
            if (env[(size_t) i] > env[(size_t) peakIdx])
                peakIdx = i;

        t.peakRms = env[(size_t) peakIdx];
        t.timeToPeakSec = (double) (peakIdx - onIdx) * windowSec;

        if (t.peakRms > 1.0e-9)
        {
            int i10 = -1, i90 = -1;

            for (int i = onIdx; i <= peakIdx; ++i)
            {
                if (i10 < 0 && env[(size_t) i] >= 0.1 * t.peakRms) i10 = i;
                if (i90 < 0 && env[(size_t) i] >= 0.9 * t.peakRms) { i90 = i; break; }
            }

            if (i10 >= 0 && i90 >= i10)
                t.attack10to90Sec = (double) (i90 - i10) * windowSec;
        }

        // Sustain: the last quarter of the hold region, away from the attack.
        const int sustainStart = offIdx - juce::jmax (1, (offIdx - onIdx) / 4);
        double sum = 0.0;
        int count = 0;

        for (int i = juce::jmax (onIdx, sustainStart); i < offIdx; ++i)
        {
            sum += env[(size_t) i];
            ++count;
        }

        t.sustainRms = count > 0 ? sum / count : 0.0;
        t.sustainRatio = t.peakRms > 1.0e-9 ? t.sustainRms / t.peakRms : 0.0;

        // Release: decay below -20 / -60 dB of the level at the last note-off.
        const double atOff = env[(size_t) lastOffIdx];

        if (atOff > 1.0e-9)
        {
            const double t20 = atOff * std::pow (10.0, -20.0 / 20.0);
            const double t60 = atOff * std::pow (10.0, -60.0 / 20.0);

            for (int i = lastOffIdx; i < (int) env.size(); ++i)
            {
                if (t.releaseToMinus20Sec < 0.0 && env[(size_t) i] <= t20)
                    t.releaseToMinus20Sec = (double) (i - lastOffIdx) * windowSec;

                if (env[(size_t) i] <= t60)
                {
                    t.releaseToMinus60Sec = (double) (i - lastOffIdx) * windowSec;
                    break;
                }
            }
        }

        return t;
    }
}

//==============================================================================
// The actual entry point, wrapped so main() can guarantee MessageManager's
// singleton is torn down (via deleteInstance()) on every exit path - JUCE's
// leak detector otherwise flags it (and asserts) at static-teardown time.
static int runTool (int argc, char* argv[])
{
    const auto args = parseArgs (argc, argv);

    if (args.has ("help"))
    {
        printUsage();
        return 0;
    }

    // Rule 28 / harness contract: a misspelt flag must not render a different
    // patch than the one asked for.
    static const juce::StringArray knownFlags { "help", "list-presets", "list-params", "sample-rate", "block", "max-block",
        "hold", "tail", "seconds", "gate", "velocity", "window-ms", "fft-size", "click-threshold", "transient-window",
        "probe-at", "out", "partial-ratios", "notes", "note-onsets", "note-holds", "seed", "state-in", "state-out",
        "preset", "preset-at", "solo", "layer", "param", "pedal", "automate", "bend", "mod", "profile", "warmup",
        "reprepare-at", "true-peak", "diagnostics", "import-preset", "export-preset" };

    for (const auto& [key, value] : args.values)
    {
        if (! knownFlags.contains (key))
        {
            std::cerr << "error: unknown flag --" << key.toStdString() << " (see --help)" << std::endl;
            return 2;
        }
    }

    if (args.has ("list-presets"))
    {
        const auto& presets = getFactoryPresets();
        juce::Array<juce::var> arr;

        for (int i = 0; i < (int) presets.size(); ++i)
            arr.add (presetToVar (presets[(size_t) i], i));

        std::cout << juce::JSON::toString (juce::var (arr), true) << std::endl;
        return 0;
    }

    // The plugin's real parameter layout in host order (harness contract
    // --list-params), for paramgen.py check --list-params and the test that
    // diffs it against parameters.csv: default normalised, plus the plain
    // range and name so the test can compare those too.
    if (args.has ("list-params"))
    {
        HorizonPadAudioProcessor processor;
        juce::Array<juce::var> arr;

        for (auto* param : processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);

            if (ranged == nullptr)
                continue;

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("id", ranged->getParameterID());
            obj->setProperty ("name", ranged->getName (64));
            obj->setProperty ("default", ranged->getDefaultValue());
            obj->setProperty ("default_plain", ranged->convertFrom0to1 (ranged->getDefaultValue()));
            obj->setProperty ("min", ranged->getNormalisableRange().start);
            obj->setProperty ("max", ranged->getNormalisableRange().end);
            obj->setProperty ("steps", ranged->getNumSteps() == juce::AudioProcessor::getDefaultNumParameterSteps() ? 0 : ranged->getNumSteps());
            obj->setProperty ("automatable", ranged->isAutomatable());
            obj->setProperty ("text_default", ranged->getText (ranged->getDefaultValue(), 64));
            arr.add (juce::var (obj));
        }

        std::cout << juce::JSON::toString (juce::var (arr), true) << std::endl;
        return 0;
    }

    // ---- Config -------------------------------------------------------
    const double sampleRate   = (double) args.getFloat ("sample-rate", 48000.0f);
    // Harness contract v3: --seconds is the render length, --gate the
    // note-off (held to the end without it); --hold/--tail are the older
    // spelling of the same and stay the default.
    const bool   fixedLength  = args.has ("seconds");
    const double renderLength = (double) args.getFloat ("seconds", 0.0f);
    const double holdSeconds  = args.has ("gate") ? (double) args.getFloat ("gate", 4.0f)
                              : fixedLength       ? renderLength
                                                  : (double) args.getFloat ("hold", 4.0f);
    const double tailSeconds  = fixedLength ? juce::jmax (0.0, renderLength - holdSeconds) : (double) args.getFloat ("tail", 6.0f);

    // --block=<n>, or --block=random:<seed> - every block a random size from 0
    // to --max-block (D.5.1: a zero-length block must be legal). --max-block is
    // what prepareToPlay announces; a fixed --block above it tests blocks
    // larger than announced.
    const auto   blockArg     = args.getString ("block", "512");
    const bool   randomBlocks = blockArg.startsWith ("random:");
    const bool   zeroBlocks   = blockArg.trim() == "0";   // a zero-length block before every --max-block one
    const int    maxBlock     = args.getInt ("max-block", randomBlocks || zeroBlocks ? 512 : blockArg.getIntValue());
    const int    blockSize    = randomBlocks || zeroBlocks ? maxBlock : blockArg.getIntValue();
    juce::Random blockRng (randomBlocks ? blockArg.fromFirstOccurrenceOf (":", false, false).getLargeIntValue() : 0);

    if (blockSize < 1 || maxBlock < 1 || (randomBlocks && ! blockArg.fromFirstOccurrenceOf (":", false, false).containsOnly ("0123456789")))
    {
        std::cerr << "error: --block must be a positive number or random:<seed>, --max-block positive" << std::endl;
        return 2;
    }
    const float  velocity     = juce::jlimit (0.0f, 1.0f, args.getFloat ("velocity", 0.85f));
    const double windowMs     = (double) args.getFloat ("window-ms", 50.0f);
    const int    fftSizeReq   = args.getInt ("fft-size", 8192);
    const float  clickThresh  = args.getFloat ("click-threshold", 0.2f);
    const int    transientWin = juce::jmax (2, args.getInt ("transient-window", 100));

    // Arbitrary probe points, in seconds. A note-on and a note-off are probed
    // automatically; this is for everything else worth looking at - above all
    // the instant a voice is stolen, which is not a MIDI event in this engine
    // and so has no other timestamp to hang a measurement on.
    std::vector<double> probeTimes;
    for (auto& tok : args.getList ("probe-at", ""))
        probeTimes.push_back (juce::jmax (0.0, (double) tok.getFloatValue()));
    const auto   outPath      = args.getString ("out", "");

    std::vector<double> partialRatios;
    for (auto& tok : args.getList ("partial-ratios", "0.5,1,2"))
    {
        const auto r = (double) tok.getFloatValue();
        if (r > 0.0)
            partialRatios.push_back (r);
    }

    if (partialRatios.empty())
        partialRatios.push_back (1.0);

    std::vector<int> notes;
    for (auto& tok : args.getList ("notes", "60"))
        notes.push_back (juce::jlimit (0, 127, tok.getIntValue()));

    if (notes.empty())
        notes.push_back (60);

    // Per-note onset/hold overrides, matched positionally to --notes. A note
    // with no entry starts at 0 and holds for --hold, so the default schedule
    // is exactly the old behaviour: every note on at 0, off at --hold.
    //
    // This is what makes voice stealing testable: eight notes at 0 plus a
    // ninth at 3 s forces a steal on an 8-slot engine that has settled.
    std::vector<double> onsets, holds;

    for (auto& tok : args.getList ("note-onsets", ""))
        onsets.push_back (juce::jmax (0.0, (double) tok.getFloatValue()));

    for (auto& tok : args.getList ("note-holds", ""))
        holds.push_back (juce::jmax (0.0, (double) tok.getFloatValue()));

    struct ScheduledNote { int note; double onSec; double holdSec; int onSample; int offSample; };
    std::vector<ScheduledNote> schedule;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        ScheduledNote sn {};
        sn.note = notes[i];
        sn.onSec = i < onsets.size() ? onsets[i] : 0.0;
        sn.holdSec = i < holds.size() ? holds[i] : holdSeconds;
        sn.onSample = (int) std::round (sn.onSec * sampleRate);
        sn.offSample = sn.onSample + (int) std::round (sn.holdSec * sampleRate);
        schedule.push_back (sn);
    }

    // ---- Build the processor -------------------------------------------
    HorizonPadAudioProcessor processor;
    const auto startupProgram = processor.getProgramName (processor.getCurrentProgram());  // before any preset/param
    processor.prepareToPlay (sampleRate, maxBlock);

    // The About box's diagnostics block, as a friend would paste it (test
    // about_copy_is_fourteen_lines); the rate and block are the ones asked for.
    if (args.has ("diagnostics"))
    {
        processor.setRateAndBufferSizeDetails (sampleRate, maxBlock);
        std::cout << horizon::diagnosticsText (processor) << std::endl;
        return 0;
    }

    // Oscillator start phases are randomised per voice on purpose, so renders
    // vary run to run. --seed pins them, which is what makes a null test, a
    // regression baseline or an A/B of a code change meaningful. Without it,
    // any metric that depends on phase (peak, spectral ratios, DC) has to be
    // averaged over several runs before it means anything.
    if (args.has ("seed"))
        processor.setDeterministicSeed ((juce::int64) args.getInt ("seed", 1));

    // 0) Optional session state (harness contract --state-in), first so the
    //    flags below override it. Any bytes: a refused blob leaves the plugin
    //    as it was and the render still runs (it keeps processing), then the
    //    run exits 2 with the reason on stderr; a newer one loads as far as
    //    understood with a note.
    juce::String stateRefused;

    if (args.has ("state-in"))
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args.getString ("state-in", ""));
        juce::MemoryBlock blob;

        if (! file.existsAsFile() || ! file.loadFileAsData (blob))
        {
            std::cerr << "error: cannot read --state-in " << file.getFullPathName().toStdString() << std::endl;
            return 2;
        }

        processor.setStateInformation (blob.getData(), (int) blob.getSize());

        if (const auto notice = processor.getStateNotice(); notice.startsWith ("refused"))
            stateRefused = notice;
        else if (notice.isNotEmpty())
            std::cerr << "note: " << notice.toStdString() << std::endl;
    }

    // 0b) A .bobpreset file imported as the preset bar does it (copied
    //     into the User folder and recalled); refused -> exit 2 with the reason.
    if (args.has ("import-preset"))
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args.getString ("import-preset", ""));

        if (const auto error = processor.importPreset (file); error.isNotEmpty())
        {
            std::cerr << "error: --import-preset refused: " << error.toStdString() << std::endl;
            return 2;
        }
    }

    // 1) Optional factory preset.
    if (args.has ("preset"))
    {
        const auto key = args.getString ("preset", "");
        const auto* preset = findPreset (key);

        if (preset == nullptr)
        {
            std::cerr << "error: no factory preset matches --preset=" << key.toStdString()
                       << " (see --list-presets)" << std::endl;
            return 2;
        }

        // The plugin's own recall path, as the preset bar uses it.
        processor.applyFactoryPreset ((int) (preset - getFactoryPresets().data()));
    }

    // 2) Optional solo: zero every layer not named, set the named ones to 1.0.
    if (args.has ("solo") || args.has ("layer"))
    {
        const auto soloList = args.has ("solo") ? args.getList ("solo", "") : args.getList ("layer", "");
        std::vector<juce::String> soloIds;

        for (auto& name : soloList)
            soloIds.push_back (resolveParamId (name));

        for (auto* id : allVolumeIds())
        {
            const bool solo = std::find (soloIds.begin(), soloIds.end(), juce::String (id)) != soloIds.end();

            if (auto* p = processor.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (solo ? 1.0f : 0.0f);   // normalised: full or silent
        }
    }

    // 3) Raw parameter overrides, repeatable: --param=id=value. Repeated flags
    //    accumulate (see parseArgs); a semicolon-separated batch works too:
    //    --param=root=1;filter=0.8. The applied state is echoed back under
    //    "activeParams" - scripts should assert on it.
    if (args.has ("param"))
    {
        const auto raw = args.getString ("param", "");

        for (auto assignment : juce::StringArray::fromTokens (raw, ";", ""))
        {
            assignment = assignment.trim();
            if (assignment.isEmpty())
                continue;

            const auto eq = assignment.indexOfChar ('=');
            if (eq < 0)
            {
                std::cerr << "error: --param entries must be id=value, got: " << assignment.toStdString() << std::endl;
                return 2;
            }

            const auto id = resolveParamId (assignment.substring (0, eq));
            const auto value = juce::jlimit (0.0f, 1.0f, assignment.substring (eq + 1).getFloatValue());

            auto* p = processor.getAPVTS().getParameter (id);
            if (p == nullptr)
            {
                std::cerr << "error: unknown parameter id: " << id.toStdString() << std::endl;
                return 2;
            }

            p->setValueNotifyingHost (value);   // normalised 0..1 (harness contract)
        }
    }

    // --preset-at=<t>:<slug> (a factory preset recalled mid-render, the
    // preset-change spike of D.2.1) and --reprepare-at=<t>:<rate>/<block>
    // (prepareToPlay again without releaseResources, pluginval's
    // non-releasing case, D.5.1). Both repeatable, applied at the first block
    // boundary at or after t.
    struct TimedPreset { double t; int index; };
    struct Reprepare  { double t; double rate; int block; };
    std::vector<TimedPreset> presetChanges;
    std::vector<Reprepare> reprepares;

    for (auto entry : juce::StringArray::fromTokens (args.getString ("preset-at", ""), ";", ""))
    {
        entry = entry.trim();
        if (entry.isEmpty())
            continue;

        const auto* preset = findPreset (entry.fromFirstOccurrenceOf (":", false, false));

        if (! entry.containsChar (':') || preset == nullptr)
        {
            std::cerr << "error: --preset-at needs <seconds>:<slug> with a factory preset, got: " << entry.toStdString() << std::endl;
            return 2;
        }

        presetChanges.push_back ({ entry.upToFirstOccurrenceOf (":", false, false).getDoubleValue(),
                                   (int) (preset - getFactoryPresets().data()) });
    }

    for (auto entry : juce::StringArray::fromTokens (args.getString ("reprepare-at", ""), ";", ""))
    {
        entry = entry.trim();
        if (entry.isEmpty())
            continue;

        const auto spec = entry.fromFirstOccurrenceOf (":", false, false);
        const auto rate = spec.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
        const auto block = spec.fromFirstOccurrenceOf ("/", false, false).getIntValue();

        if (! entry.containsChar (':') || ! spec.containsChar ('/') || rate < 8000.0 || block < 1)
        {
            std::cerr << "error: --reprepare-at needs <seconds>:<rate>/<block>, got: " << entry.toStdString() << std::endl;
            return 2;
        }

        reprepares.push_back ({ entry.upToFirstOccurrenceOf (":", false, false).getDoubleValue(), rate, block });
    }

    std::sort (presetChanges.begin(), presetChanges.end(), [] (auto& x, auto& y) { return x.t < y.t; });
    std::sort (reprepares.begin(), reprepares.end(), [] (auto& x, auto& y) { return x.t < y.t; });

    // ---- Render ---------------------------------------------------------
    int firstOnSample = std::numeric_limits<int>::max();
    int firstOffSample = std::numeric_limits<int>::max();
    int lastOffSample = 0;

    for (auto& sn : schedule)
    {
        firstOnSample  = juce::jmin (firstOnSample, sn.onSample);
        firstOffSample = juce::jmin (firstOffSample, sn.offSample);
        lastOffSample  = juce::jmax (lastOffSample, sn.offSample);
    }

    // Kept as the name the JSON has always used: the LAST note-off, i.e. the
    // point after which only release and reverb tail remain.
    const int noteOffSample = lastOffSample;

    // Sustain pedal: --pedal=down,up[,down,up...] in seconds, sent as CC64
    // 127 / 0. The render runs until the tail after the later of the last
    // note-off and the last pedal-up, so a pedalled release is captured.
    std::vector<std::pair<int, int>> pedalEvents; // (sample, value)
    {
        const auto times = args.getList ("pedal", "");

        for (size_t i = 0; i < times.size(); ++i)
            pedalEvents.push_back ({ (int) std::round (times[i].getDoubleValue() * sampleRate), i % 2 == 0 ? 127 : 0 });
    }

    // Wheels: --bend=t:v (v -1..+1) as 14-bit pitch-wheel messages, --mod=t:v
    // (v 0..1) as CC1 - through the processor's real MIDI path, so the
    // control mappings are tested where a keyboard drives them (rule 30).
    std::vector<std::pair<int, juce::MidiMessage>> wheelEvents;
    {
        auto parseTimed = [&] (const char* flag, auto makeMessage)
        {
            for (auto& tok : args.getList (flag, ""))
            {
                const auto colon = tok.indexOfChar (':');
                if (colon < 0)
                    continue;

                const auto t = tok.substring (0, colon).getDoubleValue();
                const auto v = tok.substring (colon + 1).getFloatValue();
                wheelEvents.push_back ({ (int) std::round (t * sampleRate), makeMessage (v) });
            }
        };

        parseTimed ("bend", [] (float v)
        {
            const auto wheel = juce::jlimit (0, 16383, (int) std::round (8192.0f + juce::jlimit (-1.0f, 1.0f, v) * 8191.0f));
            return juce::MidiMessage::pitchWheel (1, wheel);
        });

        parseTimed ("mod", [] (float v)
        {
            return juce::MidiMessage::controllerEvent (1, 1, juce::jlimit (0, 127, (int) std::round (juce::jlimit (0.0f, 1.0f, v) * 127.0f)));
        });
    }

    // Host-style automation, the harness contract's syntax:
    // --automate=id:t0=v0,t1=v1,... - a linear ramp through the points,
    // applied at the start of every block (JUCE hosts deliver automation per
    // block, rule 29). Before the first point the parameter is left alone;
    // after the last it holds.
    struct Automation { juce::RangedAudioParameter* param; std::vector<std::pair<double, double>> points; };
    std::vector<Automation> automations;

    for (auto entry : juce::StringArray::fromTokens (args.getString ("automate", ""), ";", ""))
    {
        entry = entry.trim();
        if (entry.isEmpty())
            continue;

        const auto colon = entry.indexOfChar (':');
        auto* p = colon > 0 ? processor.getAPVTS().getParameter (resolveParamId (entry.substring (0, colon))) : nullptr;

        if (p == nullptr)
        {
            std::cerr << "error: --automate needs id:t0=v0,t1=v1,... with a known id, got: " << entry.toStdString() << std::endl;
            return 2;
        }

        Automation a { p, {} };

        for (auto point : juce::StringArray::fromTokens (entry.substring (colon + 1), ",", ""))
        {
            const auto eq = point.indexOfChar ('=');
            if (eq > 0)
                a.points.push_back ({ point.substring (0, eq).getDoubleValue(), point.substring (eq + 1).getDoubleValue() });
        }

        std::sort (a.points.begin(), a.points.end());

        if (a.points.empty())
        {
            std::cerr << "error: --automate entry has no t=v points: " << entry.toStdString() << std::endl;
            return 2;
        }

        automations.push_back (std::move (a));
    }

    auto automationValueAt = [] (const Automation& a, double t) -> std::optional<float>
    {
        if (t < a.points.front().first)
            return std::nullopt;

        for (size_t i = 1; i < a.points.size(); ++i)
        {
            const auto& [t0, v0] = a.points[i - 1];
            const auto& [t1, v1] = a.points[i];

            if (t < t1)
                return (float) juce::jlimit (0.0, 1.0, v0 + (v1 - v0) * (t1 > t0 ? (t - t0) / (t1 - t0) : 1.0));
        }

        return (float) juce::jlimit (0.0, 1.0, a.points.back().second);
    };

    int lastEventSample = lastOffSample;
    for (auto& [sample, value] : pedalEvents)
        lastEventSample = juce::jmax (lastEventSample, sample);
    for (auto& ev : wheelEvents)
        lastEventSample = juce::jmax (lastEventSample, ev.first);

    const int totalSamples  = lastEventSample + (int) std::round (tailSeconds * sampleRate);

    juce::AudioBuffer<float> render (2, juce::jmax (1, totalSamples));
    render.clear();

    const bool timeBlocks = args.has ("profile");
    std::vector<double> blockMicros;
    std::vector<double> blockStartSec;
    std::vector<int> blockLengths;

    if (timeBlocks)
    {
        blockMicros.reserve ((size_t) (totalSamples / juce::jmax (1, blockSize) + 1));
        blockStartSec.reserve (blockMicros.capacity());
    }

    auto renderAll = [&]
    {
    int position = 0;
    size_t nextPresetChange = 0, nextReprepare = 0;

    while (position < totalSamples)
    {
        const auto now = (double) position / sampleRate;

        while (nextPresetChange < presetChanges.size() && presetChanges[nextPresetChange].t <= now)
            processor.applyFactoryPreset (presetChanges[nextPresetChange++].index);

        while (nextReprepare < reprepares.size() && reprepares[nextReprepare].t <= now)
        {
            const auto& r = reprepares[nextReprepare++];
            processor.prepareToPlay (r.rate, r.block);   // no releaseResources(): the non-releasing case
        }

        const int blockLen = randomBlocks ? juce::jmin (blockRng.nextInt (maxBlock + 1), totalSamples - position)
                                          : juce::jmin (blockSize, totalSamples - position);

        if (zeroBlocks)
        {
            juce::AudioBuffer<float> empty (render.getArrayOfWritePointers(), 2, position, 0);
            juce::MidiBuffer noMidi;
            processBlockRealtime (processor, empty, noMidi);
        }

        for (auto& a : automations)
        {
            if (const auto value = automationValueAt (a, (double) position / sampleRate))
                if (! juce::approximatelyEqual (a.param->getValue(), (float) *value))
                    a.param->setValueNotifyingHost ((float) *value);   // normalised 0..1
        }

        juce::MidiBuffer midi;

        // MidiBuffer::addEvent inserts in timestamp order, so the schedule
        // does not need to be sorted.
        for (auto& sn : schedule)
        {
            if (sn.onSample >= position && sn.onSample < position + blockLen)
                midi.addEvent (juce::MidiMessage::noteOn (1, sn.note, velocity), sn.onSample - position);

            if (sn.offSample >= position && sn.offSample < position + blockLen)
                midi.addEvent (juce::MidiMessage::noteOff (1, sn.note), sn.offSample - position);
        }

        for (auto& [sample, value] : pedalEvents)
            if (sample >= position && sample < position + blockLen)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, value), sample - position);

        for (auto& [sample, message] : wheelEvents)
            if (sample >= position && sample < position + blockLen)
                midi.addEvent (message, sample - position);

        juce::AudioBuffer<float> block (render.getArrayOfWritePointers(), 2, position, blockLen);

        if (timeBlocks)
        {
            const auto t0 = juce::Time::getHighResolutionTicks();
            processBlockRealtime (processor, block, midi);
            const auto t1 = juce::Time::getHighResolutionTicks();
            blockMicros.push_back (juce::Time::highResolutionTicksToSeconds (t1 - t0) * 1.0e6);
            blockStartSec.push_back ((double) position / sampleRate);
            blockLengths.push_back (blockLen);
        }
        else
        {
            processBlockRealtime (processor, block, midi);
        }

        position += blockLen;
    }
    };

    // Profiling times the render on this (normal-priority) thread. Measured
    // 2026-10-17 on an M3 Pro: a JUCE real-time thread here - flat out or
    // paced one block per period - made it far worse (p99 45-67% against
    // 6%), consistent with Apple Silicon placing a real-time thread that has
    // not joined an audio device's workgroup on efficiency cores. A host's
    // audio thread has joined one; an offline harness has no device to join.
    // So: normal priority, several runs, and a maximum read by where it falls
    // (one that moves between runs is the OS preempting, not the DSP).
    renderAll();

    // ---- Metrics ----------------------------------------------------------
    const auto* readL = render.getReadPointer (0);
    const auto* readR = render.getNumChannels() > 1 ? render.getReadPointer (1) : readL;

    double peak = 0.0, sumSquares = 0.0, sumL = 0.0, sumR = 0.0;
    long long nanCount = 0, infCount = 0, hotSampleCount = 0; // hot = |x| >= 0.999
    bool trueClip = false;

    for (int n = 0; n < totalSamples; ++n)
    {
        for (auto s : { readL[n], readR[n] })
        {
            if (std::isnan (s)) { ++nanCount; continue; }
            if (std::isinf (s)) { ++infCount; continue; }

            const auto a = std::abs (s);
            peak = juce::jmax (peak, (double) a);
            sumSquares += (double) s * (double) s;

            if (a >= 0.999f) ++hotSampleCount;
            if (a > 1.0f) trueClip = true;
        }

        sumL += readL[n];
        sumR += readR[n];
    }

    const auto totalSampleFrames = (double) totalSamples * 2.0;
    const auto rms = totalSampleFrames > 0 ? std::sqrt (sumSquares / totalSampleFrames) : 0.0;
    const auto dcL = totalSamples > 0 ? sumL / totalSamples : 0.0;
    const auto dcR = totalSamples > 0 ? sumR / totalSamples : 0.0;

    // Envelope shape: RMS per window across the whole render.
    const int windowSamples = juce::jmax (1, (int) std::round (windowMs * 0.001 * sampleRate));
    const auto envelope = computeEnvelope (render, totalSamples, windowSamples);

    // Timing runs on its own 2 ms envelope: the onset JND is ~2 ms, and the
    // reporting window (--window-ms, 50 ms by default) is far too coarse to
    // measure an attack against.
    const double timingWindowSec = 0.002;
    const int timingWindowSamples = juce::jmax (1, (int) std::round (timingWindowSec * sampleRate));
    const auto timingEnvelope = computeEnvelope (render, totalSamples, timingWindowSamples);
    const auto timing = analyzeEnvelopeTiming (timingEnvelope,
                                               (double) timingWindowSamples / sampleRate,
                                               firstOnSample, firstOffSample, lastOffSample, sampleRate);

    // Note-on and note-off transient windows.
    const auto onsetTransient  = analyzeTransient (render, firstOnSample, transientWin);
    const auto noteOffTransient = analyzeTransient (render, lastOffSample, transientWin);

    std::vector<std::pair<double, TransientWindow>> probes;
    for (auto t : probeTimes)
        probes.emplace_back (t, analyzeTransient (render, (int) std::round (t * sampleRate), transientWin));

    // Spectral / aliasing analysis over the back half of the "hold" region
    // (settled sustain, away from the attack transient and the note-off).
    // Region: the back half of the window in which every note is sounding,
    // i.e. from halfway through the first note's hold to just before the
    // earliest note-off.
    const int marginSamples = (int) std::round (0.05 * sampleRate);
    const int regionStart = juce::jlimit (0, firstOffSample, firstOnSample + (firstOffSample - firstOnSample) / 2);
    const int regionEnd   = juce::jmax (regionStart, firstOffSample - marginSamples);
    const double fundamentalHz = juce::MidiMessage::getMidiNoteInHertz (*std::min_element (notes.begin(), notes.end()));

    const auto loudness = measureLoudness (render, totalSamples, sampleRate, args.has ("true-peak"));

    const auto spectral = analyzeSpectrum (render, sampleRate, regionStart, regionEnd, fundamentalHz, fftSizeReq, partialRatios);
    const auto clicks = detectClicks (render, sampleRate, clickThresh);

    // ---- Optional WAV export ----------------------------------------------
    bool wavWritten = false;
    juce::String wavError;

    if (outPath.isNotEmpty())
    {
        juce::File outFile (outPath);
        outFile.getParentDirectory().createDirectory();
        outFile.deleteFile();

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream (outFile.createOutputStream());

        if (stream != nullptr)
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wavFormat.createWriterFor (stream.get(), sampleRate, 2, 32, {}, 0));

            if (writer != nullptr)
            {
                stream.release(); // writer now owns the stream
                writer->writeFromAudioSampleBuffer (render, 0, totalSamples);
                writer.reset(); // flush
                wavWritten = true;
            }
            else
            {
                wavError = "could not create WAV writer";
            }
        }
        else
        {
            wavError = "could not open output stream for " + outPath;
        }
    }

    // ---- Assemble JSON result ----------------------------------------------
    auto* root = new juce::DynamicObject();

    root->setProperty ("tool", "HorizonPadSoundTool");
    root->setProperty ("ok", true);

    {
        auto* config = new juce::DynamicObject();
        config->setProperty ("sampleRate", sampleRate);
        config->setProperty ("blockSize", blockSize);
        config->setProperty ("holdSeconds", holdSeconds);
        config->setProperty ("tailSeconds", tailSeconds);
        config->setProperty ("totalSeconds", (double) totalSamples / sampleRate);
        config->setProperty ("velocity", velocity);

        juce::Array<juce::var> noteArr;
        for (auto n : notes) noteArr.add (n);
        config->setProperty ("notes", noteArr);
        config->setProperty ("transientWindowSamples", transientWin);
        // Reported so the test suite can assert invariant #11 (latency is
        // declared AND verified) without a separate C++ harness.
        config->setProperty ("latencySamples", processor.getLatencySamples());

        juce::Array<juce::var> ratioArr;
        for (auto r : partialRatios) ratioArr.add (r);
        config->setProperty ("partialRatios", ratioArr);

        // The full note schedule, in --notes order, so a staggered render is
        // reproducible from its own output.
        juce::Array<juce::var> scheduleArr;

        for (auto& sn : schedule)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("note", sn.note);
            so->setProperty ("onSec", (double) sn.onSample / sampleRate);
            so->setProperty ("offSec", (double) sn.offSample / sampleRate);
            scheduleArr.add (juce::var (so));
        }

        config->setProperty ("schedule", scheduleArr);

        if (args.has ("preset")) config->setProperty ("preset", args.getString ("preset", ""));
        if (args.has ("solo"))   config->setProperty ("solo", args.getString ("solo", ""));
        if (args.has ("layer"))  config->setProperty ("solo", args.getString ("layer", ""));
        if (args.has ("param"))  config->setProperty ("paramOverrides", args.getString ("param", ""));
        if (args.has ("automate")) config->setProperty ("automate", args.getString ("automate", ""));
        if (args.has ("bend"))   config->setProperty ("bend", args.getString ("bend", ""));
        if (args.has ("mod"))    config->setProperty ("mod", args.getString ("mod", ""));

        root->setProperty ("config", juce::var (config));
    }

    if (timeBlocks && ! blockMicros.empty())
    {
        // Rule 37: worst case against the deadline, never the mean alone.
        // The first block after prepareToPlay is reported on its own
        // (first_block_pct), --warmup=<n> blocks are left out, and so are the
        // last (shorter) block and zero-length blocks (harness contract).
        const auto warmup = (size_t) juce::jmax (0, args.getInt ("warmup", 0));
        std::vector<double> full;
        std::vector<double> fullStart;
        for (size_t i = juce::jmax ((size_t) 1, warmup); i + 1 < blockMicros.size(); ++i)
        {
            if (blockLengths[i] > 0)
            {
                full.push_back (blockMicros[i]);
                fullStart.push_back (blockStartSec[i]);
            }
        }
        if (full.empty())
        {
            full = blockMicros;
            fullStart = blockStartSec;
        }

        auto sorted = full;
        std::sort (sorted.begin(), sorted.end());
        const auto pct = [&] (double q) { return sorted[(size_t) juce::jlimit (0, (int) sorted.size() - 1, (int) std::ceil (q * (double) sorted.size()) - 1)]; };

        double sum = 0.0;
        for (auto v : full) sum += v;

        const auto deadlineUs = 1.0e6 * (double) blockSize / sampleRate;
        const auto worst = (size_t) std::distance (full.begin(), std::max_element (full.begin(), full.end()));

        // Field names from the harness contract (dsp-kit/harness-contract.md).
        const auto asPct = [&] (double us) { return 100.0 * us / deadlineUs; };
        auto* bt = new juce::DynamicObject();
        bt->setProperty ("blocks", (int) full.size());
        bt->setProperty ("deadline_ms", deadlineUs * 0.001);
        bt->setProperty ("mean_pct", asPct (sum / (double) full.size()));
        bt->setProperty ("p50_pct", asPct (pct (0.50)));
        bt->setProperty ("p99_pct", asPct (pct (0.99)));
        bt->setProperty ("p999_pct", asPct (pct (0.999)));
        bt->setProperty ("max_pct", asPct (sorted.back()));
        bt->setProperty ("worst_block_at_s", fullStart[worst]);
        bt->setProperty ("first_block_pct", blockMicros.empty() ? 0.0 : asPct (blockMicros.front()));
        bt->setProperty ("warmup_blocks", (int) juce::jmax ((size_t) 1, warmup));
        bt->setProperty ("over_deadline", (int) std::count_if (full.begin(), full.end(), [&] (double v) { return v > deadlineUs; }));
        root->setProperty ("profile", juce::var (bt));
    }

    {
        auto* params = new juce::DynamicObject();
        static const std::array<std::pair<const char*, const char*>, 10> shown {{
            { "root", ParamID::rootVolume }, { "clearing", ParamID::clearingVolume },
            { "expanse", ParamID::expanseVolume }, { "bloom", ParamID::bloomVolume },
            { "attack", ParamID::attackMacro }, { "release", ParamID::releaseMacro },
            { "filter", ParamID::filterMacro }, { "reverb", ParamID::reverbMacro },
            { "width", ParamID::widthMacro }, { "detune", ParamID::detuneMacro }
        }};

        for (auto& [alias, id] : shown)
            if (auto* p = processor.getAPVTS().getParameter (id))
                params->setProperty (alias, p->getValue());

        root->setProperty ("activeParams", juce::var (params));
        root->setProperty ("startupProgram", startupProgram);   // the program the plugin opens on
    }

    {
        auto* levels = new juce::DynamicObject();
        levels->setProperty ("peak", peak);
        levels->setProperty ("peakDb", peak > 0.0 ? 20.0 * std::log10 (peak) : -300.0);
        levels->setProperty ("rms", rms);
        levels->setProperty ("rmsDb", rms > 0.0 ? 20.0 * std::log10 (rms) : -300.0);
        levels->setProperty ("dcOffsetL", dcL);
        levels->setProperty ("dcOffsetR", dcR);
        levels->setProperty ("integratedLufs", loudness.valid ? loudness.integratedLufs : -300.0);
        levels->setProperty ("truePeakMeasured", args.has ("true-peak"));
        levels->setProperty ("truePeak", loudness.truePeak);
        levels->setProperty ("truePeakDb", loudness.truePeakDb);
        levels->setProperty ("loudnessValid", loudness.valid);
        levels->setProperty ("loudnessGatedBlocks", loudness.gatedBlocks);
        root->setProperty ("levels", juce::var (levels));
    }

    {
        auto* safety = new juce::DynamicObject();
        safety->setProperty ("nanCount", (double) nanCount);
        safety->setProperty ("infCount", (double) infCount);
        safety->setProperty ("hasNanOrInf", nanCount > 0 || infCount > 0);
        safety->setProperty ("trueClipping", trueClip); // |sample| > 1.0 (shouldn't happen - output stage is tanh-limited)
        safety->setProperty ("hotSampleCount", (double) hotSampleCount); // |sample| >= 0.999, i.e. tanh near-saturation
        safety->setProperty ("hotSampleRatio", totalSamples > 0 ? (double) hotSampleCount / (2.0 * totalSamples) : 0.0);
        safety->setProperty ("clickThreshold", clickThresh);
        safety->setProperty ("clickCount", clicks.count);

        juce::Array<juce::var> clickTimes;
        for (auto t : clicks.firstTimestampsSec) clickTimes.add (t);
        safety->setProperty ("firstClickTimestampsSec", clickTimes);

        root->setProperty ("safety", juce::var (safety));
    }

    {
        auto* tr = new juce::DynamicObject();
        tr->setProperty ("windowSamples", transientWin);

        const auto addWindow = [&] (const char* name, const TransientWindow& t)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("maxAbsStep", t.maxAbsStep);
            o->setProperty ("rmsInWindow", t.rmsInWindow);
            o->setProperty ("rmsBefore", t.rmsBefore);
            o->setProperty ("stepRelDb", t.stepRelDb);
            tr->setProperty (name, juce::var (o));
        };

        addWindow ("noteOn", onsetTransient);
        addWindow ("noteOff", noteOffTransient);

        if (! probes.empty())
        {
            juce::Array<juce::var> probeArr;

            for (auto& [atSec, t] : probes)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("atSec", atSec);
                o->setProperty ("maxAbsStep", t.maxAbsStep);
                o->setProperty ("rmsInWindow", t.rmsInWindow);
                o->setProperty ("rmsBefore", t.rmsBefore);
                o->setProperty ("stepRelDb", t.stepRelDb);
                probeArr.add (juce::var (o));
            }

            tr->setProperty ("probes", probeArr);
        }

        root->setProperty ("transients", juce::var (tr));
    }

    {
        auto* env = new juce::DynamicObject();
        env->setProperty ("windowMs", windowMs);
        env->setProperty ("firstNoteOnAtSec", (double) firstOnSample / sampleRate);
        env->setProperty ("firstNoteOffAtSec", (double) firstOffSample / sampleRate);
        env->setProperty ("noteOffAtSec", (double) noteOffSample / sampleRate); // last note-off

        {
            auto* ti = new juce::DynamicObject();
            ti->setProperty ("valid", timing.valid);

            if (timing.valid)
            {
                ti->setProperty ("resolutionMs", timing.resolutionMs);
                ti->setProperty ("peakRms", timing.peakRms);
                ti->setProperty ("timeToPeakSec", timing.timeToPeakSec);
                ti->setProperty ("attack10to90Sec", timing.attack10to90Sec);
                ti->setProperty ("sustainRms", timing.sustainRms);
                ti->setProperty ("sustainRatio", timing.sustainRatio);
                ti->setProperty ("releaseToMinus20Sec", timing.releaseToMinus20Sec);
                ti->setProperty ("releaseToMinus60Sec", timing.releaseToMinus60Sec);
                ti->setProperty ("note", "release figures include the shared reverb tail - "
                                          "render with --param=reverb=0 to measure the envelope alone");
            }

            env->setProperty ("timing", juce::var (ti));
        }

        juce::Array<juce::var> rmsArr;
        for (auto v : envelope) rmsArr.add (v);
        env->setProperty ("rmsPerWindow", rmsArr);

        root->setProperty ("envelope", juce::var (env));
    }

    {
        auto* spec = new juce::DynamicObject();
        spec->setProperty ("valid", spectral.valid);

        if (spectral.valid)
        {
            spec->setProperty ("fundamentalHz", spectral.fundamentalHz);
            spec->setProperty ("nonHarmonicEnergyRatio", spectral.nonHarmonicRatio);
            spec->setProperty ("highFreqEnergyRatioAbove12kHz", spectral.highFreqEnergyRatio);
            spec->setProperty ("spectralCentroidHz", spectral.spectralCentroidHz);
            spec->setProperty ("fftSizeUsed", spectral.fftSizeUsed);
            spec->setProperty ("windowsAveraged", spectral.windowsAveraged);

            auto* alias = new juce::DynamicObject();
            juce::Array<juce::var> ratioArr;
            for (auto r : partialRatios) ratioArr.add (r);
            alias->setProperty ("partialRatios", ratioArr);
            alias->setProperty ("residualEnergyRatio", spectral.residualEnergyRatio);
            alias->setProperty ("asrDb", spectral.asrDb);
            alias->setProperty ("nmrMaxDb", spectral.nmrMaxDb);
            alias->setProperty ("nmrWorstBandHz", spectral.nmrWorstBandHz);
            juce::Array<juce::var> resPeaks;
            for (auto& pk : spectral.topResidualPeaks)
            {
                auto* po = new juce::DynamicObject();
                po->setProperty ("freqHz", pk.freqHz);
                po->setProperty ("magnitude", pk.magnitude);
                resPeaks.add (juce::var (po));
            }
            alias->setProperty ("topResidualPeaks", resPeaks);
            alias->setProperty ("nmrNote", "NMR < 0 dB = residual is masked. Simplified model: "
                                            "1-Bark bands, 15 dB masking depth, -12/-27 dB per Bark spread");
            spec->setProperty ("alias", juce::var (alias));

            juce::Array<juce::var> peaks;
            for (auto& pk : spectral.topNonHarmonicPeaks)
            {
                auto* po = new juce::DynamicObject();
                po->setProperty ("freqHz", pk.freqHz);
                po->setProperty ("magnitude", pk.magnitude);
                peaks.add (juce::var (po));
            }
            spec->setProperty ("topNonHarmonicPeaks", peaks);
        }
        else
        {
            spec->setProperty ("reason", "render region too short for the requested FFT size, or silent");
        }

        root->setProperty ("spectrum", juce::var (spec));
    }

    if (outPath.isNotEmpty())
    {
        auto* wav = new juce::DynamicObject();
        wav->setProperty ("path", outPath);
        wav->setProperty ("written", wavWritten);
        if (wavError.isNotEmpty()) wav->setProperty ("error", wavError);
        root->setProperty ("wav", juce::var (wav));
    }

    // The current sound as a .bobpreset (the preset bar's Export).
    if (args.has ("export-preset"))
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args.getString ("export-preset", ""));

        if (const auto error = processor.exportPreset (file); error.isNotEmpty())
        {
            std::cerr << "error: --export-preset: " << error.toStdString() << std::endl;
            return 2;
        }
    }

    // The state after the render (harness contract --state-out).
    if (args.has ("state-out"))
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args.getString ("state-out", ""));
        juce::MemoryBlock blob;
        processor.getStateInformation (blob);
        root->setProperty ("stateOut", file.replaceWithData (blob.getData(), blob.getSize()) ? file.getFullPathName() : juce::String());
    }

    root->setProperty ("activePreset", processor.getActivePresetName());
    root->setProperty ("stateNotice", processor.getStateNotice());

    // ---- Harness contract v3 (scripts/dsp-kit/harness-contract.md) ---------
    // The fields every kit script reads, beside the tool's own older ones.
    double maxStep = 0.0;
    int lastAudible = -1;

    for (int ch = 0; ch < render.getNumChannels(); ++ch)
    {
        const auto* x = render.getReadPointer (ch);

        for (int n = 0; n < totalSamples; ++n)
        {
            if (n > 0 && std::isfinite (x[n]) && std::isfinite (x[n - 1]))
                maxStep = juce::jmax (maxStep, (double) std::abs (x[n] - x[n - 1]));

            if (std::abs (x[n]) > 3.16227766e-5f)   // -90 dBFS
                lastAudible = juce::jmax (lastAudible, n);
        }
    }

    {
        auto* applied = new juce::DynamicObject();
        applied->setProperty ("seed", args.has ("seed") ? juce::var (args.getInt ("seed", 1)) : juce::var());
        applied->setProperty ("sample_rate", (int) std::lround (sampleRate));
        applied->setProperty ("block", randomBlocks ? juce::var (blockArg) : juce::var (zeroBlocks ? 0 : blockSize));
        applied->setProperty ("max_block", maxBlock);
        applied->setProperty ("seconds", fixedLength ? renderLength : (double) totalSamples / sampleRate);
        applied->setProperty ("preset", args.has ("preset") ? juce::var (args.getString ("preset", "")) : juce::var());
        applied->setProperty ("solo", args.has ("solo") ? juce::var (args.getString ("solo", "")) : juce::var());

        auto* byId = new juce::DynamicObject();
        for (auto* param : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                byId->setProperty (ranged->getParameterID(), ranged->getValue());   // normalised 0..1
        applied->setProperty ("params", juce::var (byId));

        juce::Array<juce::var> noteList;
        for (auto n : notes) noteList.add (n);
        applied->setProperty ("notes", noteList);
        applied->setProperty ("gate", holdSeconds);
        applied->setProperty ("latency_samples", processor.getLatencySamples());
        // Time from the note-off to the first sample after which everything stays below -90 dBFS (D.2.5).
        applied->setProperty ("tail_seconds", lastAudible < 0 ? 0.0 : juce::jmax (0.0, (double) (lastAudible + 1 - lastOffSample) / sampleRate));

        auto* build = new juce::DynamicObject();
        build->setProperty ("commit", BUILD_SHA);
        build->setProperty ("config", HORIZON_BUILD_CONFIG);
       #if defined (__aarch64__) || defined (__arm64__)
        build->setProperty ("arch", "arm64");
       #elif defined (__x86_64__) || defined (_M_X64)
        build->setProperty ("arch", "x86_64");
       #else
        build->setProperty ("arch", "unknown");
       #endif
        applied->setProperty ("build", juce::var (build));
        root->setProperty ("applied", juce::var (applied));
    }

    root->setProperty ("contract", 3);

    if (auto* levels = root->getProperty ("levels").getDynamicObject())
    {
        const auto db = [] (double v) { return v > 0.0 ? 20.0 * std::log10 (v) : -300.0; };
        levels->setProperty ("peak_dbfs", db (peak));
        levels->setProperty ("rms_dbfs", db (rms));
        levels->setProperty ("dc_dbfs", db (juce::jmax (std::abs (dcL), std::abs (dcR))));
        if (args.has ("true-peak"))
            levels->setProperty ("true_peak_dbtp", loudness.truePeakDb);
    }

    {
        auto* health = new juce::DynamicObject();
        health->setProperty ("nan", (double) nanCount);
        health->setProperty ("inf", (double) infCount);
        health->setProperty ("clipped_samples", (double) hotSampleCount);
        health->setProperty ("max_click_db", peak > 0.0 && maxStep > 0.0 ? 20.0 * std::log10 (maxStep / peak) : -300.0);
        root->setProperty ("health", juce::var (health));

        auto* loud = new juce::DynamicObject();
        loud->setProperty ("integrated_lufs", loudness.valid ? loudness.integratedLufs : -300.0);
        root->setProperty ("loudness", juce::var (loud));
    }

    std::cout << juce::JSON::toString (juce::var (root), true) << std::endl;

    if (stateRefused.isNotEmpty())
    {
        std::cerr << "error: --state-in " << stateRefused.toStdString() << std::endl;
        return 2;
    }

    // Harness contract: NaN or Inf in the render is exit 3 (the JSON is printed).
    return nanCount > 0 || infCount > 0 ? 3 : 0;
}

int main (int argc, char* argv[])
{
    // MessageManager must exist (and be bound to this thread) before we touch
    // any JUCE class that uses AsyncUpdater/ChangeBroadcaster/Timer - the
    // processor is a ChangeBroadcaster and calls sendChangeMessage() from
    // setCurrentProgram()/etc. We never pump the message loop (nothing here
    // needs the callback delivered), this just avoids the "no message
    // thread" assertion. It must be explicitly torn down before exit, or
    // JUCE's leak detector fires an assertion at static-teardown time.
    juce::MessageManager::getInstance();
    const auto result = runTool (argc, argv);

    // Standard JUCE shutdown order (mirrors what JUCEApplicationBase does):
    // DeletedAtShutdown singletons first (juce_events' ShutdownDetector and
    // friends register themselves this way), then the MessageManager itself.
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();

    return result;
}
