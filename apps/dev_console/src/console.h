#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <pawpaw.h>

#define CONSOLE_BUF_SIZE        1024
#define CONSOLE_PORT            26706
#define CONSOLE_PRODUCT_ID      1337

int vfs_open (struct pawpaw_event* evt);
int vfs_read (struct pawpaw_event* evt);
int vfs_write (struct pawpaw_event* evt);
int vfs_close (struct pawpaw_event* evt);

void interrupt_handler (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   0,  0,  0   },      //   RESERVED   //
    {   0,  0,  0   },      //   RESERVED   //
    {   0,  0,  0   },      //              //
    {   vfs_open,           2,  HANDLER_REPLY                       },
    {   vfs_read,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_write,          2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_close,          0,  HANDLER_REPLY                       },
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "console" };

struct fhandle* current_reader = NULL;

struct fhandle {
    struct pawpaw_event* current_event;
    seL4_Word id;
    int mode;

    struct fhandle* next;
};


#endif