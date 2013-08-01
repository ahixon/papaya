# @LICENSE(NICTA_CORE)

# Tools
BF_GEN_PATH := ${SOURCE_DIR}/tools/bitfield_gen.py
SYCALL_ID_GEN_PATH := ${SOURCE_DIR}/../../kernel/tools/syscall_header_gen.py
INVOCATION_ID_GEN_PATH := ${SOURCE_DIR}/../../kernel/tools/invocation_header_gen.py

# Targets
PRIORITY_TARGETS := include/interfaces/sel4_client.h
# include/sel4/types_gen.h include/sel4/syscall.h \
#					include/sel4/invocation.h arch_include/${ARCH}/sel4/arch/invocation.h
TARGETS          := libsel4.a

# Source files required to build the target
CFILES := \
	$(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))

# Header files/directories this library provides
# Note: sel4_client.h may not have been built at the time this is evaluated.
HDRFILES := \
	$(wildcard $(SOURCE_DIR)/include/*) \
	$(wildcard $(SOURCE_DIR)/arch_include/$(ARCH)/*) \
	$(BUILD_DIR)/include/sel4 \
	$(BUILD_DIR)/include/interfaces  #TODO proper prefix instruction

.DEFAULT_GOAL := default

install-headers: ${PRIORITY_TARGETS}

# Extra include directory for magpie to find type.h and sel4arch.idl4
INCLUDE_DIRS := include arch_include/$(ARCH)

include $(SEL4_COMMON)/common.mk

include/sel4/invocation.h: ${SOURCE_DIR}/include/interfaces/sel4.xml
	@echo "[INVOCATION_ID_GEN] $@"
	@python ${INVOCATION_ID_GEN_PATH} \
		--xml $< --libsel4 --dest $@

arch_include/${ARCH}/sel4/arch/invocation.h: ${SOURCE_DIR}/arch_include/${ARCH}/interfaces/sel4arch.xml 
	@echo "[INVOCATION_ID_GEN] $@"
	#@mkdir -p arch_include/${ARCH}/sel4/arch/
	@python ${INVOCATION_ID_GEN_PATH} \
		--xml $< --libsel4 --arch --dest ${SOURCE_DIR}/$@

# Header generator
include/sel4/syscall.h: ${SOURCE_DIR}/../../kernel/include/api/syscall.xsd \
						${SOURCE_DIR}/../../kernel/include/api/syscall.xml 
	@echo "[SYCALL_ID_GEN] $@ $^"
	@xmllint --noout --schema $^ 
	@python ${SYCALL_ID_GEN_PATH} \
	--xml ${SOURCE_DIR}/../include/api/syscall.xml \
	--libsel4_header $@

# Bitfield generator
include/sel4/types_gen.h: \
		${SOURCE_DIR}/include/sel4/types.bf \
		${BF_GEN_PATH}
	@echo "[BF_GEN] $@"
	@mkdir -p $(dir $@)
	@python ${BF_GEN_PATH} --no_c_includes --ndebug "${SOURCE_DIR}/include/sel4/types.bf" include/sel4/types_gen.h

# Stub generator
include/interfaces/sel4_client.h: \
		${SOURCE_DIR}/arch_include/${ARCH}/interfaces/sel4arch.xml \
		${SOURCE_DIR}/include/interfaces/sel4.xml
	@echo "[STUB_GEN] $@"
	@mkdir -p $(dir $@)
	@python ${SOURCE_DIR}/tools/syscall_stub_gen.py -a $(ARCH) -o $@ $^
