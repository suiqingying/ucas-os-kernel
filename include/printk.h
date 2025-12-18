#ifndef INCLUDE_PRINTK_H_
#define INCLUDE_PRINTK_H_

#include <stdarg.h>

/* kernel print */
int printk(const char *fmt, ...);

/* print to current task console area (uses current_running cursor) */
int printu(const char *fmt, ...);

/* vt100 print */
int printv(const char *fmt, ...);

/* (QEMU-only) save print content to logfile */
int printl(const char *fmt, ...);

#endif
