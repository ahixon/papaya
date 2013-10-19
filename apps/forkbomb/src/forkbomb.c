#include <sos.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    //int me = my_pid();
    printf("Process here to clobber your frames\n");
    int pid = process_create("forkbomb");
    printf("Going to wait\n");
    process_wait(pid);

    return EXIT_SUCCESS;
}
