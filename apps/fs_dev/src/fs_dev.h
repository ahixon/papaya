#ifndef __FS_DEV_H__
#define __FS_DEV_H__ 

#define FILESYSTEM_NAME     "dev"

seL4_CPtr service_ep;

struct ventry {
    char* name;
    seL4_CPtr vnode;
    int writing;

    struct ventry* next;
};

struct ventry* entries;

int vfs_open (struct pawpaw_event* evt);
int vfs_listdir (struct pawpaw_event* evt);
int vfs_stat (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   0,  0,  0   },      //  fs register info
    {   0,  0,  0   },      //  fs register cap
    {   0,  0,  0   },      //  mount
    {   vfs_open,           3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   }, 
    {   0,  0,  0   },      //  read 
    {   0,  0,  0   },      //  write
    {   0,  0,  0   },      //  close
    {   vfs_listdir,        3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_stat,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   }
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "fs_dev" };

#endif