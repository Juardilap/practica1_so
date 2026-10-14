#include "dataset.h"
#include "shm_utils.h"
#include <errno.h>
#include <stdio.h>

static volatile sig_atomic_t stop_requested;
static void stop(int signo) { (void)signo; stop_requested = 1; }

int main(void) {
    ShmSession session = SHM_SESSION_INIT(&stop_requested);
    int status = 1;
    if (shm_install_signals(stop) < 0) { perror("sigaction"); return 1; }
    if (shm_server_open(&session) < 0) { perror("crear SHM (revisar segmentos residuales)"); goto cleanup; }
    while (!stop_requested) {
        if (shm_wait(&session, &session.block->req_ready) < 0) goto failed;
        if (stop_requested) break;
        Request request = session.block->request;
        atomic_store_explicit(&session.block->state, STATE_REQUEST_READY, memory_order_release);
        if (request.operation == OP_EXIT) {
            Response bye = { .type = RESP_BYE, .text = "Sesion finalizada." };
            if (shm_emit(&bye, &session) < 0) goto failed;
            status = 0;
            goto cleanup;
        }
        /* The common module validates operation and NUL before inspecting text. */
        if (search_dataset(DATASET_DEFAULT_PATH, &request, shm_emit, &session) < 0) goto failed;
        atomic_store_explicit(&session.block->state, STATE_IDLE, memory_order_release);
    }
    status = 0;
    goto cleanup;
failed:
    if (stop_requested || errno == ECANCELED) status = 0;
    else perror("Sesion SHM");
cleanup:
    if (stop_requested) status = 0;
    if (shm_session_close(&session) < 0) status = 1;
    return status;
}
