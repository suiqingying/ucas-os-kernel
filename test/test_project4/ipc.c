#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// ipc_perf: mailbox vs pipe microbenchmarks

/*
MSG_IN_MB: message size in megabytes used for the benchmark.
*/
#define PAGE_SIZE 4096ul
const long MSG_IN_MB = 512;
const long MSG_BYTES = MSG_IN_MB * 1024 * 1024;
const long WARMUP_BYTES = PAGE_SIZE;
const char MBOX_NAME[] = "ipc-perf-mailbox";
const char PIPE_NAME[] = "ipc-perf-pipe";
enum
{
	ALL_TEST_START = 2,
	MAILBOX_TEST_LINE,
	MAILBOX_SEND_LINE,
	MAILBOX_RECV_LINE,
	PIPE_TEST_LINE,
	PIPE_SEND_LINE,
	PIPE_RECV_LINE,
	ALL_TEST_FINISH
};

/* Allocate 'pages' pages aligned to a large fixed address used by the test.
 * In this test harness the implementation returns a fixed address
 * (1 << 30) rather than performing a general allocator operation.
 */
static uintptr_t alloc_aligned_pages(size_t pages)
{
	return 1ul << 30;
}

/* Return a pointer to a payload buffer for tests.
 * This harness uses a fixed buffer address for simplicity (1 << 30).
 * Real allocators would return a writable heap region.
 */
static char *alloc_payload_buffer(void)
{
	return (char *)(1ul << 30);
}

/* Fill a buffer with deterministic pseudo-data used for verification.
 * Each byte = index & 0xff so the receiver can validate content.
 */
static void fill_payload(char *buf, size_t len)
{
	for (size_t i = 0; i < len; ++i)
	{
		buf[i] = (char)(i & 0xff);
	}
}

/* Print elapsed ticks and approximate seconds for the measured interval.
 * Uses sys_get_timebase() to convert ticks to seconds.
 */
static void print_timing(const char *tag, long bytes, long start, long end)
{
	long ticks = end - start;
	long seconds = ticks / sys_get_timebase();
	printf("[%s] %ld bytes in %ld ticks (%ld s)\n", tag, bytes, ticks, seconds);
}

// mailbox_sender: send MSG_BYTES via mailbox (with warmup)
static int mailbox_sender(void)
{
	char *buf = alloc_payload_buffer();
	fill_payload(buf, MSG_BYTES);

	int mq = sys_mbox_open((char *)MBOX_NAME);
	if (mq < 0)
	{
		sys_move_cursor(0, MAILBOX_SEND_LINE);
		printf("[mailbox send] open failed\n");
		return -1;
	}

	sys_sleep(1);

	int warmup = sys_mbox_send(mq, buf, WARMUP_BYTES);
	if (warmup < 0)
	{
		sys_move_cursor(0, MAILBOX_SEND_LINE);
		printf("[mailbox send] warmup failed (%d)\n", warmup);
		sys_mbox_close(mq);
		return -1;
	}

	long start = sys_get_tick();
	int sent = sys_mbox_send(mq, buf, MSG_BYTES);
	long end = sys_get_tick();

	sys_mbox_close(mq);

	if (sent < 0)
	{
		sys_move_cursor(0, MAILBOX_SEND_LINE);
		printf("[mailbox send] send failed (%d)\n", sent);
		return -1;
	}

	sys_move_cursor(0, MAILBOX_SEND_LINE);
	print_timing("mailbox send", sent, start, end);
	return 0;
}

// mailbox_receiver: receive MSG_BYTES via mailbox and verify
static int mailbox_receiver(void)
{
	char *buf = alloc_payload_buffer();

	int mq = sys_mbox_open((char *)MBOX_NAME);
	if (mq < 0)
	{
		sys_move_cursor(0, MAILBOX_RECV_LINE);
		printf("[mailbox recv] open failed\n");
		return -1;
	}

	int warmup = sys_mbox_recv(mq, buf, WARMUP_BYTES);
	if (warmup < 0)
	{
		sys_move_cursor(0, MAILBOX_RECV_LINE);
		printf("[mailbox recv] warmup failed (%d)\n", warmup);
		sys_mbox_close(mq);
		return -1;
	}

	long start = sys_get_tick();
	int received = sys_mbox_recv(mq, buf, MSG_BYTES);
	long end = sys_get_tick();

	sys_mbox_close(mq);

	if (received < 0)
	{
		sys_move_cursor(0, MAILBOX_RECV_LINE);
		printf("[mailbox recv] recv failed (%d)\n", received);
		return -1;
	}

	for (size_t i = 0; i < MSG_BYTES; ++i)
	{
		if (buf[i] != (char)(i & 0xff))
		{
			sys_move_cursor(0, MAILBOX_RECV_LINE);
			printf("[mailbox recv] data mismatch at %d\n", (int)i);
			return -1;
		}
	}

	sys_move_cursor(0, MAILBOX_RECV_LINE);
	print_timing("mailbox recv", received, start, end);
	return 0;
}

