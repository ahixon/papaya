# @LICENSE(NICTA_CORE)

# Targets
TARGETS := libethdrivers.a

# Source files required to build the target
#CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/*.c))
CFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/plat/$(PLAT)/*.c))
CFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/plat/$(PLAT)/uboot/*.c))
CFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/*.c))

# This should be excluded for camkes
CFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/plat/$(PLAT)/resource/*.c))

#ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard ${SOURCE_DIR}/src/arch/$(ARCH)/*.S))

#include $(SOURCE_DIR)/src/plat/imx6/Makefile

# Header files/directories this library provides
HDRFILES := \
    $(wildcard ${SOURCE_DIR}/include/*) \
    $(wildcard ${SOURCE_DIR}/plat_include/$(PLAT)/*) \
    $(wildcard ${SOURCE_DIR}/arch_include/${ARCH}/*)

include $(SEL4_COMMON)/common.mk
