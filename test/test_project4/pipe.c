// Small test program exercising kernel pipe page-give/take primitives.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PAGE 4096ul            // Page size used for pipe page transfers.
#define PIPE_SELF "pipe-self"  // Pipe name used for self-test.
#define PIPE_DEMO "pipe-demo"  // Pipe name used for cross-process demo.
#define PIPE_SEND_LINE 3
#define PIPE_RECV_LINE 2

// Allocate 'pages' aligned pages for the test. This test stub returns a
// fixed high address to simulate a mapped region.
static uintptr_t alloc_aligned_pages(size_t pages)
{
	return 1ul << 30;
}

// Test giving a page to a pipe and then taking it back within the same
// process to verify basic give/take semantics.
static int pipe_self_test(void)
{
	int pipe_id = sys_pipe_open(PIPE_SELF);
	if (pipe_id < 0)
	{
		printf("pipe self: open failed\n");
		return -1;
	}

	uintptr_t base = alloc_aligned_pages(2);
	char *src = (char *)base;
	char *dst = (char *)(base + PAGE);

	const char *msg = "hello-from-pipe-self";
	memset(src, 0xab, PAGE);
	memcpy(src, msg, strlen(msg) + 1);

	long given = sys_pipe_give_pages(pipe_id, src, PAGE);
	if (given != PAGE)
	{
		printf("pipe self: give failed (%ld)\n", given);
		return -1;
	}

	long taken = sys_pipe_take_pages(pipe_id, dst, PAGE);
	if (taken != PAGE)
	{
		printf("pipe self: get failed (%ld)\n", taken);
		return -1;
	}

	if (strcmp(dst, msg) == 0)
	{
		printf("pipe self: success, msg=\"%s\"\n", dst);
		return 0;
	}

	printf("pipe self: mismatch, got \"%s\"\n", dst);
	return -1;
}

// Sender process for cross-process test: gives one page to PIPE_DEMO.
static int pipe_sender(void)
{
	int pipe_id = sys_pipe_open(PIPE_DEMO);
	if (pipe_id < 0)
	{
		sys_move_cursor(0, PIPE_SEND_LINE);
		printf("pipe sender: open failed\n");
		return -1;
	}

	uintptr_t base = alloc_aligned_pages(1);
	char *src = (char *)base;

	memset(src, 0xcd, PAGE);
	const char *msg = "hello-from-sender";
	memcpy(src, msg, strlen(msg) + 1);

	long ret = sys_pipe_give_pages(pipe_id, src, PAGE);
	sys_move_cursor(0, PIPE_SEND_LINE);
	printf("pipe sender: give %ld bytes\n", ret);
	return ret == PAGE ? 0 : -1;
}

// Receiver process for cross-process test: waits and then takes a page
// from PIPE_DEMO.
static int pipe_receiver(void)
{
	int pipe_id = sys_pipe_open(PIPE_DEMO);
	if (pipe_id < 0)
	{
		sys_move_cursor(0, PIPE_RECV_LINE);
		printf("pipe receiver: open failed\n");
		return -1;
	}

	uintptr_t base = alloc_aligned_pages(1);
	char *dst = (char *)base;

	sys_move_cursor(0, PIPE_RECV_LINE);
	printf("pipe receiver: waiting...\n");
	long ret = sys_pipe_take_pages(pipe_id, dst, PAGE);
	if (ret != PAGE)
	{
		sys_move_cursor(0, PIPE_RECV_LINE);
		printf("pipe receiver: get failed (%ld)\n", ret);
		return -1;
	}

	sys_move_cursor(0, PIPE_RECV_LINE);
	printf("pipe receiver: got page, msg=\"%s\"\n", dst);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc > 1 && strcmp(argv[1], "send") == 0)
		return pipe_sender();

	if (argc > 1 && strcmp(argv[1], "recv") == 0)
		return pipe_receiver();

	char *prog_name = argc > 0 ? argv[0] : (char *)"pipe";

	if (pipe_self_test() < 0)
		printf("pipe: self test failed\n");

	printf("pipe: starting cross-process test\n");

	char *receiver_argv[2] = {prog_name, (char *)"recv"};
	char *sender_argv[2] = {prog_name, (char *)"send"};

	pid_t receiver = sys_exec(prog_name, 2, receiver_argv);
	if (receiver == 0)
	{
		printf("pipe: failed to start receiver\n");
		return;
	}

	sys_sleep(1);

	pid_t sender = sys_exec(prog_name, 2, sender_argv);
	if (sender == 0)
	{
		printf("pipe: failed to start sender\n");
		sys_kill(receiver);
		return;
	}

	sys_waitpid(receiver);
	sys_waitpid(sender);

	printf("pipe: cross-process test finished\n");

	return 0;
}
