# Spectre - 3-Band Spectral Envelope Follower

<div align="center">

![Spectre Plugin](https://img.shields.io/badge/Disting%20NT-Plugin-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Platform](https://img.shields.io/badge/Platform-ARM%20Cortex--M7-red)

*Real-time FFT analysis with three independent CV envelope outputs*

</div>

## Overview

**Spectre** is a sophisticated spectral envelope follower plugin for the Expert Sleepers Disting NT. It performs real-time FFT analysis on incoming audio and extracts envelope information from three user-configurable frequency bands, outputting CV signals that follow the energy in each band.

### Key Features

- **Real-time FFT Analysis** with 512-point FFT running at 60Hz
- **Three Independent Frequency Bands** with configurable center frequencies
- **Dual Detection Modes**: RMS (power-based) and Peak detection
- **Proportional Bandwidth Control**: Variable from 10% to 200% (default: 1/3 octave)
- **Precise Envelope Following**: Separate attack (1-1000ms) and release (10-5000ms) controls
- **Live Spectrum Display** on OLED with band position markers and pink noise reference
- **CV Envelope Outputs** (0-10V) with accurate voltage scaling
- **Interactive Controls** via pots and encoders

## Visual Interface

### Main Display

```svg
<svg width="400" height="200" xmlns="http://www.w3.org/2000/svg">
  <!-- OLED Screen Background -->
  <rect x="50" y="20" width="300" height="150" rx="5" fill="#000" stroke="#333" stroke-width="2"/>
  
  <!-- Spectrum Bars -->
  <g fill="#00ff00" opacity="0.7">
    <rect x="60" y="140" width="4" height="20"/>
    <rect x="66" y="130" width="4" height="30"/>
    <rect x="72" y="120" width="4" height="40"/>
    <rect x="78" y="110" width="4" height="50"/>
    <rect x="84" y="100" width="4" height="60"/>
    <rect x="90" y="90" width="4" height="70"/>
    <rect x="96" y="80" width="4" height="80"/>
    <rect x="102" y="70" width="4" height="90"/>
    <rect x="108" y="60" width="4" height="100"/>
    <rect x="114" y="50" width="4" height="110"/>
    <rect x="120" y="55" width="4" height="105"/>
    <rect x="126" y="65" width="4" height="95"/>
    <rect x="132" y="75" width="4" height="85"/>
    <rect x="138" y="85" width="4" height="75"/>
    <rect x="144" y="95" width="4" height="65"/>
    <rect x="150" y="105" width="4" height="55"/>
    <rect x="156" y="115" width="4" height="45"/>
    <rect x="162" y="125" width="4" height="35"/>
    <rect x="168" y="135" width="4" height="25"/>
    <rect x="174" y="145" width="4" height="15"/>
  </g>
  
  <!-- Band Markers -->
  <line x1="100" y1="30" x2="100" y2="160" stroke="#ff4444" stroke-width="2" opacity="0.8"/>
  <line x1="140" y1="30" x2="140" y2="160" stroke="#44ff44" stroke-width="2" opacity="0.8"/>
  <line x1="180" y1="30" x2="180" y2="160" stroke="#4444ff" stroke-width="2" opacity="0.8"/>
  
  <!-- Band Labels -->
  <text x="100" y="45" text-anchor="middle" fill="#ff4444" font-family="monospace" font-size="12" font-weight="bold">A</text>
  <text x="140" y="45" text-anchor="middle" fill="#44ff44" font-family="monospace" font-size="12" font-weight="bold">B</text>
  <text x="180" y="45" text-anchor="middle" fill="#4444ff" font-family="monospace" font-size="12" font-weight="bold">C</text>
  
  <!-- Title -->
  <text x="200" y="190" text-anchor="middle" fill="#ccc" font-family="Arial" font-size="14" font-weight="bold">Live Spectrum with Band Markers</text>
</svg>
```

### Control Layout

```svg
<svg width="500" height="300" xmlns="http://www.w3.org/2000/svg">
  <!-- Background -->
  <rect width="500" height="300" fill="#f8f8f8" stroke="#ddd" stroke-width="1"/>
  
  <!-- Pots -->
  <g id="pots">
    <!-- Pot Circles -->
    <circle cx="120" cy="100" r="30" fill="#333" stroke="#666" stroke-width="2"/>
    <circle cx="250" cy="100" r="30" fill="#333" stroke="#666" stroke-width="2"/>
    <circle cx="380" cy="100" r="30" fill="#333" stroke="#666" stroke-width="2"/>
    
    <!-- Pot Indicators -->
    <line x1="120" y1="85" x2="120" y2="75" stroke="#ff4444" stroke-width="3" stroke-linecap="round"/>
    <line x1="250" y1="85" x2="250" y2="75" stroke="#44ff44" stroke-width="3" stroke-linecap="round"/>
    <line x1="380" y1="85" x2="380" y2="75" stroke="#4444ff" stroke-width="3" stroke-linecap="round"/>
    
    <!-- Labels -->
    <text x="120" y="150" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">POT L</text>
    <text x="120" y="165" text-anchor="middle" font-family="Arial" font-size="10" fill="#ff4444">Band A Freq</text>
    <text x="250" y="150" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">POT C</text>
    <text x="250" y="165" text-anchor="middle" font-family="Arial" font-size="10" fill="#44ff44">Band B Freq</text>
    <text x="380" y="150" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">POT R</text>
    <text x="380" y="165" text-anchor="middle" font-family="Arial" font-size="10" fill="#4444ff">Band C Freq</text>
  </g>
  
  <!-- Encoders -->
  <g id="encoders">
    <!-- Encoder Circles -->
    <circle cx="120" cy="220" r="25" fill="#555" stroke="#888" stroke-width="2"/>
    <circle cx="380" cy="220" r="25" fill="#555" stroke="#888" stroke-width="2"/>
    
    <!-- Encoder Indicators -->
    <line x1="120" y1="205" x2="120" y2="200" stroke="#white" stroke-width="2" stroke-linecap="round"/>
    <line x1="380" y1="205" x2="380" y2="200" stroke="#white" stroke-width="2" stroke-linecap="round"/>
    
    <!-- Labels -->
    <text x="120" y="260" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">ENC L</text>
    <text x="120" y="275" text-anchor="middle" font-family="Arial" font-size="10">Y-Scale (×½/×2)</text>
    <text x="380" y="260" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">ENC R</text>
    <text x="380" y="275" text-anchor="middle" font-family="Arial" font-size="10">FFT Size</text>
  </g>
  
  <!-- Title -->
  <text x="250" y="30" text-anchor="middle" font-family="Arial" font-size="16" font-weight="bold">Spectre Control Layout</text>
</svg>
```

## User Manual

### Quick Start

1. **Load the plugin** onto your Disting NT
2. **Connect audio input** to the designated input
3. **Connect CV outputs** to your desired destinations
4. **Adjust frequency bands** using the three pots
5. **Fine-tune display** with the encoders

### Control Reference

#### Frequency Band Controls (Pots)

| Control | Function | Range | Color |
|---------|----------|-------|-------|
| **Pot L** | Band A Center Frequency | 20 Hz - 20 kHz | 🔴 Red |
| **Pot C** | Band B Center Frequency | 20 Hz - 20 kHz | 🟢 Green |
| **Pot R** | Band C Center Frequency | 20 Hz - 20 kHz | 🔵 Blue |

The frequency mapping follows a **logarithmic scale** for musical perception:
- Fully CCW: 20 Hz (sub-bass)
- 12 o'clock: ~450 Hz (midrange)
- Fully CW: 20 kHz (high frequencies)

#### Display Controls (Encoders)

| Control | Function | Effect |
|---------|----------|--------|
| **Encoder L** | Spectrum Y-Scale | Each detent: ×2 or ×½ scaling |
| **Encoder R** | Detection Mode | Toggles: RMS ↔ Peak |

### Parameter Pages

The plugin has three parameter pages accessible via the standard Disting NT menu:

1. **Routing Page** - Configure I/O routing
2. **Spectral Page** - Set band center frequencies
3. **Envelope Page** - Configure bandwidth, attack/release times, and detection mode

### CV Output Behavior

Each frequency band generates a **0-10V CV signal** that follows the energy in that band:

- **0V**: No energy detected in the band
- **10V**: Maximum energy detected
- **Response**: Configurable attack/release times (default: 10ms attack, 100ms release)
- **Bandwidth**: Proportional to center frequency (default: 1/3 octave)

### Performance Tips

1. **For Percussion**: Use Peak detection mode and fast attack times (1-10ms)
2. **For Sustained Sounds**: Use RMS detection mode with longer attack times (50-200ms)
3. **Band Spacing**: Space frequency bands at least 1 octave apart for independent tracking
4. **Bandwidth Control**: Wider bands capture more energy but lose frequency specificity
5. **Pink Noise Reference**: Use the background overlay to match your mix's spectral balance

## Patching Examples

### Basic Spectrum-Controlled Filter

```svg
<svg width="600" height="250" xmlns="http://www.w3.org/2000/svg">
  <!-- Background -->
  <rect width="600" height="250" fill="#f8f8f8" stroke="#ddd" stroke-width="1"/>
  
  <!-- Audio Input -->
  <rect x="50" y="50" width="80" height="40" rx="5" fill="#e8f4f8" stroke="#4a90a4"/>
  <text x="90" y="75" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">Audio In</text>
  
  <!-- Spectre Plugin -->
  <rect x="200" y="30" width="120" height="80" rx="5" fill="#ffe8e8" stroke="#d63384"/>
  <text x="260" y="50" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">Spectre</text>
  <text x="260" y="65" text-anchor="middle" font-family="Arial" font-size="10">3-Band Analysis</text>
  
  <!-- CV Outputs -->
  <circle cx="340" cy="50" r="6" fill="#ff6b6b"/>
  <circle cx="340" cy="70" r="6" fill="#4ecdc4"/>
  <circle cx="340" cy="90" r="6" fill="#45b7d1"/>
  <text x="355" y="55" font-family="Arial" font-size="10">CV A (Low)</text>
  <text x="355" y="75" font-family="Arial" font-size="10">CV B (Mid)</text>
  <text x="355" y="95" font-family="Arial" font-size="10">CV C (High)</text>
  
  <!-- VCF -->
  <rect x="450" y="50" width="80" height="40" rx="5" fill="#e8f8e8" stroke="#4a90a4"/>
  <text x="490" y="75" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">VCF</text>
  
  <!-- Connections -->
  <path d="M 130 70 L 200 70" stroke="#333" stroke-width="2" fill="none" marker-end="url(#arrowhead)"/>
  <path d="M 340 70 L 380 70 L 380 85 L 450 85" stroke="#4ecdc4" stroke-width="2" fill="none" marker-end="url(#arrowhead)"/>
  
  <!-- Arrow marker -->
  <defs>
    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#333"/>
    </marker>
  </defs>
  
  <!-- Labels -->
  <text x="165" y="85" text-anchor="middle" font-family="Arial" font-size="10" fill="#666">Audio</text>
  <text x="395" y="100" text-anchor="middle" font-family="Arial" font-size="10" fill="#666">Cutoff CV</text>
  
  <!-- Description -->
  <text x="300" y="180" text-anchor="middle" font-family="Arial" font-size="14" font-weight="bold">Spectrum-Controlled Filter</text>
  <text x="300" y="200" text-anchor="middle" font-family="Arial" font-size="12">Mid-frequency band controls filter cutoff frequency</text>
  <text x="300" y="215" text-anchor="middle" font-family="Arial" font-size="12">Filter opens when midrange content is present</text>
</svg>
```

### Multi-Band Drum Gate

```svg
<svg width="600" height="300" xmlns="http://www.w3.org/2000/svg">
  <!-- Background -->
  <rect width="600" height="300" fill="#f8f8f8" stroke="#ddd" stroke-width="1"/>
  
  <!-- Drum Input -->
  <rect x="30" y="120" width="80" height="40" rx="5" fill="#e8f4f8" stroke="#4a90a4"/>
  <text x="70" y="145" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">Drums</text>
  
  <!-- Spectre Plugin -->
  <rect x="150" y="100" width="120" height="80" rx="5" fill="#ffe8e8" stroke="#d63384"/>
  <text x="210" y="120" text-anchor="middle" font-family="Arial" font-size="12" font-weight="bold">Spectre</text>
  <text x="210" y="135" text-anchor="middle" font-family="Arial" font-size="10">3-Band Analysis</text>
  
  <!-- CV Outputs to Gates -->
  <rect x="320" y="50" width="60" height="30" rx="3" fill="#fff2cc" stroke="#d6b656"/>
  <text x="350" y="70" text-anchor="middle" font-family="Arial" font-size="10" font-weight="bold">Gate 1</text>
  
  <rect x="320" y="120" width="60" height="30" rx="3" fill="#fff2cc" stroke="#d6b656"/>
  <text x="350" y="140" text-anchor="middle" font-family="Arial" font-size="10" font-weight="bold">Gate 2</text>
  
  <rect x="320" y="190" width="60" height="30" rx="3" fill="#fff2cc" stroke="#d6b656"/>
  <text x="350" y="210" text-anchor="middle" font-family="Arial" font-size="10" font-weight="bold">Gate 3</text>
  
  <!-- Sound Generators -->
  <rect x="430" y="50" width="80" height="30" rx="3" fill="#e1f5fe" stroke="#4a90a4"/>
  <text x="470" y="70" text-anchor="middle" font-family="Arial" font-size="10">Hi-Hat Gen</text>
  
  <rect x="430" y="120" width="80" height="30" rx="3" fill="#e1f5fe" stroke="#4a90a4"/>
  <text x="470" y="140" text-anchor="middle" font-family="Arial" font-size="10">Snare Gen</text>
  
  <rect x="430" y="190" width="80" height="30" rx="3" fill="#e1f5fe" stroke="#4a90a4"/>
  <text x="470" y="210" text-anchor="middle" font-family="Arial" font-size="10">Kick Gen</text>
  
  <!-- Connections -->
  <path d="M 110 140 L 150 140" stroke="#333" stroke-width="2" fill="none"/>
  <path d="M 270 120 L 320 65" stroke="#ff6b6b" stroke-width="2" fill="none"/>
  <path d="M 270 140 L 320 135" stroke="#4ecdc4" stroke-width="2" fill="none"/>
  <path d="M 270 160 L 320 205" stroke="#45b7d1" stroke-width="2" fill="none"/>
  
  <path d="M 380 65 L 430 65" stroke="#333" stroke-width="2" fill="none"/>
  <path d="M 380 135 L 430 135" stroke="#333" stroke-width="2" fill="none"/>
  <path d="M 380 205 L 430 205" stroke="#333" stroke-width="2" fill="none"/>
  
  <!-- Frequency Labels -->
  <text x="290" y="115" font-family="Arial" font-size="10" fill="#ff6b6b">High</text>
  <text x="290" y="135" font-family="Arial" font-size="10" fill="#4ecdc4">Mid</text>
  <text x="290" y="165" font-family="Arial" font-size="10" fill="#45b7d1">Low</text>
  
  <!-- Description -->
  <text x="300" y="260" text-anchor="middle" font-family="Arial" font-size="14" font-weight="bold">Multi-Band Drum Separation</text>
  <text x="300" y="280" text-anchor="middle" font-family="Arial" font-size="12">Each frequency band triggers a different drum sound generator</text>
</svg>
```

## Suggested Usage Scenarios

### 1. **Frequency-Responsive Effects**

**Setup**: Route CV outputs to control effect parameters
- **Low band** → Reverb size (more reverb on bass hits)
- **Mid band** → Delay feedback (delay swells with vocal)
- **High band** → Filter resonance (emphasis on cymbals)

**Patch Notes**:
- Set bands to: 80Hz (low), 800Hz (mid), 8kHz (high)
- Use 512 or 1024 FFT for good frequency separation
- Attenuate CV signals if effects are too sensitive

### 2. **Spectral Gate Sequencing**

**Setup**: Use Spectre to create rhythm from any audio source
- Analyze complex audio (full mixes, field recordings)
- Extract rhythmic patterns from different frequency ranges
- Drive clock dividers or sequencers with the CV outputs

**Patch Notes**:
- Experiment with different FFT sizes for varying gate lengths
- Use comparators to convert CV to clean gates
- Chain multiple Spectres for more frequency bands

### 3. **Harmonic Following Bass**

**Setup**: Extract bass notes from polyphonic material
- **Low band** centered around fundamental frequency range
- CV output drives oscillator pitch or filter cutoff
- Creates a bass line that follows the harmonic content

**Patch Notes**:
- Set low band around 60-250Hz for bass fundamentals
- Use envelope follower on CV output for smoother pitch changes
- Combine with sample & hold for stepped bass lines

### 4. **Vocal Formant Analysis**

**Setup**: Track speech characteristics for vocal processing
- **Band A**: 800Hz (first formant)
- **Band B**: 1200Hz (second formant)
- **Band C**: 2500Hz (third formant)

**Applications**:
- Vocoder control signals
- Speech-responsive synthesis
- Automatic vocal EQ following

### 5. **Dynamic Frequency Splitting**

**Setup**: Create frequency-dependent dynamics
- Each band controls a VCA or compressor
- Different frequency ranges get different dynamic treatment
- Useful for mastering and bus processing

**Patch Notes**:
- Use larger FFT sizes (1024-2048) for precise frequency separation
- Invert some CV signals for frequency-dependent expansion
- Combine with multiband compressors

### 6. **Percussive Element Extraction**

**Setup**: Isolate drum elements from complex mixes
- **High band**: Cymbals and hi-hats (8kHz+)
- **Mid band**: Snares and claps (200Hz-2kHz)
- **Low band**: Kick drums (40-100Hz)

**Creative Uses**:
- Trigger samples based on frequency content
- Create drum fills from non-percussive sources
- Extract groove from full mixes

### 7. **Environmental Sound Reactive**

**Setup**: Respond to ambient sound characteristics
- **Low**: Rumble and mechanical sounds
- **Mid**: Voice and melodic content
- **High**: Birds, wind, electronic sounds

**Applications**:
- Generative music systems
- Sound installation control
- Adaptive soundscapes

## Advanced Techniques

### CV Processing Tips

1. **Smoothing**: Add slew limiters to CV outputs for smoother parameter changes
2. **Scaling**: Use attenuverters to scale CV ranges to match your modules
3. **Logic**: Combine CV signals with logic modules for complex behaviors
4. **Quantization**: Quantize CV outputs for musical intervals

### Frequency Band Strategies

- **Musical Intervals**: Set bands to musical ratios (octaves, fifths)
- **Instrument Ranges**: Match bands to specific instrument frequency ranges
- **Psychoacoustic**: Use critical band frequencies for perceptual relevance
- **Dynamic**: Modulate band frequencies for animated analysis

### Performance Considerations

- **FFT Size vs. Latency**: Smaller FFT = lower latency but less frequency precision
- **Band Overlap**: Overlapping bands can create interesting interactions
- **Input Gain**: Adjust input levels for optimal analysis sensitivity
- **Update Rate**: Consider how fast you need the analysis to respond

## Building from Source

### Prerequisites

You need the ARM GCC toolchain installed:

**macOS (Homebrew):**
```bash
brew install --cask gcc-arm-embedded
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install gcc-arm-none-eabi
```

**Windows:**
Download and install the [ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).

### Repository Setup

1. **Clone the repository with submodules:**
```bash
git clone --recursive https://github.com/yourusername/spectre.git
cd spectre
```

2. **If you already cloned without `--recursive`, initialize submodules:**
```bash
git submodule update --init --recursive
```

This will download:
- `extern/distingnt-api/` - Disting NT plugin API
- `extern/cmsis-dsp/` - ARM CMSIS-DSP mathematics library

### Build Process

1. **Build the plugin:**
```bash
make
```

2. **Check for undefined symbols:**
```bash
make check
```

3. **View memory usage:**
```bash
make size
```

4. **Clean build artifacts:**
```bash
make clean
```

### Build Output

The build process generates:
- **`build/spectralEnvFollower_plugin.o`** - Final plugin file for Disting NT
- **`build/spectralEnvFollower.o`** - Main plugin object
- **`build/extern/cmsis-dsp/`** - CMSIS-DSP object files

### Installation

1. Copy `build/spectralEnvFollower_plugin.o` to your Disting NT
2. Follow the Disting NT plugin installation procedure
3. The plugin will appear as "SpecEnv 3‑Band" with GUID `ThSf`

## Technical Specifications

### Audio Processing
- **Sample Rate**: Matches Disting NT host (typically 48 kHz)
- **Bit Depth**: 32-bit floating point internal processing
- **FFT Algorithm**: ARM-optimized CMSIS-DSP implementation
- **Windowing**: Hann window for spectral analysis
- **Latency**: Depends on FFT size (256 samples minimum)

### Memory Usage
- **Program Memory**: ~586 KB (including FFT lookup tables)
- **DTC Memory**: ~20.6 KB (audio buffers and FFT workspace)
- **SRAM**: <1 KB (algorithm instance)

### Frequency Response
- **Analysis Range**: 0 Hz to Nyquist frequency (sample_rate/2)
- **Band Width**: ±3 FFT bins around center frequency
- **Frequency Resolution**: sample_rate / FFT_size

## Troubleshooting

### Common Issues

**Q: Plugin doesn't appear in Disting NT**
- Verify the `.o` file is properly copied
- Check that submodules are initialized (`git submodule status`)
- Ensure ARM toolchain is correctly installed

**Q: Build fails with "command not found"**
- Install ARM GCC toolchain
- Add toolchain to your PATH environment variable

**Q: Undefined symbol errors**
- Run `make check` to see which symbols are undefined
- Expected symbols: `NT_*`, `memset`, math functions, ARM EABI functions

**Q: No audio response**
- Verify audio input is connected
- Check that CV outputs are properly routed
- Adjust Y-scale with Encoder L if spectrum is too small

### Development

For development and debugging:
```bash
# View symbol table
arm-none-eabi-nm build/spectralEnvFollower_plugin.o

# Disassemble code
arm-none-eabi-objdump -d build/spectralEnvFollower_plugin.o

# Check dependencies
arm-none-eabi-objdump -p build/spectralEnvFollower_plugin.o
```

## License

This project is released under the MIT License. See the LICENSE file for details.

## Credits

- **Developer**: Thorinside (Neal Sanche)
- **FFT Library**: ARM CMSIS-DSP
- **Platform**: Expert Sleepers Disting NT
- **Generated with assistance from**: Claude Code

## Version History

### v1.0.0 (2025-10-25)
- Initial release
- 3-band spectral envelope follower with 512-point FFT
- RMS and Peak detection modes
- Proportional bandwidth control (10-200%)
- Attack/Release envelope parameters (1-1000ms, 10-5000ms)
- Pink noise reference overlay on spectrum display
- Proper FFT normalization and voltage scaling for accurate CV output
- Fixed envelope timing to use FFT update rate (60Hz)