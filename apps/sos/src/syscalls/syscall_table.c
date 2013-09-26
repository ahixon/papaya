#include <sel4/sel4.h>
#include <thread.h>

#include <syscalls/syscall_table.h>
#include <syscalls.h>

/* THESE MUST BE KEPT IN ORDER WITH SYSCALL.H */
const struct syscall_info syscall_table[NUM_SYSCALLS] = {
    { syscall_sbrk,             1,  true  },
    { syscall_service_find,     3,  true  },
    { syscall_service_register, 1,  true  },
    { syscall_register_irq,     1,  true  },
    { syscall_map_device,       2,  true  },
    { syscall_alloc_cnodes,     1,  true  },
    { syscall_create_ep_sync,   0,  true  },
    { syscall_create_ep_async,  0,  true  },
    { syscall_bind_async_tcb,   1,  true  },
    { syscall_sbuf_create,      1,  true  },
    { syscall_sbuf_mount,       1,  true  },
    { syscall_suicide,          0,  false },
};