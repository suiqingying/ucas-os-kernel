#ifndef __INCLUDE_LOADER_H__
#define __INCLUDE_LOADER_H__

#include <type.h>

uint64_t load_task_img(char *taskname, uintptr_t pgdir);
void do_list();
#endif