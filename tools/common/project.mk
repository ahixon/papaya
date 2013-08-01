# @LICENSE(NICTA_CORE)

# This file should be included from the top-level project makefile.
# It is responsible for invoking the build systems of all other components.

# Builds components in the correct order such that their dependencies are
# satisfied.

PWD := $(shell pwd)
KERNEL_ROOT_PATH := ${PWD}/kernel
COMMON_PATH := ${PWD}/tools/common
SEL4_LIBS_PATH = ${PWD}/libs
SEL4_APPS_PATH = ${PWD}/apps
SEL4_MODULES_PATH = ${PWD}/modules
DATA_PATH = ${PWD}/data
KMAKE_FLAGS = $(COMMON_PATH)/Makefile.flags
AUTOCONF_H_FILE := include/generated/autoconf.h

export KERNEL_ROOT_PATH COMMON_PATH SEL4_LIBS_PATH SEL4_APPS_PATH SEL4_MODULES_PATH DATA_PATH

export BUILD_ROOT=${PWD}/build

KBUILD_SCRIPTS = tools/kbuild

PHONY :=

makefile_name := Makefile

### Verbose building
########################################

# Set V=1 for verbose building, this can be passed in on the command line
# Set V=2 to have make echo out commands before executing them

ifeq ($V, 1)
	BUILD_VERBOSE = 1
	MAKE_SILENT = -s
	quiet =
	Q =
else
ifeq ($V, 2)
	BUILD_VERBOSE = 1
	MAKE_SILENT =
	quiet =
	Q =
else
ifeq ($V, 3)
	BUILD_VERBOSE = 1
	MAKE_SILENT =
	quiet =
	Q =
else
	MAKE_SILENT = -s
	quiet = quiet_
	Q = @
endif
endif
endif

ifeq ($(KBUILD_SRC),)

ifdef O
	ifeq ("$(origin O)", "command line")
		KBUILD_OUTPUT := $(O)
	endif
endif

PHONY += _all
_all:

ifneq ($(KBUILD_OUTPUT),)

saved-output := $(KBUILD_OUTPUT)
KBUILD_OUTPUT := $(shell cd $(KBUILD_OUTPUT) && /bin/pwd)
$(if $(KBUILD_OUTPUT),, \
	$(error output directory "$(saved-output)" does not exist))
PHONY += $(MAKECMDGOALS)
$(filter-out _all,$(MAKECMDGOALS)) _all:
	$(if $(BUILD_VERBOSE:1=),@)$(MAKE) -C $(KBUILD_OUTPUT) \
		KBUILD_SRC=$(CURDIR)	\
		-f $(CURDIR)/$(makefile_name) $@
skip-makefile := 1

endif # ifneq ($(KBUILD_OUTPUT),)
endif # ifeq ($(KBUILD_SRC),)

ifeq ($(skip-makefile),)

PHONY += all

_all: all

srctree := $(if $(KBUILD_SRC),$(KBUILD_SRC),$(CURDIR))
objtree := $(CURDIR)
src		:= $(srctree)
obj		:= $(objtree)
export srctree objtree src obj

# reassign the environment variables to correct path
KERNEL_ROOT_PATH = $(srctree)/kernel
COMMON_PATH = $(srctree)/tools/common
TOOLS_ROOT = $(srctree)/tools
LIBS_ROOT = $(srctree)/libs
APPS_ROOT = $(srctree)/apps
MODULES_ROOT = $(srctree)/modules
SEL4_LIBS_PATH = $(srctree)/libs
SEL4_APPS_PATH = $(srctree)/apps
SEL4_MODULES_PATH = $(srctree)/modules
KMAKE_FLAGS = $(COMMON_PATH)/Makefile.flags

export COMMON_PATH

# build output dirs
BUILD_ROOT = $(objtree)/build
STAGE_ROOT = $(objtree)/stage
KBUILD_ROOT =$(BUILD_ROOT)/kernel
IMAGE_ROOT = $(objtree)/images
STAGE_BASE = $(STAGE_ROOT)/$(ARCH)/$(PLAT)
BUILD_BASE = $(BUILD_ROOT)/$(ARCH)/$(PLAT)


# Linux expects CROSS_COMPILE to contain the cross compiler prefix
CROSS_COMPILE ?=
ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := $(shell grep ^CONFIG_CROSS_COMPILER_PREFIX .config 2>/dev/null)
CROSS_COMPILE := $(subst CONFIG_CROSS_COMPILER_PREFIX=,,$(CROSS_COMPILE))
CROSS_COMPILE := $(subst ",,$(CROSS_COMPILE))
#")
export CROSS_COMPILE
endif


CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
		else if [ -x /bin/bash ]; then echo /bin/bash; \
		else echo sh; fi ; fi)


KBUILD_MODULES :=
KBUILD_BUILTIN := 1
KBUILD_CHECKSRC = 0

