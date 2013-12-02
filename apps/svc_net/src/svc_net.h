#ifndef __SVC_NET_H__
#define __SVC_NET_H__ 

#define NET_BUFFER_SIZE     0x3000

int netsvc_register (struct pawpaw_event* evt);
int netsvc_read (struct pawpaw_event* evt);
int netsvc_write (struct pawpaw_event* evt);

#define NUM_HANDLERS    4

struct pawpaw_eventhandler_info handlers[NUM_HANDLERS] = {
    {   netsvc_register,    3, HANDLER_REPLY                        },
    {   0,  0,  0   },      /* net unregister svc */
    {   netsvc_read,        1,  HANDLER_REPLY                       },
    {   netsvc_write,       3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    /* query state event might be nice */
};

struct pawpaw_event_table handler_table = { NUM_HANDLERS, handlers, "net" };

struct saved_data {
    struct pawpaw_share* share;
    struct pawpaw_cbuf* buffer;
    char* buffer_data;
    seL4_CPtr badge;
    seL4_CPtr cap;
    void* pcb;
    unsigned int id;    /* ID of registered network service */

    struct saved_data* next;
};

#endif