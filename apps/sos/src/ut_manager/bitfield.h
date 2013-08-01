

#ifndef _BITFIELD_H_
#define _BITFIELD_H_


typedef struct {
    int next_free;
    int available;
    int size;
    char* b;
} bitfield_t;

enum bf_init_state {
    BITFIELD_INIT_FILLED,
    BITFIELD_INIT_EMPTY
};

bitfield_t* new_bitfield(int size, enum bf_init_state state);
void destroy_bitfield(bitfield_t* bf);

int bf_set_next_free(bitfield_t* bitfield);

void bf_set(bitfield_t* bitfield, int offset);
void bf_clr(bitfield_t* bitfield, int offset);
int bf_get(const bitfield_t* field, int offset);


#endif
