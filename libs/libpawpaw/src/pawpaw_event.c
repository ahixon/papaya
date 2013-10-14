#include <sel4/sel4.h>
#include <pawpaw.h>
#include <stdlib.h>

void pawpaw_event_loop (struct pawpaw_event_table* table, seL4_CPtr ep) {
	while (1) {
        seL4_Word badge = 0;
        seL4_MessageInfo_t msg = seL4_Wait (ep, &badge);

        struct pawpaw_event* evt = pawpaw_event_create (msg, badge);
        if (!evt) {
        	/* silently drop event */
        	pawpaw_event_dispose (evt);
        	continue;
        }

        /* only process valid events, and ignore everything else */
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

	return evt;
}

void pawpaw_event_dispose (struct pawpaw_event* evt) {
	if (evt->reply_cap) {
		pawpaw_cspace_free_slot (evt->reply_cap);
	}

	if (evt->args) {
		free (evt->args);
	}

	free (evt);
}

int pawpaw_event_process (struct pawpaw_event_table* table, struct pawpaw_event *evt, seL4_CPtr (*save_reply_func)(void)) {
	unsigned int argc = seL4_MessageInfo_get_length (evt->msg);
    if (argc < 1) {
    	return PAWPAW_EVENT_INVALID;
    }

	unsigned int evt_id = seL4_GetMR (0);
	if (evt_id >= table->num_events) {
        return PAWPAW_EVENT_INVALID;
    }

    struct pawpaw_eventhandler_info eh = table->handlers[evt_id];
    argc--; /* since we don't want syscall ID as an arg to the func */

    /* event is valid, but no function handler defined */
    if (eh.func == 0) {
        return PAWPAW_EVENT_UNHANDLED;
    }

   	/* bad argument count */
    if (argc != eh.argcount) {
    	return PAWPAW_EVENT_INVALID;
    }

    /* grab arguments */
    if (argc > 0) {
	    evt->args = malloc (argc * sizeof (seL4_Word));
	    for (int i = 0; i < argc; i++) {
	    	evt->args[i] = seL4_GetMR (i + 1);
	    }
	}

	/* get the reply cap
	 * NOTE: we do this after we grab arguments since pawpaw_save_reply might call
	 * the root server itself, thus overwriting the message registers */
    if (eh.requires_reply) {
    	evt->reply_cap = save_reply_func ();
    	if (!(evt->reply_cap)) {
    		return PAWPAW_EVENT_INVALID;
    	}
    }

    /* ok call the event */
    return eh.func (evt);
}