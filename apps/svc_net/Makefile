# @LICENSE(NICTA_CORE)

# Targets
TARGETS := $(notdir $(SOURCE_DIR)).bin

# Source files required to build the target
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/crt/arch-${ARCH}/crt0.S))

INCLUDE_DIRS  += $(SOURCE_DIR)/src 

# Libraries required to build the target
LIBS := sel4c sel4 sos lwip ethdrivers sel4sync pawpaw nfs

include $(SEL4_COMMON)/common.mk
