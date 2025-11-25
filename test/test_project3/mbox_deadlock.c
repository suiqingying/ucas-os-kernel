#include <mailbox.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEADLOCK_MBOX_A "deadlock-mbox-1"
#define DEADLOCK_MBOX_B "deadlock-mbox-2"
#define DEADLOCK_BARRIER_KEY 0xdead

static void fill_mailbox(const char *name, char pattern) {
    int handle = sys_mbox_open((char *)name);
    static char payload[MAX_MBOX_LENGTH];
    memset(payload, pattern, sizeof(payload));
    sys_mbox_send(handle, payload, MAX_MBOX_LENGTH);
    // Intentionally keep the mailbox open so that its buffer remains full.
}

static void spawn_worker(const char *role) {
    char *child_argv[2];
    child_argv[0] = "mbox_deadlock"; // argv[0] should be program name
    child_argv[1] = (char *)role;    // argv[1] carries the role tag
    pid_t pid = sys_exec("mbox_deadlock", 2, child_argv);
    printf("[Deadlock] spawned role %s (pid=%d)\n", role, pid);
}

static void controller_main(void) {
    sys_move_cursor(0, 0);
    printf("=========== Mailbox Deadlock Demo ===========\n");
    printf("Step 1: Prefilling both mailboxes so they are full...\n");
    fill_mailbox(DEADLOCK_MBOX_A, 'A');
    fill_mailbox(DEADLOCK_MBOX_B, 'B');

    printf("Step 2: Spawning two workers that will both try to send.\n");
    spawn_worker("roleA");
    spawn_worker("roleB");

    printf("Both workers are now running. Each will try to send a %d-byte\n", MAX_MBOX_LENGTH);
    printf("message to an already-full mailbox and will therefore block.\n");
    printf("Use 'ps' to observe the two processes stuck in TASK_BLOCKED.\n");
    printf("This reproduces the two-mailbox circular wait described in the guide.\n");

    // Keep the controller alive so the shell regains control with '&'.
    while (1) {
        sys_sleep(1);
    }
}

static void worker_main(int is_role_a) {
    const char *target = is_role_a ? DEADLOCK_MBOX_A : DEADLOCK_MBOX_B;
    const char *peer = is_role_a ? DEADLOCK_MBOX_B : DEADLOCK_MBOX_A;
    char payload[MAX_MBOX_LENGTH];
    memset(payload, is_role_a ? 'A' : 'B', sizeof(payload));

    int send_handle = sys_mbox_open((char *)target);
    int recv_handle = sys_mbox_open((char *)peer);
    int barrier = sys_barrier_init(DEADLOCK_BARRIER_KEY, 2);

    sys_move_cursor(0, is_role_a ? 5 : 6);
    printf("[Deadlock-%c] Ready. Waiting for peer...\n", is_role_a ? 'A' : 'B');

    sys_barrier_wait(barrier);

    sys_move_cursor(0, is_role_a ? 5 : 6);
    printf("[Deadlock-%c] Sending to %s (expect to block)\n", is_role_a ? 'A' : 'B', target);
    sys_mbox_send(send_handle, payload, MAX_MBOX_LENGTH);

    sys_move_cursor(0, is_role_a ? 5 : 6);
    printf("[Deadlock-%c] Unexpectedly sent! Receiving from %s\n", is_role_a ? 'A' : 'B', peer);
    sys_mbox_recv(recv_handle, payload, MAX_MBOX_LENGTH);
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

