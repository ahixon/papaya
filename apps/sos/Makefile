# Targets
TARGETS := sos.bin

ifeq (${COMPONENTS},)
$(error "No compoments setup to build against.")
endif

# Source files required to build the target
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/ut_manager/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/sys/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/vm/*.c))
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/crt/arch-${ARCH}/crt0.S))
OFILES   := archive.o


INCLUDE_DIRS  += $(SOURCE_DIR)/src 

# Libraries required to build the target
LIBS := sel4 elf sel4c cpio lwip ethdrivers \
	 sel4sync serial nfs clock sel4cspace pawpaw

include $(SEL4_COMMON)/common.mk


${COMPONENTS}:
	false

archive.o: ${COMPONENTS}
	$(Q)mkdir -p $(dir $@)
	${COMMON_PATH}/files_to_obj.sh $@ _cpio_archive $^
