/*
 * Spectral Envelope Follower (3‑Band)
 * -----------------------------------
 *
 * ‑ Uses a fast ARM‑optimised FFT (e.g. KissFFT or CMSIS‑DSP – compiled
 *   separately and **statically linked**).
 * ‑ Analyses an incoming audio signal and tracks the energy in three
 *   user‑selectable frequency bands.
 * ‑ The energy of each band is output on three CV outputs (0‑10 V).
 * ‑ The three pots choose the *centre* frequency of each band.  Bands may
 *   overlap freely.
 * ‑ Encoder L scales the Y‑axis of the spectrum view (×½ / ×2 per detent).
 * ‑ Encoder R steps through FFT sizes 256 → 512 → 1024 → 2048 (and wraps).
 * ‑ The custom UI draws a bar chart of the current FFT magnitudes with
 *   bold markers at the three band centres.
 *
 * 2025 © Thorinside / Example code produced by ChatGPT‑o3.
 * Released under the MIT Licence.
 */

#include <math.h>
#include <string.h>
#include <new>
#include <distingnt/api.h>
#include <arm_math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Simple errno stub to eliminate undefined symbol dependency
extern "C" int* __errno(void) {
    static int errno_val = 0;
    return &errno_val;
}

// -----------------------------------------------------------------------------
// CONFIGURATION CONSTANTS
// -----------------------------------------------------------------------------

static const int kMaxFftSize             = 2048;          // Largest FFT we ever allocate
static const int kFftSizes[]             = {256, 512, 1024, 2048};
static const int kNumFftSizes            = sizeof(kFftSizes)/sizeof(kFftSizes[0]);
static const float kReferenceVoltage     = 10.0f;         // 10 V full‑scale
static const float kEnvelopeDecayCoeff   = 0.96f;         // Simple exponential decay for envelope smoothing
static const float kMinPotFreq           = 20.0f;         // 20 Hz lower limit
static const float kMaxPotFreq           = 20000.0f;      // 20 kHz upper limit

// No need for separate function pointers - we'll use the generic arm_cfft_init_f32()

// -----------------------------------------------------------------------------
// DTC (D‑TCM) – real‑time / large data that benefits from fast access.
// -----------------------------------------------------------------------------
struct _SpectralEnvFollower_DTC
{
    // FFT buffers (size == currentFftSize)
    float timeBuffer[kMaxFftSize]   __attribute__((aligned(4)));
    float workBuffer[kMaxFftSize]   __attribute__((aligned(4))); // complex out

    // Per‑bin magnitude (half‑spectrum)
    float magnitude[kMaxFftSize/2]  __attribute__((aligned(4)));

    // FFT instances (one for each supported size, initialized on demand)
    arm_cfft_instance_f32 fft_instances[kNumFftSizes] __attribute__((aligned(4)));
    bool fft_initialized[kNumFftSizes]; // track which ones are initialized

    // Envelope followers for the three bands
    float env[3];

    // UI & control state
    int   currentFftSizeIdx;   // index into kFftSizes
    int   samplesAccumulated;  // how many samples currently in timeBuffer
    float potCentres[3];       // Hz – centre freq for each band (updated by UI)
    float potCentreBins[3];    // cached centre‑bin indices (float) for speed
    float yScale;              // vertical scale in UI (multiplier)
};

// -----------------------------------------------------------------------------
// Algorithm object (lives in SRAM).
// -----------------------------------------------------------------------------
struct _SpectralEnvFollower : public _NT_algorithm
{
    _SpectralEnvFollower_DTC *dtc;

    _SpectralEnvFollower(_SpectralEnvFollower_DTC *d) : dtc(d) {}
};

// -----------------------------------------------------------------------------
// Parameter list
// -----------------------------------------------------------------------------
enum
{
    kParamInput = 0,
    kParamCvOut1, kParamCvOut1Mode,
    kParamCvOut2, kParamCvOut2Mode,
    kParamCvOut3, kParamCvOut3Mode,
};

static _NT_parameter gParameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Audio In", 1, 1)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band A CV", 1, 13)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band B CV", 1, 14)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band C CV", 1, 15)
};

// Single routing page – pots/encoders handled in custom UI.
static const uint8_t routingPage[] = {
    kParamInput,
    kParamCvOut1, kParamCvOut1Mode,
    kParamCvOut2, kParamCvOut2Mode,
    kParamCvOut3, kParamCvOut3Mode,
};

