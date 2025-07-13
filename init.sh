#!/usr/bin/env bash
# Initialise a fresh repo for a Disting NT plug-in that depends on
# the Expert Sleepers API and the Arm CMSIS-DSP library.
# Usage:  ./init_repo.sh  (from the project root)

set -euo pipefail

############## 1. Create / verify Git repo #########################
if [ ! -d ".git" ]; then
    git init
    echo "✔️  Created new Git repository"
else
    echo "ℹ️  Git repo already exists – continuing"
fi

############## 2. Add submodules ###################################
mkdir -p extern

# Expert Sleepers Disting NT API
if [ ! -d "extern/distingnt-api" ]; then
    git submodule add --depth 1 \
        https://github.com/expertsleepersltd/distingNT_API \
        extern/distingnt-api
    echo "✔️  Added submodule: distingNT_API"
fi

# Arm CMSIS-DSP (fast FFT, NEON / Helium optimised)
if [ ! -d "extern/cmsis-dsp" ]; then
    git submodule add --depth 1 \
        https://github.com/ARM-software/CMSIS-DSP \
        extern/cmsis-dsp
    echo "✔️  Added submodule: CMSIS-DSP"
fi

git submodule update --init --recursive
echo "✔️  Submodules initialised"

############## 3. First commit #####################################
git add .
git commit -m "Initial project skeleton: API + CMSIS-DSP submodules"
echo "🎉  Repo ready.  Run 'make' to build the plug-in."