MAKEFLAGS += --include-dir=$(srctree)

HOSTCC			= gcc
HOSTCFLAGS		:=
HOSTCFLAGS      += -Wall
MAKEFLAGS	+= -rR

export HOSTCC HOSTCFLAGS MAKEFLAGS

AS	= $(CROSS_COMPILE)as
CC	= $(CROSS_COMPILE)gcc
LD	= $(CROSS_COMPILE)ld -nostdlib
CPP	= $(CROSS_COMPILE)cpp
AR	= $(CROSS_COMPILE)ar
NM	= $(CROSS_COMPILE)nm

CFLAGS		:= -std=gnu99
CPPFLAGS	:=
AFLAGS		:=
LDFLAGS		:=

export AS CC LD CPP AR NM CFLAGS CPPFLAGS AFLAGS LDFLAGS

PHONY += scripts_basic
scripts_basic:
	$(MAKE) $(build)=$(KBUILD_SCRIPTS)/basic

include $(KBUILD_SCRIPTS)/Kbuild.include

$(KBUILD_SCRIPTS)/basic/%: scripts_basic;

PHONY += gen_makefile
gen_makefile:
	@echo "[GEN] $(objtree)/$(makefile_name)"
ifneq ($(KBUILD_SRC),)
	$(Q)$(CONFIG_SHELL) $(KBUILD_SCRIPTS)/mkmakefile \
		$(srctree) $(objtree) $(VERSION) $(PATCHLEVEL) $(makefile_name)
endif

PHONY += config
config: gen_makefile scripts_basic
	$(Q)mkdir -p include
	$(Q)$(MAKE) $(MAKE_SILENT) $(build)=$(KBUILD_SCRIPTS)/kconfig $@

%config: gen_makefile scripts_basic FORCE
	$(Q)mkdir -p include
	$(Q)mkdir -p include/config
	$(Q)$(MAKE) $(MAKE_SILENT) $(build)=$(KBUILD_SCRIPTS)/kconfig $@

-include .config

.config: ;

include $(KMAKE_FLAGS)
include $(SEL4_LIBS_PATH)/Kbuild
#
# If 'capDL loader boot' is selected then we are
# building a system using the driver framework.
#
ifdef CONFIG_CAPDL_LOADER_BOOT
include $(SEL4_MODULES_PATH)/Kbuild
modules = $(modules-y)
include $(COMMON_PATH)/capdl.mk
endif
include $(SEL4_APPS_PATH)/Kbuild

libs = $(libs-y)
apps = $(apps-y)
components = $(components-y)

$(AUTOCONF_H_FILE): .config
	$(Q)mkdir -p include/config
	$(Q)mkdir -p include/generated
	$(Q)$(MAKE) $(MAKE_SILENT) -f $(srctree)/$(makefile_name) silentoldconfig

.SECONDEXPANSION:
all: .config kernel_elf

cp_if_changed = \
	@set -e; $(echo-cmd)  \
	cmp -s $(1) $(2) || (cp $(1) $(2) && echo -n ' '; echo -n '[STAGE] '; basename $(2);)

$(KBUILD_ROOT)/.config: .config
	$(Q)mkdir -p $(KBUILD_ROOT)
	$(Q)cp .config $(KBUILD_ROOT)

PHONY += kernel_elf
kernel_elf: setup $(KBUILD_ROOT)/.config
	@echo "[KERNEL]"
	$(Q)mkdir -p $(KBUILD_ROOT)
	$(Q)$(MAKE) $(MAKE_SILENT) -C $(KBUILD_ROOT) -f $(KERNEL_ROOT_PATH)/Makefile \
		SOURCE_ROOT=$(KERNEL_ROOT_PATH) O=$(KBUILD_ROOT) V=$(V) \
		TOOLPREFIX=$(CONFIG_CROSS_COMPILER_PREFIX:"%"=%) \
		CONFIG_DOMAIN_SCHEDULE=${CONFIG_DOMAIN_SCHEDULE} \

	$(call cp_if_changed, $(KBUILD_ROOT)/kernel.elf,$(STAGE_BASE)/kernel.elf)
	@echo "[KERNEL] done."

PHONY += setup
setup: $(AUTOCONF_H_FILE)
	$(Q)mkdir -p ${STAGE_BASE}
	$(Q)mkdir -p ${BUILD_BASE}
	$(Q)mkdir -p ${IMAGE_ROOT}

PHONY += common
common: setup
	$(Q)cp -ur $(TOOLS_ROOT)/$@ $(STAGE_BASE)

export SEL4_COMMON=$(STAGE_BASE)/common

ifeq ($(ARCH),arm)
include $(COMMON_PATH)/project-arm.mk
endif

ifeq ($(ARCH),ia32)
include $(COMMON_PATH)/project-ia32.mk
endif

