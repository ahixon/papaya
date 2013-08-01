# @LICENSE(NICTA_CORE) 

# Targets
TARGETS := libnfs.a

# Source files required to build the target
CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c)) 

# Header files/directories this library provides
RHDRFILES := '$(wildcard $(SOURCE_DIR)/include/*) nfs'

# Extra flags for c compliler
CFLAGS := -Wall -Werror

include $(SEL4_COMMON)/common.mk

.PHONY: docs
docs: docs/libnfs.pdf

.PHONY: docs/libnfs.pdf
docs/libnfs.pdf: docs/Doxyfile include/nfs.h
	doxygen docs/Doxyfile
	$(MAKE) --directory=docs/latex
	cp docs/latex/refman.pdf docs/libnfs.pdf


