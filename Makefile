# @LICENSE(NICTA_CORE)

# Top level makefile.
# Type "make <thing to build>" in this directory and it will be built.

TFTPROOT = /var/tftpboot/$(USER)

DEFINES += SOS_NFS_DIR='"$(TFTPROOT)"'

ifeq ($(OS), Darwin)
	SERIAL_PORT = $(firstword $(wildcard /dev/cu.usbserial-*))
else
	SERIAL_PORT = $(firstword $(wildcard /dev/ttyUSB*))
endif

REMOTEUSER = alex
REMOTEPATH = /var/tftpboot/alex
REMOTEHOST = vodka.alexhixon.com
ifeq ($(shell hostname), coke)
	REMOTEHOST = vodka.local
endif


-include .config


include tools/common/project.mk


all: app-images
	mkdir -p $(TFTPROOT)
	cp -v $(IMAGE_ROOT)/sos-image-arm-imx6 $(TFTPROOT)/bootimg.elf
	$(MAKE) reset

.PHONY: reset
ifeq ($(SERIAL_PORT),)
reset:
	@echo "Uploading to $(REMOTEHOST) and resetting..."
	scp $(TFTPROOT)/bootimg.elf $(REMOTEUSER)@$(REMOTEHOST):$(REMOTEPATH)/bootimg.elf
	ssh $(REMOTEUSER)@$(REMOTEHOST) ./aos/run.sh
else
reset:
	@echo "Resetting sabre @ $(SERIAL_PORT)"
	@echo "reset" >> $(SERIAL_PORT)
endif



.PHONY: docs
docs: common
	mkdir -p docs
	@for doc in $(libdocs-y);                  \
	do                                         \
	  set -e;                                  \
	  echo "Building docs for $$doc";          \
	  $(MAKE) --directory "libs/$$doc" docs;   \
	  cp "libs/$$doc/docs/$$doc.pdf" ./docs;   \
	done