static const _NT_parameterPage gPages[] = {
    {.name = "Routing", .numParams = (uint8_t)ARRAY_SIZE(routingPage), .params = routingPage},
};

static const _NT_parameterPages gParameterPages = {
    .numPages = ARRAY_SIZE(gPages),
    .pages    = gPages,
};

// -----------------------------------------------------------------------------
// Helper – map 0‑1 pot value → frequency (log scale).
// -----------------------------------------------------------------------------
static inline float potToFreq(float norm)
{
    // Map linearly in log domain for perceptual spacing.
    const float minLog = logf(kMinPotFreq);
    const float maxLog = logf(kMaxPotFreq);
    float f = expf(minLog + norm * (maxLog - minLog));
    return f;
}

// -----------------------------------------------------------------------------
// calculateRequirements – called by host while browsing/adding algorithm.
// -----------------------------------------------------------------------------
static void calculateRequirements(_NT_algorithmRequirements &req, const int32_t *)
{
    req.numParameters = ARRAY_SIZE(gParameters);
    req.sram = sizeof(_SpectralEnvFollower);
    req.dram = 0;
    req.dtc  = sizeof(_SpectralEnvFollower_DTC);
    req.itc  = 0;
}

// -----------------------------------------------------------------------------
// construct – create a new algorithm instance.
// -----------------------------------------------------------------------------
static _NT_algorithm *construct(const _NT_algorithmMemoryPtrs &mem,
                                const _NT_algorithmRequirements &,
                                const int32_t *)
{
    auto *dtc = new (mem.dtc) _SpectralEnvFollower_DTC();
    memset(dtc, 0, sizeof(*dtc));

    dtc->currentFftSizeIdx = 1;   // default 512‑pt FFT
    dtc->yScale            = 1.0f;

    // Initialize FFT tracking (instances will be initialized on first use)
    for (int i = 0; i < kNumFftSizes; ++i) {
        dtc->fft_initialized[i] = false;
    }

    auto *alg = new (mem.sram) _SpectralEnvFollower(dtc);
    alg->parameters      = gParameters;
    alg->parameterPages  = &gParameterPages;

    // Initialise envelopes to 0 V.
    for (int i=0;i<3;++i) dtc->env[i] = 0.0f;

    return alg;
}

// -----------------------------------------------------------------------------
// Utility – convert band envelope level → voltage (0‑10 V).
// -----------------------------------------------------------------------------
static inline float envToVolts(float env)
{
    // Clamp for safety.
    if (env < 0.0f) env = 0.0f;
    return env * kReferenceVoltage;
}

// -----------------------------------------------------------------------------
// parameterChanged – none required presently (pots/encoders handled in UI).
// -----------------------------------------------------------------------------
static void parameterChanged(_NT_algorithm *, int) {}

