###############################################################################
#  Disting NT plug-in — Spectral Envelope Follower (3-band, CV out)
#  Build file – uses simple table-free FFT implementation
###############################################################################

############################  Toolchain  ######################################
# Override from the command line if you target a different core:
#   make CROSS_COMPILE=arm-linux-gnueabihf- CPU=cortex-a53
CROSS_COMPILE ?= arm-none-eabi-
CXX      := $(CROSS_COMPILE)g++
CC       := $(CROSS_COMPILE)gcc
AR       := $(CROSS_COMPILE)ar
LD       := $(CXX)
OBJCOPY  := $(CROSS_COMPILE)objcopy
SIZE     := $(CROSS_COMPILE)size

############################  Target CPU  ####################################
CPU ?= cortex-m7
FPU ?= fpv5-sp-d16
FLOATABI ?= hard

ARCH_FLAGS := -mcpu=$(CPU) -mfpu=$(FPU) -mfloat-abi=$(FLOATABI) -mthumb

############################  Directories  ###################################
TOP            := $(CURDIR)
SRC_DIR        := src
BUILD_DIR      := build
API_DIR        := extern/distingnt-api/include

############################  Files  ##########################################
PLUGIN_SRC  := spectralEnvFollower.cpp
PLUGIN_OBJ  := $(BUILD_DIR)/$(notdir $(PLUGIN_SRC:.cpp=.o))
OBJS        := $(PLUGIN_OBJ)

TARGET_OBJ  := $(BUILD_DIR)/spectralEnvFollower_plugin.o
TARGET_ELF  := $(BUILD_DIR)/spectralEnvFollower.elf
TARGET_BIN  := $(BUILD_DIR)/spectralEnvFollower.bin

############################  Flags  ##########################################
INCLUDE_PATH := -I$(API_DIR) -I.
CPPFLAGS += $(INCLUDE_PATH) -std=c++17 -Os -ffast-math \
            -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti \
            -Wall -Wextra -Werror $(ARCH_FLAGS) \
            -fno-math-errno

LDFLAGS  += -static -Wl,--gc-sections $(ARCH_FLAGS)

############################  Rules  ##########################################
all: $(TARGET_OBJ) | size

# Create final plugin object (relocatable)
$(TARGET_OBJ): $(OBJS)
	$(LD) -r -o $@ $^

# Compile C++
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -c $< -o $@

# Compile C
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link
$(TARGET_ELF): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

# Convert ELF → raw binary (if the Disting loader expects .bin)
$(TARGET_BIN): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@

size: $(TARGET_OBJ)
	$(SIZE) $<

clean:
	rm -rf $(BUILD_DIR)

flash: $(TARGET_BIN)
	@echo "⚠️  Implement your own flashing routine here"

# Check for undefined symbols in the plugin
check: $(TARGET_OBJ)
	@echo "Checking for undefined symbols..."
	@$(CROSS_COMPILE)nm -u $(TARGET_OBJ) | grep -v "^[[:space:]]*$$" > /tmp/undefined_symbols.txt || true
	@if [ -s /tmp/undefined_symbols.txt ]; then \
		echo "⚠️  WARNING: Found undefined symbols:"; \
		cat /tmp/undefined_symbols.txt; \
		echo "These symbols must be provided by the host or may cause runtime errors."; \
	else \
		echo "✅ No undefined symbols found - plugin is self-contained!"; \
	fi
	@rm -f /tmp/undefined_symbols.txt

.PHONY: all clean flash size check

# === VCV Emulator Test Builds (added by makefile_augmenter.py) ===
# Build plugins as host platform dynamic libraries for VCV Rack emulator testing
# Host compiler settings
HOST_CXX ?= clang++
HOST_CXXFLAGS := -std=c++11 -fPIC -Wall  $(INCLUDE_PATH)

# Detect host platform
HOST_OS := $(shell uname -s)

ifeq ($(HOST_OS),Darwin)
    HOST_SUFFIX := .dylib
    HOST_LDFLAGS := -dynamiclib -undefined dynamic_lookup
else ifeq ($(HOST_OS),Linux)
    HOST_SUFFIX := .so
    HOST_LDFLAGS := -shared
else
    # Windows/MinGW
    HOST_SUFFIX := .dll
    HOST_LDFLAGS := -shared
endif

# Source files
host_inputs := $(wildcard *.cpp)
# Transform source files to host plugins
host_plugins := $(patsubst %.cpp,%$(HOST_SUFFIX),$(basename host_inputs))

# Build rule for host plugins
%$(HOST_SUFFIX): %.cpp
	@echo "Building host plugin: $@"
	$(HOST_CXX) $(HOST_CXXFLAGS) $(HOST_LDFLAGS) -o $@ $<

# Convenience targets
.PHONY: host-plugins clean-host install-host

host-plugins: $(host_plugins)
	@echo "Built $(words $(host_plugins)) host plugin(s)"

clean-host:
	rm -f *.dylib *.so *.dll

# Install to a test directory (customize INSTALL_DIR as needed)
INSTALL_DIR ?= ../test_plugins
install-host: host-plugins
	@mkdir -p $(INSTALL_DIR)
	cp $(host_plugins) $(INSTALL_DIR)/
	@echo "Installed to $(INSTALL_DIR)"

# Help for host builds
help-host:
	@echo "Host build targets for VCV Rack emulator testing:"
	@echo "  make host-plugins    - Build all plugins for host platform"
	@echo "  make clean-host      - Remove host plugin builds"
	@echo "  make install-host    - Copy plugins to test directory"
	@echo ""
	@echo "Individual plugin targets:"
	@echo "  make <plugin>$(HOST_SUFFIX)"

# === End VCV Emulator Test Builds ===
