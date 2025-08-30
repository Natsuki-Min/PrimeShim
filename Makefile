# Toolchain
CC := arm-linux-gnueabi-gcc
AS := arm-linux-gnueabi-as
LD := arm-linux-gnueabi-gcc

# Flags
CFLAGS := -march=armv5te -mabi=aapcs-linux -mfloat-abi=soft -fPIC -O2
ASFLAGS := -march=armv5te  -mfloat-abi=soft
LDFLAGS := -T linker.ld -march=armv5te

#CFLAGS += -Dfree\(x\)=

# Directories
SRC_DIR := .
BUILD_DIR := build
ASM_DIR := .

# Find all source files
C_SRCS := $(shell find $(SRC_DIR) -type f -name '*.c')
ASM_SRCS := $(shell find $(ASM_DIR) -type f -name '*.asm')

# Object files
C_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst $(ASM_DIR)/%.asm,$(BUILD_DIR)/%.o,$(ASM_SRCS))
OBJS := $(C_OBJS) $(ASM_OBJS)

# Output
TARGET := $(BUILD_DIR)/arm.elf

# Default target
all: $(TARGET)

# Link target
$(TARGET): $(OBJS) linker.ld
	@echo "Linking $@"
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Compile C files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble ASM files
$(BUILD_DIR)/%.o: $(ASM_DIR)/%.asm
	@echo "Assembling $<"
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Clean
clean:
	rm -rf $(BUILD_DIR)

# Phony targets
.PHONY: all clean

# Enable second expansion for directory creation
.SECONDEXPANSION:
