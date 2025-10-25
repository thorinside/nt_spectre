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

static const int kFftSize                = 512;           // Fixed 512-point FFT
static const int kFftRateHz              = 60;            // FFT update rate (Hz)
static const float kMinAttackMs          = 1.0f;          // 1 ms minimum attack
static const float kMaxAttackMs          = 1000.0f;       // 1 second maximum attack
static const float kMinReleaseMs         = 10.0f;         // 10 ms minimum release
static const float kMaxReleaseMs         = 5000.0f;       // 5 seconds maximum release
static const float kReferenceVoltage     = 10.0f;         // 10 V full‑scale
static const float kMinPotFreq           = 20.0f;         // 20 Hz lower limit
static const float kMaxPotFreq           = 20000.0f;      // 20 kHz upper limit

// Precomputed factors for the 512-sample Hann window used before the FFT.
static const float kHannWindowSum        = 0.5f * ((float)kFftSize - 1.0f);   // Σ Hann[n]
static const float kHannWindowRmsGain    = 0.6117741f;                        // √(Σ Hann[n]^2 / N)
static const float kFftRmsNormalization  = 1.0f / ((float)kFftSize * kHannWindowRmsGain);
static const float kPeakNormPositive     = 2.0f / kHannWindowSum;             // For mirrored bins
static const float kPeakNormEdge         = 1.0f / kHannWindowSum;             // For DC / Nyquist bins
static const float kSqrtTwo              = 1.41421356f;

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
    // Input buffer for real samples (circular buffer)
    float inputBuffer[kFftSize]     __attribute__((aligned(4)));
    
    // Temporary buffer for FFT processing
    float tempBuffer[kFftSize]      __attribute__((aligned(4)));
    
    // FFT output buffer (complex spectrum)
    Complex fftOutput[kFftSize]     __attribute__((aligned(4)));

    // Per‑bin magnitude (half‑spectrum)
    float magnitude[kFftSize/2]     __attribute__((aligned(4)));

    // Envelope followers for the three bands
    float env[3];

    // Envelope follower state
    float attackCoeff;         // calculated from attack time parameter
    float releaseCoeff;        // calculated from release time parameter

    // UI & control state
    int   samplesAccumulated;  // how many samples currently in inputBuffer
    int   samplesSinceLastFFT; // counter for FFT rate limiting
    float potCentres[3];       // Hz – centre freq for each band (updated by UI)
    float potCentreBins[3];    // cached centre‑bin indices (float) for speed
    float bandwidthOctaves;    // bandwidth in octaves (e.g., 0.333 for 1/3 octave)
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
    kParamBandwidth,
    kParamAttackTime,
    kParamReleaseTime,
    kParamDetectionMode,
};

static const char* detectionModeStrings[] = {"RMS", "Peak", nullptr};

