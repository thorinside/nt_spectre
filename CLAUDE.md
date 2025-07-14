# Claude Memory for Spectre Project

## Project Overview
This is a **Disting NT plugin** called "Spectre" - a 3-band spectral envelope follower with real-time FFT analysis and custom OLED visualization.

- **Developer**: Thorinside (Neal Sanche)
- **Plugin GUID**: `ThSf` (Thorinside Spectral Follower)
- **Plugin Name**: "SpecEnv 3‑Band"

## Build System
- **Target Platform**: ARM Cortex-M7 (Disting NT hardware)
- **Output Format**: Relocatable object file (`.o`) not executable
- **Main Target**: `build/spectralEnvFollower_plugin.o`
- **Build Command**: `make`
- **Symbol Check**: `make check` (verifies undefined symbols)
- **Size Check**: `make size`

## Key Architecture Decisions

### Memory Management
- **All buffers pre-allocated in DTC** (fast access memory)
- **No dynamic allocation** - everything sized at compile time
- **Memory calculated in `calculateRequirements()`** function
- FFT instances stored in user-controlled DTC memory

### FFT Integration
- Uses **CMSIS-DSP library** with ARM-optimized assembly
- **Minimal integration**: Only essential source files compiled
- **Custom `cmsis_compiler.h`**: Provides minimal CMSIS core compatibility
- **Pre-allocated FFT instances**: Initialized on-demand in DTC memory
- **Lazy initialization**: FFT instances created when first used per size

### Symbol Dependencies
**Expected undefined symbols** (provided by Disting NT host):
- `NT_drawText`, `NT_globals`, `NT_screen` (Disting NT API)
- `memset` (standard C library)
- `cosf`, `expf` (math library)
- `__aeabi_*` (ARM EABI runtime functions)

**Eliminated problematic symbols**:
- `__errno` (via custom stub function)

## File Structure
```
spectralEnvFollower.cpp  # Main plugin implementation
Makefile                 # Build system
cmsis_compiler.h         # Minimal CMSIS compatibility header
.gitignore              # Excludes build artifacts
build/                  # Generated files (not in git)
extern/                 # Git submodules (distingnt-api, cmsis-dsp)
```

## Plugin Features
- **3 frequency bands** with user-configurable center frequencies
- **Variable FFT sizes**: 256, 512, 1024, 2048 points (user selectable)
- **Real-time spectrum display** on OLED with band markers
- **CV outputs**: 0-10V envelope following for each band
- **Custom UI**: Pots control band frequencies, encoders control display/FFT size

## Important Technical Notes

### Compilation Flags
- **ARM-specific**: `-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard`
- **CMSIS-DSP**: `-DARM_MATH_CM7 -DARM_MATH_MATRIX_CHECK -DARM_MATH_ROUNDING`
- **Math optimization**: `-ffast-math -fno-math-errno`

### Disting NT Specifics
- Plugin format: **Relocatable object file** (not shared library)
- Memory regions: **SRAM** (algorithm instance) + **DTC** (buffers)
- No `main()` function - uses `pluginEntry()` export
- Custom UI overrides pots and encoders

### Testing Commands
```bash
make                    # Build plugin
make check             # Verify symbols  
make clean && make     # Clean rebuild
```

## Troubleshooting

### Common Issues
1. **Compilation errors**: Check CMSIS-DSP source files exist
2. **Symbol errors**: Run `make check` to identify missing dependencies
3. **Size issues**: Use `make size` to check memory usage
4. **FFT problems**: Verify CMSIS instances are properly initialized

### Build Dependencies
- **ARM GCC toolchain**: `arm-none-eabi-gcc/g++`
- **CMSIS-DSP submodule**: Must be initialized (`git submodule update --init`)
- **Disting NT API submodule**: Must be initialized

## Next Steps for Development
- Test plugin on actual Disting NT hardware
- Optimize memory usage if needed
- Add more frequency bands if memory permits
- Consider adding different windowing functions
- Implement preset saving/loading if API supports it