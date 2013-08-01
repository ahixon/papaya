/* @LICENSE(NICTA_CORE) */

#ifndef __LIBSEL4_ARCH_CONSTANTS_H
#define __LIBSEL4_ARCH_CONSTANTS_H

typedef enum {
    seL4_ARM_SmallPageObject = seL4_NonArchObjectTypeCount,
    seL4_ARM_LargePageObject,
    seL4_ARM_SectionObject,
    seL4_ARM_SuperSectionObject,
    seL4_ARM_PageTableObject,
    seL4_ARM_PageDirectoryObject,
    seL4_ObjectTypeCount
} seL4_ArchObjectType;

#endif
