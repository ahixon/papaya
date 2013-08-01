# @LICENSE(NICTA_CORE)
#
# Targets
TARGETS := elfloader.o

# Source files required to build the target
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch-$(ARCH)/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch-$(ARCH)/plat-$(PLAT)/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch-$(ARCH)/elf/*.c))
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(SOURCE_DIR)/src/arch-$(ARCH)/crt0.S)
ASMFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch-$(ARCH)/plat-$(PLAT)/*.S))

NK_CFLAGS += -D_XOPEN_SOURCE=700

ifeq ($(ARMV),armv5)
ASMFILES += $(patsubst $(SOURCE_DIR)/%,%,$(SOURCE_DIR)/src/arch-$(ARCH)/mmu-v5.S)
endif
ifeq ($(ARMV),armv6)
ASMFILES += $(patsubst $(SOURCE_DIR)/%,%,$(SOURCE_DIR)/src/arch-$(ARCH)/mmu-v6.S)
endif
ifeq ($(ARMV),armv7-a)
ASMFILES += $(patsubst $(SOURCE_DIR)/%,%,$(SOURCE_DIR)/src/arch-$(ARCH)/mmu-v7a.S)
endif

LIBS = cpio

include $(SEL4_COMMON)/common.mk

#
# We produce a partially linked object file here which, to be used, will be
# eventually relinked with the compiled kernel and user images forming
# a bootable ELF file.
#
elfloader.o: $(OBJFILES)
	@echo "[LD] $@"
	@$(LD) -r $^ $(LDFLAGS) -o $@