// -----------------------------------------------------------------------------
// step – DSP core.
// -----------------------------------------------------------------------------
static void step(_NT_algorithm *base, float *bus, int framesBy4)
{
    auto *self = (_SpectralEnvFollower *)base;
    auto *d    = self->dtc;
    const int  Ntotal = framesBy4 * 4;

    // Resolve pointers.
    const int  inputBusIdx   = self->v[kParamInput] - 1;
    float     *inBuf         = bus + inputBusIdx * Ntotal;

    // Output busses (replace or add handled later).
    const int  outIdx[3]     = { self->v[kParamCvOut1] - 1,
                                 self->v[kParamCvOut2] - 1,
                                 self->v[kParamCvOut3] - 1 };
    const bool outModeAdd[3] = { (bool)self->v[kParamCvOut1Mode],
                                 (bool)self->v[kParamCvOut2Mode],
                                 (bool)self->v[kParamCvOut3Mode] };

    const int  fftSize       = kFftSizes[d->currentFftSizeIdx];

    // -----------------------------------------------------------------
    // Accumulate samples until we have fftSize, then run analysis.
    // -----------------------------------------------------------------
    int idx = d->samplesAccumulated;
    for (int n=0; n<Ntotal; ++n)
    {
        if (idx < fftSize)
        {
            d->timeBuffer[idx++] = inBuf[n];
        }
        if (idx == fftSize)
        {
            // Windowing (Hann) + FFT.
            for (int i=0;i<fftSize;++i)
            {
                float w = 0.5f * (1.0f - cosf((2.0f * M_PI * i) / (fftSize-1)));
                d->workBuffer[i] = d->timeBuffer[i] * w;
            }
            // Copy windowed data to complex format (real parts only, imaginary = 0)
            for (int i = 0; i < fftSize; i++) {
                d->timeBuffer[2*i] = d->workBuffer[i];     // Real part
                d->timeBuffer[2*i + 1] = 0.0f;             // Imaginary part = 0
            }
            
            // Initialize FFT instance if needed
            if (!d->fft_initialized[d->currentFftSizeIdx]) {
                arm_status status = arm_cfft_init_f32(&d->fft_instances[d->currentFftSizeIdx], 
                                                     (uint16_t)fftSize);
                if (status == ARM_MATH_SUCCESS) {
                    d->fft_initialized[d->currentFftSizeIdx] = true;
                }
            }
            
            // Perform FFT using CMSIS-DSP (only if initialization succeeded)
            if (d->fft_initialized[d->currentFftSizeIdx]) {
                const arm_cfft_instance_f32 *fft_inst = &d->fft_instances[d->currentFftSizeIdx];
                arm_cfft_f32(fft_inst, d->timeBuffer, 0, 1); // Forward FFT, bit-reverse
            }

            // Magnitudes (only first half – real spectrum).
            const int half = fftSize / 2;
            for (int k=0;k<half;++k)
            {
                float re = d->timeBuffer[2*k];     // FFT output is in timeBuffer
                float im = d->timeBuffer[2*k + 1];
                d->magnitude[k] = sqrtf(re*re + im*im);
            }

            // Update envelopes for each band.
            for (int b=0;b<3;++b)
            {
                // Convert centre freq (Hz) → bin.
                float centreBin = d->potCentreBins[b];
                // Integrate ±3 bins.
                int c = (int)roundf(centreBin);
                int lo = c - 3; if (lo < 0) lo = 0;
                int hi = c + 3; if (hi >= half) hi = half-1;

                float sum = 0.0f;
                for (int k=lo;k<=hi;++k) sum += d->magnitude[k];
                float env = sum / (float)(hi-lo+1); // mean magnitude

                // Simple exponential smoothing – attack instant, decay slow.
                if (env > d->env[b])
                    d->env[b] = env;
                else
                    d->env[b] = d->env[b] * kEnvelopeDecayCoeff + env * (1.0f - kEnvelopeDecayCoeff);
            }

            // Reset accumulator.
            idx = 0;
        }
    }
    d->samplesAccumulated = idx;

    // -----------------------------------------------------------------
    // Write CV outputs for this block – hold last envelope value.
    // -----------------------------------------------------------------
    for (int b=0;b<3;++b)
    {
        if (outIdx[b] < 0) continue; // not connected
        float *out = bus + outIdx[b] * Ntotal;
        float v    = envToVolts(d->env[b]);
        if (outModeAdd[b])
        {
            for (int n=0;n<Ntotal;++n) out[n] += v;
        }
        else
        {
            for (int n=0;n<Ntotal;++n) out[n]  = v;
        }
    }
}

// -----------------------------------------------------------------------------
// draw – custom OLED rendering (128×64) – returns true to suppress header.
// -----------------------------------------------------------------------------
static bool draw(_NT_algorithm *base)
{
    auto *self = (_SpectralEnvFollower *)base;
    auto *d    = self->dtc;

    const int width  = 128; // OLED controller width
    const int height = 64;

    // Clear screen.
    memset(NT_screen, 0, sizeof(NT_screen));

    // Draw spectrum (every 2 px → 64 bars).
    const int fftSize    = kFftSizes[d->currentFftSizeIdx];
    const int half       = fftSize / 2;
    const int bars       = width / 2;
    const int binsPerBar = half / bars;

    for (int bar=0; bar<bars; ++bar)
    {
        // Average magnitude for this bar.
        float mag = 0.0f;
        int   startBin = bar * binsPerBar;
        int   endBin   = startBin + binsPerBar - 1;
        if (endBin >= half) endBin = half-1;
        for (int k=startBin; k<=endBin; ++k) mag += d->magnitude[k];
        mag /= (float)(endBin-startBin+1);

        // Scale and clamp to screen height.
        int barHeight = (int)(mag * d->yScale * 0.004f * height); // empirical scaling
        if (barHeight > height) barHeight = height;

        // Draw vertical bar (filled).
        int x = bar * 2;
        for (int y=0; y<barHeight; ++y)
        {
            NT_screen[(height-1-y)*128 + x] = 0xFF;
            NT_screen[(height-1-y)*128 + x+1] = 0xFF;
        }
    }

    // Draw centre lines for each band.
    for (int b=0;b<3;++b)
    {
        float bin = d->potCentreBins[b];
        int   bar = (int)(bin / binsPerBar);
        if (bar < 0) bar = 0;
        if (bar >= bars) bar = bars-1;
        int x = bar * 2;
        // Vertical marker.
        for (int y=0; y<height; ++y)
            NT_screen[y*128 + x] = (b==0?0xA0:(b==1?0x60:0x40));
        // Big digit at bottom.
        char txt[2] = {(char)('A'+b), 0};
        NT_drawText(x, height-10, txt, kNT_textLarge, kNT_textCentre);
    }

    return true; // suppress default header line
}