PHONY += dite
dite:
	@echo "[DITE] building..."
	$(Q)$(MAKE) --no-print-directory --directory=$(TOOLS_ROOT)/dite \
		STAGING_ROOT=$(BUILD_ROOT)/dite BUILD_ROOT=$(STAGE_ROOT)/dite
	@echo "[DITE] done."

$(modules-):
	$(error Module "$@" was not selected in .config)

PHONY += $(libs)
$(libs):
	@echo "[libs/$@] building..."
	$(Q)mkdir -p $(BUILD_BASE)/$@
	$(Q)CFLAGS= LDFLAGS= $(MAKE) $(MAKE_SILENT) -C $(BUILD_BASE)/$@ -f $(srctree)/.config -f $(LIBS_ROOT)/$@/Makefile \
		BUILD_DIR=$(BUILD_BASE)/$@ SOURCE_DIR=$(LIBS_ROOT)/$@ V=$(V) \
		STAGE_DIR=$(STAGE_BASE) \
		TOOLPREFIX=$(CONFIG_CROSS_COMPILER_PREFIX:"%"=%)
	@echo "[libs/$@] done."

PHONY += app-images
app-images: $(patsubst %,%-image,$(apps))

PHONY += $(apps) $(components)
$(apps) $(components): common
	@echo "[apps/$@] building..."
	$(Q)mkdir -p $(BUILD_BASE)/$@
	$(Q)CFLAGS= LDFLAGS= $(MAKE) $(MAKE_SILENT) -C $(BUILD_BASE)/$@ -f $(srctree)/.config -f $(APPS_ROOT)/$@/Makefile \
		BUILD_DIR=$(BUILD_BASE)/$@ SOURCE_DIR=$(APPS_ROOT)/$@ V=$(V) \
		STAGE_DIR=$(STAGE_BASE) \
		TOOLPREFIX=$(CONFIG_CROSS_COMPILER_PREFIX:"%"=%)
	@echo "[apps/$@] done."

PHONY += module-images
module-images: $(patsubst %,%-image,$(modules)) $(libs)

PHONY += $(modules)
$(modules): common
	@echo "[modules/$@] building..."
	$(Q)mkdir -p $(BUILD_BASE)/$@
	$(Q)CFLAGS= LDFLAGS= $(MAKE) $(MAKE_SILENT) -C $(BUILD_BASE)/$@ -f $(srctree)/.config -f $(MODULES_ROOT)/$@/Makefile \
		BUILD_DIR=$(BUILD_BASE)/$@ SOURCE_DIR=$(MODULES_ROOT)/$@ V=$(V) \
		STAGE_DIR=$(STAGE_BASE) \
		TOOLPREFIX=$(CONFIG_CROSS_COMPILER_PREFIX:"%"=%)
	@echo "[modules/$@] done."

PHONY += clean
clean:
	@echo "[CLEAN] in $(PWD)"
	@if [ -d $(BUILD_ROOT) ]; then echo " [BUILD] $(BUILD_ROOT)"; \
		rm -fr $(BUILD_ROOT); fi
	@if [ -d $(STAGE_ROOT) ]; then echo " [STAGE] `basename $(STAGE_ROOT)`"; \
		rm -fr $(STAGE_ROOT); fi
# delete the Kconfig generated include files
	@if [ -d include/generated ]; then echo " [INCLUDE] $(PWD)/include/generated"; \
		rm -fr include/generated; fi
	@if [ -d include/config ]; then echo " [INCLUDE] $(PWD)/include/config"; \
		rm -fr include/config; fi
# if the only files in include are the Kconfig ones, then this should work silently
# otherwise it should fail
	@if [ -d include ]; then echo " [INCLUDE] $(PWD)/include"; \
		rmdir --ignore-fail-on-non-empty include > /dev/null 2>&1; fi

PHONY += mrproper
mrproper: clean
	@if [ -d $(IMAGE_ROOT) ]; then echo " [IMAGE_ROOT] $(IMAGE_ROOT)"; \
		rm -fr $(IMAGE_ROOT); fi
	@if [ -e $(PWD)/.config ]; then echo " [CLEAN] .config"; \
		rm -f .config; fi
	@if [ -e $(PWD)/.config.old ]; then echo " [CLEAN] .config.old"; \
		rm -f .config.old; fi

PHONY += style
style:
	@if [ ! -e "${STYLE_PATH}" ]; then \
		echo "Specify a STYLE_PATH to style i.e. STYLE_PATH=apps/sel4test"; \
		exit 1; \
	else \
		astyle --options=tools/common/astylerc --recursive "${PWD}/${STYLE_PATH}/*.c" "${PWD}/${STYLE_PATH}/*.h"; \
	fi

PHONY += FORCE

.PHONY: $(PHONY)

endif # end of skip-makefile
