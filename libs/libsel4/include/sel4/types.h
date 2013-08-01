/* @LICENSE(NICTA_CORE) */

#ifndef __LIBSEL4_TYPES_H
#define __LIBSEL4_TYPES_H

#include <sel4/macros.h>
#include <sel4/arch/types.h>
#include <stdint.h>
#include <sel4/types_gen.h>
#include <sel4/syscall.h>

typedef enum {
    seL4_UntypedObject = 0,
    seL4_TCBObject,
    seL4_EndpointObject,
    seL4_AsyncEndpointObject,
    seL4_CapTableObject,
    seL4_NonArchObjectTypeCount,
    SEL4_FORCE_LONG_ENUM(seL4_ObjectType),
} seL4_ObjectType;

typedef enum {
    seL4_NoError = 0,
    seL4_InvalidArgument,
    seL4_InvalidCapability,
    seL4_IllegalOperation,
    seL4_RangeError,
    seL4_AlignmentError,
    seL4_FailedLookup,
    seL4_TruncatedMessage,
    seL4_DeleteFirst,
    seL4_RevokeFirst,
    seL4_NotEnoughMemory,
    SEL4_FORCE_LONG_ENUM(seL4_Error),
} seL4_Error;

typedef enum {
    seL4_NoFault = 0,
    seL4_CapFault,
    seL4_VMFault,
    seL4_UnknownSyscall,
    seL4_UserException,
    seL4_Interrupt,
    SEL4_FORCE_LONG_ENUM(seL4_FaultType),
} seL4_FaultType;

typedef enum {
    seL4_NoFailure = 0,
    seL4_InvalidRoot,
    seL4_MissingCapability,
    seL4_DepthMismatch,
    seL4_GuardMismatch,
    SEL4_FORCE_LONG_ENUM(seL4_LookupFailureType),
} seL4_LookupFailureType;

typedef enum {
    seL4_CanWrite = 0x01,
    seL4_CanRead = 0x02,
    seL4_CanGrant = 0x04,
    seL4_AllRights = seL4_CanWrite | seL4_CanRead | seL4_CanGrant,
    seL4_Transfer_Mint = 0x100,
    SEL4_FORCE_LONG_ENUM(seL4_CapRights),
} seL4_CapRights;

#define seL4_MsgMaxLength 120
#define seL4_MsgExtraCapBits 2
#define seL4_MsgMaxExtraCaps ((1<<seL4_MsgExtraCapBits)-1)
#define seL4_UntypedRetypeMaxObjects 256
#define seL4_GuardSizeBits 5
#define seL4_GuardBits 18
#define seL4_BadgeBits 28

typedef struct _seL4_IPCBuffer {
    seL4_MessageInfo_t tag;
    seL4_Word msg[seL4_MsgMaxLength];
    seL4_Word userData;
    union {
        seL4_CPtr caps[seL4_MsgMaxExtraCaps];
        seL4_CapData_t badges[seL4_MsgMaxExtraCaps];
    };
    seL4_CPtr receiveCNode;
    seL4_CPtr receiveIndex;
    seL4_Word receiveDepth;
} seL4_IPCBuffer __attribute__ ((__aligned__ (sizeof(struct _seL4_IPCBuffer))));

typedef seL4_CPtr seL4_CNode;
typedef seL4_CPtr seL4_IRQHandler;
typedef seL4_CPtr seL4_IRQControl;
typedef seL4_CPtr seL4_TCB;
typedef seL4_CPtr seL4_Untyped;

#define seL4_NilData seL4_CapData_Badge_new(0)

#include <sel4/arch/constants.h>

#endif
