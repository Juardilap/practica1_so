#include "shm_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <unistd.h>
static volatile sig_atomic_t stop;
int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "corrupt")) {
        SharedBlock *b = shmat(atoi(argv[2]), NULL, 0);
        if (b == (void *)-1) return 1;
        b->version = 999;
        return shmdt(b) < 0;
    }
    ShmSession session = SHM_SESSION_INIT(&stop);
    if (shm_client_open(&session) < 0) { perror("probe connect"); shm_session_close(&session); return 1; }
    puts("CONNECTED"); fflush(stdout);
    if (argc < 2 || !strcmp(argv[1], "hold")) { pause(); }
    else {
        Request request = { .operation = atoi(argv[1]) };
        if (argc > 2 && !strcmp(argv[2], "badnul")) memset(request.value, 'x', sizeof(request.value));
        else if (argc > 2) snprintf(request.value, sizeof(request.value), "%s", argv[2]);
        session.block->request = request;
        if (sem_post(&session.block->req_ready) < 0) return 2;
        for (;;) {
            if (shm_wait(&session, &session.block->resp_ready) < 0) return 3;
            int type = session.block->response.type;
            printf("TYPE %d\n", type); fflush(stdout);
            if (argc > 3 && !strcmp(argv[3], "noack")) pause();
            if (sem_post(&session.block->resp_consumed) < 0) return 4;
            if (type == RESP_END || type == RESP_ERROR || type == RESP_BYE) break;
        }
    }
    return shm_session_close(&session) < 0;
}
