# @LICENSE(NICTA_CORE)

# Targets
TARGETS := liblwip.a

LWIP_IPV := ipv4
#LWIP_IPV := ipv6

LWIP_DIR := lwip-1.4.1
LWIP_PATH := $(SOURCE_DIR)/$(LWIP_DIR)/src

# Source files required to build the target
CFILES := $(patsubst ${SOURCE_DIR}/%,%,$(wildcard ${LWIP_PATH}/*/*.c))
CFILES += $(patsubst ${SOURCE_DIR}/%,%,$(wildcard ${LWIP_PATH}/core/snmp/*.c))
CFILES += $(patsubst ${SOURCE_DIR}/%,%,$(wildcard ${LWIP_PATH}/core/${LWIP_IPV}/*.c))

# Header files/directories this library provides
# LwIP core headers
HDRFILES := $(wildcard ${LWIP_PATH}/include/*)
HDRFILES += $(wildcard ${LWIP_PATH}/include/$(LWIP_IPV)/*)
HDRFILES += $(wildcard ${LWIP_PATH}/include/netif/*)
# Configuration headers
HDRFILES += $(wildcard ${SOURCE_DIR}/include/*)


include $(SEL4_COMMON)/common.mk