static _NT_parameter gParameters[] = {
    NT_PARAMETER_AUDIO_INPUT("Audio In", 1, 1)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band A CV", 1, 13)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band B CV", 1, 14)
    NT_PARAMETER_CV_OUTPUT_WITH_MODE("Band C CV", 1, 15)
    { .name = "Band A Freq", .min = 20, .max = 20000, .def = 100, .unit = kNT_unitHz, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Band B Freq", .min = 20, .max = 20000, .def = 1000, .unit = kNT_unitHz, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Band C Freq", .min = 20, .max = 20000, .def = 8000, .unit = kNT_unitHz, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Bandwidth", .min = 10, .max = 200, .def = 33, .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Attack", .min = 1, .max = 1000, .def = 10, .unit = kNT_unitMs, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Release", .min = 10, .max = 5000, .def = 100, .unit = kNT_unitMs, .scaling = kNT_scalingNone, .enumStrings = nullptr },
    { .name = "Detection", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = kNT_scalingNone, .enumStrings = detectionModeStrings },
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

static const uint8_t envelopePage[] = {
    kParamBandwidth, kParamAttackTime, kParamReleaseTime, kParamDetectionMode,
};

static const _NT_parameterPage gPages[] = {
    {.name = "Routing", .numParams = static_cast<uint8_t>(ARRAY_SIZE(routingPage)), .params = routingPage},
    {.name = "Spectral", .numParams = static_cast<uint8_t>(ARRAY_SIZE(spectralPage)), .params = spectralPage},
    {.name = "Envelope", .numParams = static_cast<uint8_t>(ARRAY_SIZE(envelopePage)), .params = envelopePage},
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
        dtc->tempBuffer[i] = 0.0f;
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

    // Initialize envelope follower coefficients - will be updated by parameterChanged()
    // Default to 10ms attack, 100ms release at 60Hz update rate
    // 10ms * 60Hz = 0.6 updates, 100ms * 60Hz = 6 updates
    dtc->attackCoeff = 1.0f - expf(-1.0f / 0.6f);   // ~0.81 (fast attack)
    dtc->releaseCoeff = 1.0f - expf(-1.0f / 6.0f);  // ~0.15 (moderate release)
    dtc->bandwidthOctaves = 0.333f;                 // default 1/3 octave

    dtc->samplesAccumulated = 0;
    dtc->samplesSinceLastFFT = 0;    // initialize FFT rate limiter
    dtc->yScale              = 1.0f;
    dtc->displayInitialized  = false; // initialize per-instance display flag

    auto *alg = new (mem.sram) _SpectralEnvFollower(dtc);
    alg->parameters      = gParameters;
    alg->parameterPages  = &gParameterPages;

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
// parameterChanged – handle parameter changes.
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
    else if (paramIndex == kParamBandwidth) {
        // Bandwidth parameter is in percent (10-200), convert to octaves
        // 33% = 1/3 octave, 100% = 1 octave, 200% = 2 octaves
        float percent = (float)self->v[kParamBandwidth];
        d->bandwidthOctaves = percent / 100.0f;
    }
    else if (paramIndex == kParamAttackTime) {
        // Convert attack time (ms) to coefficient for exponential smoothing
        // NOTE: Envelope is updated at FFT rate (kFftRateHz), not audio rate
        float attackMs = (float)self->v[kParamAttackTime];
        // Calculate coefficient: 1 - exp(-1 / (time_constant_in_updates))
        // Time constant = time to reach ~63% of target
        float attackUpdates = (attackMs / 1000.0f) * (float)kFftRateHz;
        d->attackCoeff = 1.0f - expf(-1.0f / attackUpdates);
    }
    else if (paramIndex == kParamReleaseTime) {
        // Convert release time (ms) to coefficient
        // NOTE: Envelope is updated at FFT rate (kFftRateHz), not audio rate
        float releaseMs = (float)self->v[kParamReleaseTime];
        float releaseUpdates = (releaseMs / 1000.0f) * (float)kFftRateHz;
        d->releaseCoeff = 1.0f - expf(-1.0f / releaseUpdates);
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
    
    // Calculate FFT interval based on sample rate
    float sampleRate = (NT_globals.sampleRate > 0) ? (float)NT_globals.sampleRate : 48000.0f;
    int fftInterval = (int)(sampleRate / (float)kFftRateHz);  // samples between FFTs
    
    for (int n = 0; n < numFrames; ++n)
    {
        // Always store samples in circular buffer
        d->inputBuffer[idx] = inBuf[n];
        idx = (idx + 1) % kFftSize;  // Circular buffer
        
        d->samplesSinceLastFFT++;
        
        // Process FFT at the specified rate, but ensure first FFT runs once buffer is full
        if (d->samplesSinceLastFFT >= fftInterval || d->samplesSinceLastFFT == kFftSize)
        {
            // Copy circular buffer to linear temp buffer for FFT
            int startIdx = idx;  // Current position in circular buffer
            for (int i = 0; i < kFftSize; ++i) {
                int circIdx = (startIdx + i) % kFftSize;
                d->tempBuffer[i] = d->inputBuffer[circIdx];
            }
            
            // Apply Hann window to temp buffer
            for (int i = 0; i < kFftSize; ++i)
            {
                float w = 0.5f * (1.0f - cosf((2.0f * M_PI_F * i) / (kFftSize - 1)));
                d->tempBuffer[i] *= w;
            }
            
            // Perform table-free FFT (real input -> complex output)
            realFFT(d->tempBuffer, d->fftOutput, kFftSize);

            // Calculate magnitudes from complex FFT output
            const int half = kFftSize / 2;
            for (int k = 0; k < half; ++k)
            {
                float re = d->fftOutput[k].real;
                float im = d->fftOutput[k].imag;
                d->magnitude[k] = sqrtf(re * re + im * im);
            }

            // Calculate bin resolution for bandwidth calculation
            float binHz = sampleRate / (float)kFftSize;

            // Get detection mode (0 = RMS, 1 = Peak)
            bool usePeakDetection = (self->v[kParamDetectionMode] == 1);

            // Update envelopes for each band.
            for (int b=0;b<3;++b)
            {
                // Convert centre freq (Hz) → bin
                float centreBin = d->potCentreBins[b];
                float centreFreq = d->potCentres[b];

                // Calculate bandwidth in bins based on octaves
                // bandwidth_hz = centre_freq * (2^octaves - 1)
                float bandwidthHz = centreFreq * (powf(2.0f, d->bandwidthOctaves) - 1.0f);
                float bandwidthBins = bandwidthHz / binHz;

                // Calculate bin range
                int lo = (int)roundf(centreBin - bandwidthBins / 2.0f);
                int hi = (int)roundf(centreBin + bandwidthBins / 2.0f);
                if (lo < 0) lo = 0;
                if (hi >= half) hi = half-1;

                float env = 0.0f;
                if (hi >= lo) {
                    // Peak and RMS metrics aggregated over the band
                    float peakMag = 0.0f;
                    int peakBin = lo;
                    float powerSum = 0.0f;

                    for (int k = lo; k <= hi; ++k) {
                        float mag = d->magnitude[k];

                        if (mag > peakMag) {
                            peakMag = mag;
                            peakBin = k;
                        }

                        float mag2 = mag * mag;
                        float weight = (k == 0 || k == half) ? 1.0f : 2.0f;  // Account for mirrored bins
                        powerSum += mag2 * weight;
                    }

                    if (usePeakDetection) {
                        // Convert FFT magnitude back to linear peak amplitude
                        float peakScale = (peakBin == 0 || peakBin == half) ? kPeakNormEdge : kPeakNormPositive;
                        env = peakMag * peakScale;
                    } else if (powerSum > 0.0f) {
                        // Convert band power to an RMS amplitude, scaled so full-scale sine → 1.0
                        float rms = sqrtf(powerSum) * kFftRmsNormalization;
                        env = rms * kSqrtTwo;
                    }
                }

                if (env < 0.0f) {
                    env = 0.0f;
                } else if (env > 1.0f) {
                    env = 1.0f;
                }

                // Apply exponential smoothing with separate attack and release
                if (env > d->env[b]) {
                    // Attack: approaching higher value
                    d->env[b] += d->attackCoeff * (env - d->env[b]);
                } else {
                    // Release: approaching lower value
                    d->env[b] += d->releaseCoeff * (env - d->env[b]);
                }
            }

            // Reset FFT timer
            d->samplesSinceLastFFT = 0;
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
        // Initialize magnitude array with small values to provide initial display
        for (int i = 0; i < kFftSize/2; i++) {
            d->magnitude[i] = 0.001f;  // Small non-zero value for initial display
        }
        
        d->displayInitialized = true;
    }

    // Draw spectrum visualization
    const int width = 256;
    const int height = 64;
    const int half = kFftSize / 2;  // 512 bins

    // Get sample rate and bin resolution for pink noise calculation
    float sampleRate = (NT_globals.sampleRate > 0) ? (float)NT_globals.sampleRate : 48000.0f;
    float binHz = sampleRate / (float)kFftSize;

    // Draw pink noise reference overlay first (as background)
    // Pink noise has 1/f power spectrum, drops 3dB per octave
    // Draw it in a darker color (3-4) so it doesn't overpower the actual spectrum
    for (int x = 1; x < width && x < half; ++x) {  // Start at 1 to avoid log(0)
        int binIdx = x;
        float freq = binIdx * binHz;

        // Pink noise magnitude is proportional to 1/sqrt(f)
        // Reference: at 1kHz, use a reasonable magnitude
        const float refFreq = 1000.0f;
        const float refMag = 50.0f;  // Tunable reference magnitude

        float pinkMag = refMag * sqrtf(refFreq / freq);

        // Apply same logarithmic scaling as main spectrum
        float logMag = (pinkMag > 0.001f) ? logf(pinkMag + 1.0f) : 0.0f;

        // Scale to screen height
        int barHeight = (int)(logMag * d->yScale * 20.0f);
        if (barHeight < 0) barHeight = 0;
        if (barHeight > height) barHeight = height;

        // Draw pink noise reference line in darker color
        if (barHeight > 0 && x >= 0 && x < width) {
            int y1 = height - barHeight;
            int y2 = height - 1;
            if (y1 < 0) y1 = 0;
            if (y2 >= height) y2 = height - 1;
            if (y1 <= y2) {
                NT_drawShapeI(kNT_line, x, y1, x, y2, 3);  // Darker color for background
            }
        }
    }

    // Always draw a baseline at the bottom to verify drawing is working
    NT_drawShapeI(kNT_line, 0, height-1, width-1, height-1, 15);

    // Draw consecutive bins - 512 FFT gives us 256 bins, map directly to 256 pixels
    for (int x = 0; x < width && x < half; ++x) {
        // Map display pixel directly to FFT bin (1:1 mapping)
        int binIdx = x;
        
        // Get magnitude for this bin directly
        float mag = d->magnitude[binIdx];
        
        // Apply logarithmic scaling for better visualization
        float logMag = (mag > 0.001f) ? logf(mag + 1.0f) : 0.0f;
        
        // Scale to screen height - increase sensitivity to see weaker signals
        int barHeight = (int)(logMag * d->yScale * 20.0f);
        if (barHeight < 0) barHeight = 0;
        if (barHeight > height) barHeight = height;

        // Draw spectrum line for this bin
        if (barHeight > 0 && x >= 0 && x < width) {
            int y1 = height - barHeight;
            int y2 = height - 1;
            // Ensure y coordinates are reasonable
            if (y1 < 0) y1 = 0;
            if (y2 >= height) y2 = height - 1;
            if (y1 <= y2) {
                NT_drawShapeI(kNT_line, x, y1, x, y2, 7);  // Vertical line for each bin
            }
        }
    }

    // Draw band center markers based on current parameter values
    for (int b = 0; b < 3; ++b) {
        // Get frequency directly from parameter values
        int paramIndex = kParamBandAFreq + b;
        float frequency = (float)self->v[paramIndex];
        
        // Draw marker - use default if frequency is 0
        if (frequency <= 0.0f) {
            // Use default frequencies if not set
            frequency = (b == 0) ? 100.0f : ((b == 1) ? 1000.0f : 8000.0f);
        }
        {
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
    if (!self || !self->dtc) return;  // Safety check
    
    auto *d = self->dtc;

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

    // Encoder R – Detection mode toggle (RMS/Peak).
    if (ui.encoders[1] != 0)
    {
        // Toggle between 0 (RMS) and 1 (Peak)
        int currentMode = self->v[kParamDetectionMode];
        int newMode = (currentMode == 0) ? 1 : 0;
        NT_setParameterFromUi(NT_algorithmIndex(self), kParamDetectionMode + NT_parameterOffset(), newMode);
    }
}

static void setupUi(_NT_algorithm *base, _NT_float3 &pots) 
{
    auto *self = (_SpectralEnvFollower *)base;
    
    // Safety check - ensure self and self->v are valid
    if (!self || !self->v) {
        // Set default pot positions if parameter access is not safe
        pots[0] = freqToPot(100.0f);   // Band A default
        pots[1] = freqToPot(1000.0f);  // Band B default
        pots[2] = freqToPot(8000.0f);  // Band C default
        return;
    }
    
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
