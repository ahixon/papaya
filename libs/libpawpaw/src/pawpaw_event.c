#include <sel4/sel4.h>
#include <pawpaw.h>
#include <stdlib.h>
#include <stdio.h>

static seL4_CPtr recv_cap = 0;

static void pawpaw_event_new_recv_cap (void) {
    recv_cap = pawpaw_cspace_alloc_slot ();
    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, recv_cap, PAPAYA_CSPACE_DEPTH);
}

void pawpaw_event_init (void) {
    pawpaw_event_new_recv_cap ();
}

seL4_CPtr pawpaw_event_get_recv_cap (void) {
    seL4_CPtr ret = recv_cap;
    pawpaw_event_new_recv_cap ();
    return ret;
}

void pawpaw_event_loop (struct pawpaw_event_table* table, void (*interrupt_func)(struct pawpaw_event* evt), seL4_CPtr ep) {
	while (1) {
        seL4_Word badge = 0;
        seL4_MessageInfo_t msg = seL4_Wait (ep, &badge);

        seL4_Word label = seL4_MessageInfo_get_label (msg);
        if (!(label == seL4_NoFault || (label == seL4_Interrupt && interrupt_func))) {
            printf ("%s: junking incoming was wasn't NoFault or Interrupt with handler\n", __FUNCTION__);
            continue;
        }

        struct pawpaw_event* evt = pawpaw_event_create (msg, badge);
        if (!evt) {
        	/* silently drop event */
        	pawpaw_event_dispose (evt);
        	continue;
        }

        evt->table = table;

        if (label == seL4_Interrupt) {
            evt->args = malloc (sizeof (seL4_Word));
            evt->args[0] = seL4_GetMR (0);

            interrupt_func (evt);
            pawpaw_event_dispose (evt);
        } else {
            int result = pawpaw_event_process (table, evt, pawpaw_save_reply);

            switch (result) {
                case PAWPAW_EVENT_NEEDS_REPLY:
                    seL4_Send (evt->reply_cap, evt->reply);
                    pawpaw_event_dispose (evt); 
                    break;
                case PAWPAW_EVENT_HANDLED:
                    pawpaw_event_dispose (evt);
                    break;
                case PAWPAW_EVENT_HANDLED_SAVED:
                    /* don't dispose event since it's still used somewhere */
                    break;
                default:
                    /* unknown or unhandled event response */
                    break;
            }
        }
    }
}

struct pawpaw_event* pawpaw_event_create (seL4_MessageInfo_t msg, seL4_Word badge) {
	struct pawpaw_event* evt = malloc (sizeof (struct pawpaw_event));
	if (!evt) {
		return NULL;
	}

	evt->badge = badge;
	evt->msg = msg;
	evt->reply_cap = 0;
	evt->flags = PAWPAW_EVENT_UNHANDLED;
	evt->reply.words[0] = 0;	/* oh my god seL4 whyyyy */
	evt->args = NULL;
    evt->share = NULL;

	return evt;
}

void pawpaw_event_dispose (struct pawpaw_event* evt) {
	if (evt->reply_cap) {
		pawpaw_cspace_free_slot (evt->reply_cap);
	}

	if (evt->args) {
		free (evt->args);
	}

    if (evt->share) {
        if (evt->flags & PAWPAW_EVENT_UNMOUNT) {
            printf ("%s: had share 0x%x @ %p, unmounting\n", evt->table->app_name, evt->share->id, evt->share->buf);
            pawpaw_share_unset (evt->share);
            pawpaw_share_unmount (evt->share);
        } else {
            //printf ("%s: had share 0x%x @ %p, but NOT UNMOUNTING\n", evt->table->app_name, evt->share->id, evt->share->buf);
        }
    }

	free (evt);
}

int pawpaw_event_process (struct pawpaw_event_table* table, struct pawpaw_event *evt, seL4_CPtr (*save_reply_func)(void)) {
	unsigned int argc = seL4_MessageInfo_get_length (evt->msg);
    if (argc < 1) {
    	return PAWPAW_EVENT_INVALID;
    }

    unsigned int arg_offset = 1;
	unsigned int evt_id = seL4_GetMR (0);
	if (evt_id >= table->num_events) {
        printf ("%s: junking incoming request as function ID %d was beyond table size %d events (last idx %d)\n", __FUNCTION__, evt_id, table->num_events, table->num_events - 1);
        return PAWPAW_EVENT_INVALID;
    }

    struct pawpaw_eventhandler_info eh = table->handlers[evt_id];
    argc--; /* since we don't want syscall ID as an arg to the func */

    /* event is valid, but no function handler defined */
    if (eh.func == 0) {
        printf ("%s: %s: junking incoming request 0x%x as function handler was NULL\n", table->app_name, __FUNCTION__, evt_id);
        return PAWPAW_EVENT_UNHANDLED;
    }

   	/* bad argument count */
    if (argc != eh.argcount) {
        printf ("%s: %s: junking incoming request as argument count did not match\n", table->app_name, __FUNCTION__);
    	return PAWPAW_EVENT_INVALID;
    }

    /* see if we need to automount - thus the first arg should
     * be a ID of a share, and if a cap provided, a share to initially
     * mount */
    seL4_Word share_id = 0;
    if (eh.flags & HANDLER_AUTOMOUNT) {
        argc--;
        arg_offset = 2;
        share_id = seL4_GetMR (1);
    }

    /* grab arguments */
    if (argc > 0) {
	    evt->args = malloc (argc * sizeof (seL4_Word));
	    for (unsigned int i = 0; i < argc; i++) {
	    	evt->args[i] = seL4_GetMR (arg_offset + i);
	    }
	}

    /* store the reply cap
     * NOTE: we do this and mounting shares after we grab arguments since
     * pawpaw_save_reply might call the root server itself, thus overwriting
     * the message registers */
    if (eh.flags & HANDLER_REPLY) {
        evt->reply_cap = save_reply_func ();
        if (!(evt->reply_cap)) {
            printf ("%s: %s: junking since failed to save reply cap\n", table->app_name, __FUNCTION__);
            return PAWPAW_EVENT_INVALID;
        }
    }

    if (eh.flags & HANDLER_AUTOMOUNT) {

        if (seL4_MessageInfo_get_extraCaps (evt->msg) > 0) {
            //printf ("%s: mounting share from cap\n", table->app_name);
            evt->share = pawpaw_share_mount (recv_cap);
            if (evt->share) {
                //printf ("\thad ID 0x%x\n", evt->share->id);
                pawpaw_share_set (evt->share);
            }

            pawpaw_event_new_recv_cap ();
        } else {
            printf ("%s: mounting share from ID 0x%x\n", table->app_name, share_id);
            evt->share = pawpaw_share_get (share_id);
            if (!evt->share) {
                printf ("%s: share cap missing, even though was automount\n", __FUNCTION__);
                evt->share = NULL;
            }
        }
    } else {
        evt->share = NULL;
    }

    /* ok call the event */
    //printf ("%s: handling function id %d\n", table->app_name, evt_id);
    int res = eh.func (evt);
    //printf ("%s: finished syscall\n", table->app_name);
    return res;
}