# Targets
TARGETS := libpawpaw.a

# Source files required to build the target
CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))

# Header files/directories this library provides
HDRFILES := $(wildcard $(SOURCE_DIR)/include/*)
CFLAGS := -g -O0 -Wall -Werror

LIBS :=

include $(SEL4_COMMON)/common.mk
