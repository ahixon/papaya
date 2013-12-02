#ifndef _BOOT_H_
#define _BOOT_H_

int boot_thread (void);

extern char _cpio_archive[];
extern seL4_CPtr rootserver_syscall_cap;

#define BOOT_LIST       "boot.txt"
#define BOOT_LIST_LINE  "\n"
#define BOOT_LIST_SEP   "="
#define BOOT_ARG_SEP    "\t "

#define BOOT_CMD_MOUNT  "mount "
#define BOOT_CMD_SWAP   "swap "
#define BOOT_CMD_PIN    "pin "

#define BOOT_TYPE_UNKNOWN   0
#define BOOT_TYPE_ASYNC     1
#define BOOT_TYPE_SYNC      2
#define BOOT_TYPE_BOOT      3
#define BOOT_TYPE_CMD_MOUNT 4
#define BOOT_TYPE_CMD_SWAP  5
#define BOOT_TYPE_CMD_PIN   6

int mount (char* mountpoint, char* fs);
int parse_fstab (char* path);
int open_swap (char* path);

extern struct as_region* share_reg;
extern seL4_CPtr share_badge;
extern seL4_Word share_id;

#endif /* _BOOT_H_ */