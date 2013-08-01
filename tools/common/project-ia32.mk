# @LICENSE(NICTA_CORE)

# Some of the targets in here use bashisms, so force bash as the interpreter.
SHELL=/bin/bash
ifeq ($(wildcard ${SHELL}),)
$(error Prerequisite ${SHELL} not found)
endif

export SEL4_CMDLINE="console=0x3f8 debug=0x2f8 max_num_nodes=8"
PHONY += harddisk-images
harddisk-images: $(patsubst %,%-harddisk-image,$(apps))

%-harddisk-image: % common kernel_elf FORCE
	@echo "[COBBLER] $@"
	$(Q)$(SEL4_COMMON)/cobbler -k "$(STAGE_BASE)/kernel.elf" -a $(SEL4_CMDLINE) \
		-o "$(IMAGE_ROOT)/$@-$(ARCH)-$(PLAT)" "$(STAGE_BASE)/bin/$<" 2>&1 \
		| while read line; do echo " [COBBLER] $$line"; done; \
		exit $${PIPESTATUS[0]}

%-image: % kernel_elf common FORCE
	@echo "[GEN_IMAGE] $@"
	$(Q)cp -f "$(STAGE_BASE)/kernel.elf" "$(IMAGE_ROOT)/kernel-$(ARCH)-$(PLAT)"
	$(Q)cp -f "$(STAGE_BASE)/bin/$<" "$(IMAGE_ROOT)/$@-$(ARCH)-$(PLAT)"

#New target to make using capDL-loader
capDL-$(ARCH)-$(PLAT): capDL-loader kernel_elf common FORCE
	@echo "[GEN_IMAGE] $@"
	$(Q)$(call cp_if_changed, "$(STAGE_BASE)/kernel.elf", \
		"$(IMAGE_ROOT)/kernel-$(ARCH)-$(PLAT)")
	$(Q)$(call cp_if_changed, "$(STAGE_BASE)/bin/$<", \
		"$(IMAGE_ROOT)/$@")


