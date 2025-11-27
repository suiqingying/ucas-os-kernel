#include <mailbox.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define THREADFIX_MBOX_A "threadfix-mbox-1"
#define THREADFIX_MBOX_B "threadfix-mbox-2"
#define THREADFIX_BARRIER_KEY 0xbeef

typedef struct {
    int send_handle;
    int recv_handle;
    char label;
    int max_loops;
} thread_state_t;

static void fill_mailbox_full(const char *name, char token) {
    int handle = sys_mbox_open((char *)name);
    static char payload[MAX_MBOX_LENGTH];
    memset(payload, token, sizeof(payload));
    sys_mbox_send(handle, payload, MAX_MBOX_LENGTH);
    // keep the mailbox open so the content stays
}

static void receiver_thread(void *arg) {
    thread_state_t *state = (thread_state_t *)arg;
    char buff[MAX_MBOX_LENGTH];
    for (int i = 0; i < state->max_loops; i++) {
        sys_mbox_recv(state->recv_handle, buff, MAX_MBOX_LENGTH);
        sys_move_cursor(0, state->label == 'A' ? 7 : 9);
        printf("[Fix-%c] recv #%d complete\n", state->label, i + 1);
    }
    sys_thread_exit(NULL);
}

static void sender_thread(void *arg) {
    thread_state_t *state = (thread_state_t *)arg;
    char payload[MAX_MBOX_LENGTH];
    memset(payload, state->label, sizeof(payload));
    for (int i = 0; i < state->max_loops; i++) {
        sys_mbox_send(state->send_handle, payload, MAX_MBOX_LENGTH);
        sys_move_cursor(0, state->label == 'A' ? 8 : 10);
        printf("[Fix-%c] send #%d pushed successfully\n", state->label, i + 1);
    }
    sys_thread_exit(NULL);
}

static pid_t spawn_thread_worker(const char *role) {
    char *child_argv[2];
    child_argv[0] = "mbox_thr_demo";
    child_argv[1] = (char *)role;
    pid_t pid = sys_exec("mbox_thr_demo", 2, child_argv);
    printf("[ThreadFix] spawned role %s (pid=%d)\n", role, pid);
    return pid;
}

static void controller_main(void) {
    sys_move_cursor(0, 0);
    printf("=========== Mailbox Thread Fix Demo ==========\n");
    printf("Step 1: Prefilling mailboxes (same deadlock setup)...\n");
    fill_mailbox_full(THREADFIX_MBOX_A, 'A');
    fill_mailbox_full(THREADFIX_MBOX_B, 'B');
    printf("Step 2: Spawning threaded workers A & B.\n");
    pid_t pidA = spawn_thread_worker("roleA");
    pid_t pidB = spawn_thread_worker("roleB");
    printf("Threads split send/recv; waiting for both workers to finish...\n");

    sys_waitpid(pidA);
    sys_waitpid(pidB);

    printf("[ThreadFix] All workers exited cleanly. Demo finished.\n");
    sys_exit();
}

static void worker_main(int is_role_a) {
    const char *target = is_role_a ? THREADFIX_MBOX_A : THREADFIX_MBOX_B;
    const char *peer = is_role_a ? THREADFIX_MBOX_B : THREADFIX_MBOX_A;

    thread_state_t state;
    state.send_handle = sys_mbox_open((char *)target);
    state.recv_handle = sys_mbox_open((char *)peer);
    state.label = is_role_a ? 'A' : 'B';
    state.max_loops = 5000;

    int barrier = sys_barrier_init(THREADFIX_BARRIER_KEY, 2);
    sys_move_cursor(0, is_role_a ? 5 : 6);
    printf("[Fix-%c] worker ready, waiting for peer...\n", state.label);
    sys_barrier_wait(barrier);

    int recv_tid = sys_thread_create(receiver_thread, &state);
    int send_tid = sys_thread_create(sender_thread, &state);
    sys_move_cursor(0, is_role_a ? 5 : 6);
    printf("[Fix-%c] recv_tid=%d send_tid=%d running.\n", state.label, recv_tid, send_tid);

    sys_thread_join(recv_tid, NULL);
    sys_thread_join(send_tid, NULL);
    sys_exit();
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "roleA") == 0) {
        worker_main(1);
        return 0;
    } else if (argc >= 2 && strcmp(argv[1], "roleB") == 0) {
        worker_main(0);
        return 0;
    }

    controller_main();
    return 0;
}

