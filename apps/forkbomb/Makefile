# Targets
TARGETS := forkbomb.bin

# Source files required to build the target
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/crt/crt0.S))

# Libraries required to build the target
LIBS := sel4c sel4 sos pawpaw
#export DEBUG=1
include $(SEL4_COMMON)/common.mk
