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

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

// Simple errno stub to eliminate undefined symbol dependency
extern "C" int* __errno(void) {
    static int errno_val = 0;
    return &errno_val;
}

// -----------------------------------------------------------------------------
// CONFIGURATION CONSTANTS
// -----------------------------------------------------------------------------

static const int kFftSize                = 1024;          // Fixed 1024-point FFT
// Smoothing presets for envelope following (0=fast, 3=smooth)
static const float kSmoothingFactors[]   = {0.1f, 0.3f, 0.6f, 0.85f};
static const int kNumSmoothingLevels     = sizeof(kSmoothingFactors)/sizeof(kSmoothingFactors[0]);
static const float kReferenceVoltage     = 10.0f;         // 10 V full‑scale
static const float kMinPotFreq           = 20.0f;         // 20 Hz lower limit
static const float kMaxPotFreq           = 20000.0f;      // 20 kHz upper limit

// No need for separate function pointers - we'll use the generic arm_cfft_init_f32()

// -----------------------------------------------------------------------------
// Simple table-free FFT implementation (radix-2, in-place)
// -----------------------------------------------------------------------------

// Complex number structure
struct Complex {
    float real, imag;
    Complex(float r = 0, float i = 0) : real(r), imag(i) {}
    Complex operator+(const Complex& other) const { return Complex(real + other.real, imag + other.imag); }
    Complex operator-(const Complex& other) const { return Complex(real - other.real, imag - other.imag); }
    Complex operator*(const Complex& other) const { 
        return Complex(real * other.real - imag * other.imag, real * other.imag + imag * other.real); 
    }
};

// Bit-reverse function for FFT reordering
static void bitReverse(Complex* data, int n) {
    // Validate inputs
    if (!data || n <= 0 || n > kFftSize) return;
    
    int j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            Complex temp = data[i];
            data[i] = data[j];
            data[j] = temp;
        }
    }
}

// Simple in-place radix-2 FFT with on-the-fly twiddle computation
static void simpleFFT(Complex* data, int n) {
    // Validate inputs
    if (!data || n <= 0 || n > kFftSize) return;
    
    bitReverse(data, n);
    
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * M_PI_F / len;
        Complex wlen(cosf(angle), sinf(angle));
        
        for (int i = 0; i < n; i += len) {
            Complex w(1, 0);
            for (int j = 0; j < len / 2; j++) {
                Complex u = data[i + j];
                Complex v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w = w * wlen;
            }
        }
    }
}

// Real-to-complex FFT wrapper (input: real samples, output: complex spectrum)
static void realFFT(float* realInput, Complex* complexOutput, int n) {
    // Validate inputs
    if (!realInput || !complexOutput || n <= 0 || n > kFftSize) return;
    
    // Copy real input to complex array (imaginary parts = 0)
    for (int i = 0; i < n; i++) {
        complexOutput[i] = Complex(realInput[i], 0);
    }
    
    // Perform complex FFT
    simpleFFT(complexOutput, n);
}

// -----------------------------------------------------------------------------
// DTC (D‑TCM) – real‑time / large data that benefits from fast access.
// -----------------------------------------------------------------------------
struct _SpectralEnvFollower_DTC
{
    // Input buffer for real samples
    float inputBuffer[kFftSize]     __attribute__((aligned(4)));
    
    // FFT output buffer (complex spectrum)
    Complex fftOutput[kFftSize]     __attribute__((aligned(4)));

    // Per‑bin magnitude (half‑spectrum)
    float magnitude[kFftSize/2]     __attribute__((aligned(4)));

    // Envelope followers for the three bands
    float env[3];

    // UI & control state
    int   currentSmoothingIdx; // index into kSmoothingFactors (0-3)
    int   samplesAccumulated;  // how many samples currently in inputBuffer
    float potCentres[3];       // Hz – centre freq for each band (updated by UI)
    float potCentreBins[3];    // cached centre‑bin indices (float) for speed
    float yScale;              // vertical scale in UI (multiplier)
    bool  displayInitialized;  // flag to track per-instance display initialization
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
    kParamBandAFreq, kParamBandBFreq, kParamBandCFreq,
};

static _NT_parameter gParameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Audio In", 1, 1)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band A CV", 1, 13)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band B CV", 1, 14)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band C CV", 1, 15)
    { .name = "Band A Freq", .min = 20, .max = 20000, .def = 100, .unit = kNT_unitHz, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Band B Freq", .min = 20, .max = 20000, .def = 1000, .unit = kNT_unitHz, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Band C Freq", .min = 20, .max = 20000, .def = 8000, .unit = kNT_unitHz, .scaling = kNT_scalingNone, .enumStrings = nullptr },
};

