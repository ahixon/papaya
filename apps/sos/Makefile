# Targets
TARGETS := sos.bin

ifeq (${COMPONENTS},)
$(error "No compoments setup to build against.")
endif

# Source files required to build the target
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/boot/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/ut_manager/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/services/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/sys/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/syscalls/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/vm/*.c))
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/crt/arch-${ARCH}/crt0.S))
OFILES   := archive.o


INCLUDE_DIRS  += $(SOURCE_DIR)/src 

# Libraries required to build the target
LIBS := sel4 elf sel4c cpio sel4cspace pawpaw

include $(SEL4_COMMON)/common.mk


${COMPONENTS}:
	false

archive.o: ${COMPONENTS} $(SOURCE_DIR)/src/boot/boot.txt $(SOURCE_DIR)/src/boot/fstab.txt
	$(Q)mkdir -p $(dir $@)
	echo "LINKING $@"
	${COMMON_PATH}/files_to_obj.sh $@ _cpio_archive $^ 