// -----------------------------------------------------------------------------
// hasCustomUi / customUi – pot & encoder handling.
// -----------------------------------------------------------------------------
static uint32_t hasCustomUi(_NT_algorithm *)
{
    return kNT_potL | kNT_potC | kNT_potR | kNT_encoderL | kNT_encoderR;
}

static void customUi(_NT_algorithm *base, const _NT_uiData &ui)
{
    auto *self = (_SpectralEnvFollower *)base;
    auto *d    = self->dtc;

    // Update band centres from pots.
    float pots[3] = { ui.pots[0], ui.pots[1], ui.pots[2] };
    for (int b=0;b<3;++b)
    {
        d->potCentres[b]     = potToFreq(pots[b]);
        // Cache bin index.
        int   fftSize = kFftSizes[d->currentFftSizeIdx];
        float binHz   = (float)NT_globals.sampleRate / (float)fftSize;
        d->potCentreBins[b] = d->potCentres[b] / binHz;
    }

    // Encoder L – vertical scale (±1 detent).
    if (ui.encoders[0] != 0)
    {
        d->yScale *= (ui.encoders[0] > 0) ? 2.0f : 0.5f;
        if (d->yScale < 0.125f) d->yScale = 0.125f;
        if (d->yScale > 8.0f)   d->yScale = 8.0f;
    }

    // Encoder R – FFT size cycling.
    if (ui.encoders[1] != 0)
    {
        int dir = (ui.encoders[1] > 0) ? 1 : -1;
        d->currentFftSizeIdx = (d->currentFftSizeIdx + dir + kNumFftSizes) % kNumFftSizes;
        // Reset accumulation so we don't mix sizes.
        d->samplesAccumulated = 0;
        // Recompute cached bin positions.
        for (int b=0;b<3;++b)
        {
            float binHz = (float)NT_globals.sampleRate / (float)kFftSizes[d->currentFftSizeIdx];
            d->potCentreBins[b] = d->potCentres[b] / binHz;
        }
        // Note: FFT instance for new size will be initialized on next use
    }
}

static void setupUi(_NT_algorithm *, _NT_float3 &) {}

// -----------------------------------------------------------------------------
// Factory descriptor.
// -----------------------------------------------------------------------------
static const _NT_factory gFactory =
{
    .guid                       = NT_MULTICHAR('S','p','e','3'),
    .name                       = "SpecEnv 3‑Band",
    .description                = "Spectral envelope follower with three CV bands and live FFT display",
    .numSpecifications          = 0,
    .specifications             = nullptr,
    .calculateStaticRequirements= nullptr,
    .initialise                 = nullptr,
    .calculateRequirements      = calculateRequirements,
    .construct                  = construct,
    .parameterChanged           = parameterChanged,
    .step                       = step,
    .draw                       = draw,
    .midiRealtime               = nullptr,
    .midiMessage                = nullptr,
    .tags                       = 0,
    .hasCustomUi                = hasCustomUi,
    .customUi                   = customUi,
    .setupUi                    = setupUi,
    .serialise                  = nullptr,
    .deserialise                = nullptr,
    .midiSysEx                  = nullptr,
};

// -----------------------------------------------------------------------------
// pluginEntry – required export.
// -----------------------------------------------------------------------------
extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t)
{
    switch (selector)
    {
        case kNT_selector_version:       return (uintptr_t)kNT_apiVersionCurrent;
        case kNT_selector_numFactories:  return (uintptr_t)1;
        case kNT_selector_factoryInfo:   return (uintptr_t)&gFactory;
        default:                         return 0;
    }
}

