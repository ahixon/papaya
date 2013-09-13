#include <sel4/sel4.h>
#include <stdlib.h>

#include <pawpaw.h>

int pawpaw_mbox_init (int size) {

}

/* register a mailbox with a given client */
void pawpaw_mbox_register (seL4_CPtr client, int mbox) {

}

inline void* pawpaw_mbox_get (int mbox) {
	return (void*)(PAPAYA_MBOX_VBASE + (mbox * PAPAYA_MBOX_SIZE));
}

/* saves the mailbox out into a regular frame 
 * REQUIRES A SYSCALL but no copy time
 * requires that there's at least a page left in the address space */
pawpaw_mbox_save ()