// pipe_sender: warmup + give pages
static int pipe_sender(void)
{
	int pipe_id = sys_pipe_open(PIPE_NAME);
	if (pipe_id < 0)
	{
		sys_move_cursor(0, PIPE_SEND_LINE);
		printf("[pipe send] open failed\n");
		return -1;
	}

	char *warmup_src = (char *)alloc_aligned_pages(WARMUP_BYTES / PAGE_SIZE);
	fill_payload(warmup_src, WARMUP_BYTES);

	long warmup = sys_pipe_give_pages(pipe_id, warmup_src, WARMUP_BYTES);
	if (warmup != WARMUP_BYTES)
	{
		sys_move_cursor(0, PIPE_SEND_LINE);
		printf("[pipe send] warmup failed (%ld)\n", warmup);
		return -1;
	}

	char *src = alloc_payload_buffer();
	fill_payload(src, MSG_BYTES);

	long start = sys_get_tick();
	long given = sys_pipe_give_pages(pipe_id, src, MSG_BYTES);
	long end = sys_get_tick();

	if (given != MSG_BYTES)
	{
		sys_move_cursor(0, PIPE_SEND_LINE);
		printf("[pipe send] give failed (%ld)\n", given);
		return -1;
	}

	sys_move_cursor(0, PIPE_SEND_LINE);
	print_timing("pipe send", given, start, end);
	return 0;
}

// pipe_receiver: warmup + take pages + verify
static int pipe_receiver(void)
{
	int pipe_id = sys_pipe_open(PIPE_NAME);
	if (pipe_id < 0)
	{
		sys_move_cursor(0, PIPE_RECV_LINE);
		printf("[pipe recv] open failed\n");
		return -1;
	}

	char *warmup_dst = (char *)alloc_aligned_pages(WARMUP_BYTES / PAGE_SIZE);
	char *dst = alloc_payload_buffer();

	long warmup = sys_pipe_take_pages(pipe_id, warmup_dst, WARMUP_BYTES);
	if (warmup != WARMUP_BYTES)
	{
		sys_move_cursor(0, PIPE_RECV_LINE);
		printf("[pipe recv] warmup failed (%ld)\n", warmup);
		return -1;
	}

	long start = sys_get_tick();
	long taken = sys_pipe_take_pages(pipe_id, dst, MSG_BYTES);
	long end = sys_get_tick();

	if (taken != MSG_BYTES)
	{
		sys_move_cursor(0, PIPE_RECV_LINE);
		printf("[pipe recv] take failed (%ld)\n", taken);
		return -1;
	}

	for (size_t i = 0; i < MSG_BYTES; ++i)
	{
		if (dst[i] != (char)(i & 0xff))
		{
			sys_move_cursor(0, PIPE_RECV_LINE);
			printf("[pipe recv] data mismatch at %d\n", (int)i);
			return -1;
		}
	}

	sys_move_cursor(0, PIPE_RECV_LINE);
	print_timing("pipe recv", taken, start, end);
	return 0;
}

// run_mailbox_test: spawn receiver, then sender, wait
static void run_mailbox_test(char *prog_name)
{
	sys_move_cursor(0, MAILBOX_TEST_LINE);
	printf("== mailbox performance test ==\n");
	char *recv_argv[3] = {prog_name, (char *)"mbox", (char *)"recv"};
	char *send_argv[3] = {prog_name, (char *)"mbox", (char *)"send"};

	pid_t receiver = sys_exec(prog_name, 3, recv_argv);
	if (receiver == 0)
	{
		sys_move_cursor(0, MAILBOX_TEST_LINE);
		printf("mailbox: failed to start receiver\n");
		return;
	}

	sys_sleep(1);

	pid_t sender = sys_exec(prog_name, 3, send_argv);
	if (sender == 0)
	{
		sys_move_cursor(0, MAILBOX_TEST_LINE);
		printf("mailbox: failed to start sender\n");
		sys_kill(receiver);
		return;
	}

	sys_waitpid(receiver);
	sys_waitpid(sender);
}

// run_pipe_test: spawn receiver, then sender, wait
static void run_pipe_test(char *prog_name)
{
	sys_move_cursor(0, PIPE_TEST_LINE);
	printf("== pipe performance test ==\n");
	char *recv_argv[3] = {prog_name, (char *)"pipe", (char *)"recv"};
	char *send_argv[3] = {prog_name, (char *)"pipe", (char *)"send"};

	pid_t receiver = sys_exec(prog_name, 3, recv_argv);
	if (receiver == 0)
	{
		sys_move_cursor(0, PIPE_TEST_LINE);
		printf("pipe: failed to start receiver\n");
		return;
	}

	sys_sleep(1);

	pid_t sender = sys_exec(prog_name, 3, send_argv);
	if (sender == 0)
	{
		sys_move_cursor(0, PIPE_TEST_LINE);
		printf("pipe: failed to start sender\n");
		sys_kill(receiver);
		return;
	}

	sys_waitpid(receiver);
	sys_waitpid(sender);
}

// main: dispatch subcommands or run both tests
int main(int argc, char *argv[])
{
	char *prog_name = argc > 0 ? argv[0] : (char *)"ipc_perf";

	if (argc >= 3 && strcmp(argv[1], "mbox") == 0)
	{
		if (strcmp(argv[2], "send") == 0)
			return mailbox_sender();
		if (strcmp(argv[2], "recv") == 0)
			return mailbox_receiver();
	}

	if (argc >= 3 && strcmp(argv[1], "pipe") == 0)
	{
		if (strcmp(argv[2], "send") == 0)
			return pipe_sender();
		if (strcmp(argv[2], "recv") == 0)
			return pipe_receiver();
	}

	sys_move_cursor(0, ALL_TEST_START);
	printf("ipc_perf: comparing mailbox vs pipe with %ld byte payloads\n", (long)MSG_BYTES);
	run_mailbox_test(prog_name);
	run_pipe_test(prog_name);
	sys_move_cursor(0, ALL_TEST_FINISH);
	printf("ipc_perf: done\n");
	return 0;
}
