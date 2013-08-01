# @LICENSE(NICTA_CORE) 

# Targets
TARGETS := libclock.a

# Source files required to build the target
CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c)) 

HDRFILES := $(wildcard $(SOURCE_DIR)/include/*)

CFLAGS := -g -O0
include $(SEL4_COMMON)/common.mk