// Parameter pages
static const uint8_t routingPage[] = {
    kParamInput,
    kParamCvOut1, kParamCvOut1Mode,
    kParamCvOut2, kParamCvOut2Mode,
    kParamCvOut3, kParamCvOut3Mode,
};

static const uint8_t spectralPage[] = {
    kParamBandAFreq, kParamBandBFreq, kParamBandCFreq,
};

static const _NT_parameterPage gPages[] = {
    {.name = "Routing", .numParams = static_cast<uint8_t>(ARRAY_SIZE(routingPage)), .params = routingPage},
    {.name = "Spectral", .numParams = static_cast<uint8_t>(ARRAY_SIZE(spectralPage)), .params = spectralPage},
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

// Helper – map frequency → 0‑1 pot value (inverse of potToFreq)
static inline float freqToPot(float freq)
{
    const float minLog = logf(kMinPotFreq);
    const float maxLog = logf(kMaxPotFreq);
    return (logf(freq) - minLog) / (maxLog - minLog);
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
    
    // Initialize arrays manually since memset can't be used with non-trivial types
    for (int i = 0; i < kFftSize; i++) {
        dtc->inputBuffer[i] = 0.0f;
        dtc->fftOutput[i] = Complex(0, 0);
    }
    for (int i = 0; i < kFftSize/2; i++) {
        dtc->magnitude[i] = 0.0f;
    }
    for (int i = 0; i < 3; i++) {
        dtc->env[i] = 0.0f;
    }
    
    // Initialize frequency values to zero - will be set by parameterChanged() calls
    for (int i = 0; i < 3; i++) {
        dtc->potCentres[i] = 0.0f;
        dtc->potCentreBins[i] = 0.0f;
    }
    dtc->currentSmoothingIdx = 0;
    dtc->samplesAccumulated = 0;

    dtc->currentSmoothingIdx = 1;    // default to moderate smoothing
    dtc->yScale              = 1.0f;
    dtc->displayInitialized  = false; // initialize per-instance display flag

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
// parameterChanged – handle frequency parameter changes.
// -----------------------------------------------------------------------------
static void parameterChanged(_NT_algorithm *base, int paramIndex) 
{
    auto *self = (_SpectralEnvFollower *)base;
    auto *d = self->dtc;
    
    // Use actual sample rate if available, otherwise assume 48kHz
    float sampleRate = (NT_globals.sampleRate > 0) ? (float)NT_globals.sampleRate : 48000.0f;
    float binHz = sampleRate / (float)kFftSize;
    
    // Handle frequency parameter changes
    if (paramIndex == kParamBandAFreq) {
        d->potCentres[0] = (float)self->v[kParamBandAFreq];
        d->potCentreBins[0] = d->potCentres[0] / binHz;
    }
    else if (paramIndex == kParamBandBFreq) {
        d->potCentres[1] = (float)self->v[kParamBandBFreq];
        d->potCentreBins[1] = d->potCentres[1] / binHz;
    }
    else if (paramIndex == kParamBandCFreq) {
        d->potCentres[2] = (float)self->v[kParamBandCFreq];
        d->potCentreBins[2] = d->potCentres[2] / binHz;
    }
}

// -----------------------------------------------------------------------------
// step – DSP core.
// -----------------------------------------------------------------------------
static void step(_NT_algorithm *base, float *bus, int framesBy4)
{
    if (!base || !bus || framesBy4 <= 0) return;
    
    auto *self = (_SpectralEnvFollower *)base;
    if (!self || !self->dtc || !self->v) return;
    
    auto *d = self->dtc;
    
    const int numFrames = framesBy4 * 4;
    
    // Validate input parameter
    int inputBus = self->v[kParamInput];
    if (inputBus < 1 || inputBus > 28) return;
    const float *inBuf = bus + (inputBus - 1) * numFrames;
    
    // Get output bus pointers
    float *outBuf[3] = {nullptr, nullptr, nullptr};
    bool outModeAdd[3] = {false, false, false};
    
    for (int b = 0; b < 3; b++) {
        int paramIdx = kParamCvOut1 + b * 2;
        int modeIdx = paramIdx + 1;
        
        int outputBus = self->v[paramIdx];
        if (outputBus >= 1 && outputBus <= 28) {
            outBuf[b] = bus + (outputBus - 1) * numFrames;
            outModeAdd[b] = (bool)self->v[modeIdx];
        }
    }

    // -----------------------------------------------------------------
    // Accumulate samples until we have fftSize, then run analysis.
    // -----------------------------------------------------------------
    int idx = d->samplesAccumulated;
    if (idx < 0 || idx >= kFftSize) {
        idx = 0;
        d->samplesAccumulated = 0;
    }
    
    for (int n = 0; n < numFrames; ++n)
    {
        if (idx < kFftSize)
        {
            d->inputBuffer[idx++] = inBuf[n];
        }
        if (idx == kFftSize)
        {
            // Apply Hann window to input
            for (int i = 0; i < kFftSize; ++i)
            {
                float w = 0.5f * (1.0f - cosf((2.0f * M_PI_F * i) / (kFftSize - 1)));
                d->inputBuffer[i] *= w;
            }
            
            // Perform table-free FFT (real input -> complex output)
            realFFT(d->inputBuffer, d->fftOutput, kFftSize);

            // Calculate magnitudes from complex FFT output
            const int half = kFftSize / 2;
            for (int k = 0; k < half; ++k)
            {
                float re = d->fftOutput[k].real;
                float im = d->fftOutput[k].imag;
                d->magnitude[k] = sqrtf(re * re + im * im);
            }

            // Update envelopes for each band.
            for (int b=0;b<3;++b)
            {
                // Convert centre freq (Hz) → bin
                float centreBin = d->potCentreBins[b];
                int c = (int)roundf(centreBin);
                int lo = c - 10; if (lo < 0) lo = 0;
                int hi = c + 10; if (hi >= half) hi = half-1;

                float sum = 0.0f;
                for (int k = lo; k <= hi; ++k) {
                    sum += d->magnitude[k];
                }
                float env = sum / (float)(hi - lo + 1);

                // Apply smoothing
                const float smoothing = kSmoothingFactors[d->currentSmoothingIdx];
                
                if (env > d->env[b])
                    d->env[b] = env; // Fast attack
                else
                    d->env[b] = d->env[b] * smoothing + env * (1.0f - smoothing); // Configurable decay
            }

            // Reset accumulator.
            idx = 0;
        }
    }
    d->samplesAccumulated = idx;

    // -----------------------------------------------------------------
    // Write CV outputs for this block – hold last envelope value.
    // -----------------------------------------------------------------
    for (int b = 0; b < 3; ++b)
    {
        if (outBuf[b] != nullptr) {
            float v = envToVolts(d->env[b]);
            
            if (outModeAdd[b]) {
                for (int n = 0; n < numFrames; ++n) {
                    outBuf[b][n] += v;
                }
            } else {
                for (int n = 0; n < numFrames; ++n) {
                    outBuf[b][n] = v;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// draw – custom OLED rendering (128×64) – returns true to suppress header.
// -----------------------------------------------------------------------------
static bool draw(_NT_algorithm *base)
{
    auto *self = (_SpectralEnvFollower *)base;
    
    // Very basic safety check - test if we can even access the structure
    if (!self) {
        return false;
    }
    
    auto *d = self->dtc;
    if (!d) {
        return false;
    }
    
    // Initialize bin positions on first draw (per-instance)
    if (!d->displayInitialized) {
        // Use actual sample rate if available, otherwise assume 48kHz
        float sampleRate = (NT_globals.sampleRate > 0) ? (float)NT_globals.sampleRate : 48000.0f;
        float binHz = sampleRate / (float)kFftSize;
        
        // Recalculate bin positions from current frequency values (set by parameterChanged)
        for (int i = 0; i < 3; i++) {
            if (d->potCentres[i] > 0.0f) {
                d->potCentreBins[i] = d->potCentres[i] / binHz;
                // Ensure bin positions are valid and clipped to reasonable bounds
                if (d->potCentreBins[i] < 0.0f) d->potCentreBins[i] = 0.0f;
                if (d->potCentreBins[i] >= (float)(kFftSize/2)) d->potCentreBins[i] = (float)(kFftSize/2 - 1);
            }
        }
        d->displayInitialized = true;
    }

    // Draw spectrum visualization
    const int width = 256;
    const int height = 64;
    const int half = kFftSize / 2;  // 512 bins
    
    // Display first 256 bins (covers most useful frequency range)
    for (int x = 0; x < width && x < half; ++x) {
        // Get magnitude for this bin
        float mag = d->magnitude[x];
        
        // Apply logarithmic scaling for better visualization
        float logMag = (mag > 0.001f) ? logf(mag + 1.0f) : 0.0f;
        
        // Scale to screen height - increase sensitivity to see weaker signals
        int barHeight = (int)(logMag * d->yScale * 20.0f);  // Increased from 8.0f to 20.0f
        if (barHeight < 0) barHeight = 0;
        if (barHeight > height) barHeight = height;

        // Draw spectrum bar with coordinate validation
        if (barHeight > 0 && x >= 0 && x < width) {
            int y1 = height - barHeight;
            int y2 = height - 1;
            // Ensure y coordinates are reasonable (clipping will handle edge cases)
            if (y1 < 0) y1 = 0;
            if (y2 >= height) y2 = height - 1;
            if (y1 <= y2) {  // Only draw if we have a valid line
                NT_drawShapeI(kNT_line, x, y1, x, y2, 7);  // Use color 7 for spectrum
            }
        }
    }

    // Draw band center markers based on current parameter values
    float sampleRate = (NT_globals.sampleRate > 0) ? (float)NT_globals.sampleRate : 48000.0f;
    float binHz = sampleRate / (float)kFftSize;
    
    for (int b = 0; b < 3; ++b) {
        // Get frequency directly from parameter values
        int paramIndex = kParamBandAFreq + b;
        float frequency = (float)self->v[paramIndex];
        
        // Only draw marker if we have a valid frequency (> 0)
        if (frequency > 0.0f) {
            float centerBin = frequency / binHz;
            
            // Ensure centerBin is within display range
            if (centerBin >= 1.0f && centerBin < (float)(width - 1)) {
                int centerX = (int)roundf(centerBin);
                
                // Ensure centerX is within reasonable bounds
                if (centerX >= 0 && centerX < width) {
                    // Use distinct colors: 15 (white), 11, 3
                    int color = (b == 0) ? 15 : ((b == 1) ? 11 : 3);
                    
                    // Draw vertical line for frequency marker
                    NT_drawShapeI(kNT_line, centerX, 0, centerX, height - 1, color);
                    
                    // Draw horizontal line at top for visibility
                    if (centerX >= 2 && centerX <= width - 3) {
                        NT_drawShapeI(kNT_line, centerX - 2, 0, centerX + 2, 0, color);
                        NT_drawShapeI(kNT_line, centerX - 2, 1, centerX + 2, 1, color);
                    }
                }
            }
        }
    }

    return true;  // Suppress header to use full screen
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

    // Handle pot changes by updating parameters through proper API
    // Handle left pot (Band A)
    if (ui.controls & kNT_potL)
    {
        float frequency = potToFreq(ui.pots[0]);
        int16_t freqValue = (int16_t)roundf(frequency);
        NT_setParameterFromUi(NT_algorithmIndex(self), kParamBandAFreq + NT_parameterOffset(), freqValue);
    }
    
    // Handle center pot (Band B)
    if (ui.controls & kNT_potC)
    {
        float frequency = potToFreq(ui.pots[1]);
        int16_t freqValue = (int16_t)roundf(frequency);
        NT_setParameterFromUi(NT_algorithmIndex(self), kParamBandBFreq + NT_parameterOffset(), freqValue);
    }
    
    // Handle right pot (Band C)
    if (ui.controls & kNT_potR)
    {
        float frequency = potToFreq(ui.pots[2]);
        int16_t freqValue = (int16_t)roundf(frequency);
        NT_setParameterFromUi(NT_algorithmIndex(self), kParamBandCFreq + NT_parameterOffset(), freqValue);
    }

    // Encoder L – vertical scale (±1 detent).
    if (ui.encoders[0] != 0)
    {
        d->yScale *= (ui.encoders[0] > 0) ? 2.0f : 0.5f;
        if (d->yScale < 0.125f) d->yScale = 0.125f;
        if (d->yScale > 8.0f)   d->yScale = 8.0f;
    }

    // Encoder R – Envelope smoothing adjustment.
    if (ui.encoders[1] != 0)
    {
        int dir = (ui.encoders[1] > 0) ? 1 : -1;
        d->currentSmoothingIdx = (d->currentSmoothingIdx + dir + kNumSmoothingLevels) % kNumSmoothingLevels;
    }
}

static void setupUi(_NT_algorithm *base, _NT_float3 &pots) 
{
    auto *self = (_SpectralEnvFollower *)base;
    
    // Set pot positions to reflect current frequency parameter values
    // Convert Hz parameter values to normalized 0-1 pot positions
    pots[0] = freqToPot((float)self->v[kParamBandAFreq]);
    pots[1] = freqToPot((float)self->v[kParamBandBFreq]);
    pots[2] = freqToPot((float)self->v[kParamBandCFreq]);
    
    // Clamp to valid range [0, 1]
    for (int b = 0; b < 3; ++b) {
        if (pots[b] < 0.0f) pots[b] = 0.0f;
        if (pots[b] > 1.0f) pots[b] = 1.0f;
    }
}

// -----------------------------------------------------------------------------
// Factory descriptor.
// -----------------------------------------------------------------------------
static const _NT_factory gFactory =
{
    .guid                       = NT_MULTICHAR('T','h','S','f'),
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

