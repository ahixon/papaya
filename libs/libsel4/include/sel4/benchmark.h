#ifndef LIBSEL4_BENCHMARK
#define LIBSEL4_BENCHMARK

#ifdef CONFIG_BENCHMARK

#define MAX_IPC_BUFFER 1020
#define IPC_OFFSET 4

#include <sel4/sel4.h>

static inline void
seL4_BenchmarkDumpFullLog()
{
    uint32_t potential_size = seL4_BenchmarkLogSize();

    for (uint32_t j = 0; j < potential_size; j += MAX_IPC_BUFFER) {
        uint32_t chunk = potential_size - j;
        uint32_t requested = chunk > MAX_IPC_BUFFER ? MAX_IPC_BUFFER : chunk;
        uint32_t recorded = seL4_BenchmarkDumpLog(j, requested);
        for (uint32_t i = IPC_OFFSET; i < recorded; i++) {
            printf("%u ", seL4_GetMR(i));
        }
        printf("\n");
        /* we filled the log buffer */
        if (requested != recorded) {
            printf("Dumped %u of %u potential logs\n", j + recorded, potential_size);
            return;
        }
    }

    /* logged amount was smaller than log buffer */
    printf("Dumped entire log, size %u\n", potential_size);
}

#endif /* CONFIG_BENCHMARK */
#endif /* LIBSEL4_BENCHMARK */

