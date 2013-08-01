/* @LICENSE(NICTA_CORE) */

#include <unistd.h>

#define CPU_FREQUENCY (532UL * 1000000) /* MHz */

unsigned sleep(unsigned seconds) {
    /* XXX: This is *not* a good way to sleep. If you are the highest priority
     * thread in the system you will block everyone else while you are
     * sleeping. Difficult to sleep properly without kernel assistance.
     */
    while (seconds-- > 0) {
        /* Spin for a second. It's in asm so we know how many cycles it takes.
         * Volatile so the compiler doesn't optimise it out.
         */
        asm volatile ( "\tmov r1, %0\n"
                      "1:\n"
                      "\tcmp r1, #0\n"
                      "\tsubne r1, #1\n"
                      "\tbne 1b\n"
                     : /* no outputs */
                     :"r"(CPU_FREQUENCY / 3 /* number of instructions in the loop. */)
                     :"r1");
    }

    return 0;
}
