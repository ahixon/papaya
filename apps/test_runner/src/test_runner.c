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
    printf ("\tstarting '%s'...\n", path);
    pid_t pid = process_create (path);
    assert (pid > 0);   /* process 0 should be root server */

    printf ("\tprocess started with PID %d, waiting for exit\n", pid);
    pid_t died = process_wait (pid);
    assert (died == pid);   /* ensure didn't die because parent died */
}

int main(void) {
    printf ("test_runner: hello!\n");

    start_and_wait (TESTBIN_PATH "sos_test");

   	printf ("gonna die\n");
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, SYSCALL_PROCESS_CREATE);
    seL4_SetMR (1, 0x9fffffdc);
    seL4_SetMR (2, 0x2000);	/* 2 pages */

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return seL4_GetMR (0);

    printf ("test_runner: all tests passed! YOU ARE AWESOME!\n");
}
