#include <sync/mutex.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>

extern void* sync_new_ep(seL4_CPtr* ep);
extern void sync_free_ep(void* ep);

struct sync_mutex_ {
    void* ep;
    seL4_CPtr mapping;
};

sync_mutex_t sync_create_mutex() {
    sync_mutex_t mutex;

    mutex = (sync_mutex_t) malloc(sizeof(struct sync_mutex_));
    if (!mutex)
        return NULL;

    mutex->ep = sync_new_ep(&mutex->mapping);
    if(mutex->ep == NULL){
        free(mutex);
        return NULL;
    }

    // Prime the endpoint
    seL4_Notify(mutex->mapping, 1);
    return mutex;
}

void sync_destroy_mutex(sync_mutex_t mutex) {
    sync_free_ep(mutex->ep);
    free(mutex);
}
void sync_acquire(sync_mutex_t mutex) {
    seL4_Word badge;
    assert(mutex);
    seL4_MessageInfo_t msginfo = seL4_Wait(mutex->mapping, &badge);
    // We should *only* be woken up by an async notify
    assert(seL4_MessageInfo_get_label(msginfo)==seL4_Interrupt);
}

void sync_release(sync_mutex_t mutex) {
    // Wake the next guy up
    seL4_Notify(mutex->mapping, 1);
}

int sync_try_acquire(sync_mutex_t mutex) {
    seL4_Word badge;
    seL4_MessageInfo_t info = seL4_Poll(mutex->mapping, &badge);
    return seL4_MessageInfo_get_label(info) == seL4_Interrupt;
}
