# @LICENSE(NICTA_CORE) 

# Targets
TARGETS := libserial.a

# Source files required to build the target
CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c)) 

RHDRFILES := '$(wildcard $(SOURCE_DIR)/include/*) serial' 

CFLAGS := -g -O0
include $(SEL4_COMMON)/common.mk

