###############################################################################
#  Disting NT plug-in — Spectral Envelope Follower (3-band, CV out)
#  Build file – static link to CMSIS-DSP FFT for Cortex-M / Cortex-A cores
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
CMSIS_DIR      := extern/cmsis-dsp
CMSIS_INC      := $(CMSIS_DIR)/Include
# Essential CMSIS-DSP FFT source files
CMSIS_FFT_SRC  := $(CMSIS_DIR)/Source/TransformFunctions/arm_cfft_f32.c \
                  $(CMSIS_DIR)/Source/TransformFunctions/arm_cfft_radix8_f32.c \
                  $(CMSIS_DIR)/Source/TransformFunctions/arm_bitreversal2.c \
                  $(CMSIS_DIR)/Source/TransformFunctions/arm_cfft_init_f32.c \
                  $(CMSIS_DIR)/Source/CommonTables/arm_common_tables.c \
                  $(CMSIS_DIR)/Source/CommonTables/arm_const_structs.c

############################  Files  ##########################################
PLUGIN_SRC  := spectralEnvFollower.cpp
PLUGIN_OBJ  := $(BUILD_DIR)/$(notdir $(PLUGIN_SRC:.cpp=.o))
CMSIS_FFT_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CMSIS_FFT_SRC))
OBJS           := $(PLUGIN_OBJ) $(CMSIS_FFT_OBJS)

TARGET_OBJ  := $(BUILD_DIR)/spectralEnvFollower_plugin.o
TARGET_ELF  := $(BUILD_DIR)/spectralEnvFollower.elf
TARGET_BIN  := $(BUILD_DIR)/spectralEnvFollower.bin

############################  Flags  ##########################################
CPPFLAGS += -I$(API_DIR) -I$(CMSIS_INC) -I. -std=c++17 -Os -ffast-math \
            -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti \
            -Wall -Wextra -Werror $(ARCH_FLAGS) \
            -DARM_MATH_CM7 -DARM_MATH_MATRIX_CHECK -DARM_MATH_ROUNDING \
            -fno-math-errno

CFLAGS   += -I$(API_DIR) -I$(CMSIS_INC) -I. -std=c99 -Os -ffast-math \
            -fdata-sections -ffunction-sections \
            -Wall -Wextra -Werror $(ARCH_FLAGS) \
            -DARM_MATH_CM7 -DARM_MATH_MATRIX_CHECK -DARM_MATH_ROUNDING \
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

