#include "debug.h"
void
dump_regs(uint32_t* start, int size){
    int i, j;
    for(i = 0; i < size/sizeof(*start); ){
        printf("+0x%03x: ",((uint32_t)start)&0xfff);
        for(j = 0; j < 4; j++, i++, start++){
            printf("0x%08x ", *start);
        }
        printf("\n");
    }
}


