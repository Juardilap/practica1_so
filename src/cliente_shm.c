#include "console_input.h"
#include "shm_utils.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t stop_requested;
static void stop(int signo) { (void)signo; stop_requested = 1; }

static int receive(ShmSession *session, int exiting) {
    for (;;) {
        if (shm_wait(session, &session->block->resp_ready) < 0) return -1;
        Response response = session->block->response;
        /* ACK copied messages, including malformed and terminal responses. */
        if (sem_post(&session->block->resp_consumed) < 0) return -1;
        if (!memchr(response.text, '\0', sizeof(response.text))) { errno = EPROTO; return -1; }
        if (response.type == RESP_ERROR) {
            fprintf(stderr, "%s\n", response.text); errno = EIO; return -1;
        }
        if (exiting && response.type == RESP_BYE) return puts(response.text) < 0 ? -1 : 0;
        if (!exiting) {
            if (response.type == RESP_END) return puts(response.text) < 0 ? -1 : 0;
            if (response.type == RESP_ROW || response.type == RESP_NOT_FOUND) {
                if (puts(response.text) < 0) return -1;
                continue;
            }
        }
        errno = EPROTO;
        return -1;
    }
}

int main(void) {
    ShmSession session = SHM_SESSION_INIT(&stop_requested);
    ConsoleInput input = {0};
    int status = 1;
    if (shm_install_signals(stop) < 0) { perror("sigaction"); return 1; }
    if (shm_client_open(&session) < 0) { perror("conectar SHM (servidor listo y unico cliente)"); goto cleanup; }
    for (;;) {
        Request request;
        if (console_request(&input, session.peer_fd, shm_check, &session, &request) < 0) goto failed;
        if (shm_check(&session) < 0) goto failed;
        session.block->request = request;
        if (sem_post(&session.block->req_ready) < 0) goto failed;
        if (receive(&session, request.operation == OP_EXIT) < 0) goto failed;
        if (request.operation == OP_EXIT) { status = 0; break; }
    }
    goto cleanup;
failed:
    if (stop_requested) status = 0;
    else perror("Sesion SHM");
cleanup:
    if (stop_requested) status = 0;
    if (shm_session_close(&session) < 0) status = 1;
    return status;
}
