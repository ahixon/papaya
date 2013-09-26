#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>

#define TESTBIN_PATH ""         /* should include trailing separator */
#define PATHBUF 1024

void start_and_wait (const char* path) {
    printf ("\tstarting %s...\n", path);
    pid_t pid = process_create (path);
    assert (pid > 0);   /* process 0 should be root server */

    printf ("\tprocess started, waiting for exit\n");
    pid_t died = process_wait (pid);
    assert (died == pid);   /* ensure didn't die because parent died */
}

int main(void) {
    printf ("test_runner: hello!\n");

    start_and_wait (TESTBIN_PATH "test_vm");

    printf ("test_runner: all tests passed! YOU ARE AWESOME!\n");
}
