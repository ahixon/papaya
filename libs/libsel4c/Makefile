# @LICENSE(NICTA_CORE)

# Targets
TARGETS := libsel4c.a

# Source files required to build the target
CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/*.c))
CFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/arch-$(ARCH)/*.c))
CFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/plat-$(PLAT)/*.c))

# ASM files require to build the target
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/arch-$(ARCH)/*.S))

# Header files/directories this library provides
HDRFILES := \
	$(wildcard ${SOURCE_DIR}/include/*) \
	$(wildcard ${SOURCE_DIR}/arch_include/$(ARCH)/*)

include $(SEL4_COMMON)/common.mk
