/* Fault injection is confined to a test LD_PRELOAD library. */
#include <dlfcn.h>
#include <errno.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int sem_init(sem_t *sem, int shared, unsigned value) {
    static int calls;
    ++calls;
    const char *delay = getenv("SO_TEST_INIT_DELAY_MS");
    const char *failure = getenv("SO_TEST_FAIL_INIT");
    if (calls == 1 && delay) usleep((useconds_t)strtoul(delay, NULL, 10) * 1000);
    if (failure && atoi(failure) == calls) { errno = ENOSPC; return -1; }
    int (*real_init)(sem_t *, int, unsigned);
    void *symbol = dlsym(RTLD_NEXT, "sem_init");
    _Static_assert(sizeof(real_init) == sizeof(symbol), "POSIX function pointer size");
    memcpy(&real_init, &symbol, sizeof(symbol));
    if (!real_init) { errno = ENOSYS; return -1; }
    return real_init(sem, shared, value);
}
