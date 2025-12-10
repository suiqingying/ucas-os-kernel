#include <os/kernel.h>
#include <os/mm.h>
#include <os/string.h>
#include <os/task.h>
#include <pgtable.h>
#include <printk.h>
#include <type.h>

uint64_t load_task_img(char *task_name, uintptr_t pgdir) {
    int i;
    for (i = 0; i < TASK_MAXNUM; i++) {
        if (tasks[i].task_name[0] != '\0' && strcmp(task_name, tasks[i].task_name) == 0) {
            break;
        }
    }
    if (i == TASK_MAXNUM) {
        printk("Error: Task %s not found!\n", task_name);
        return 0;
    }

    task_info_t *task = &tasks[i];
    if (task->p_memsz == 0) return 0;

    int start_sec = tasks[i].start_addr / 512;
    int offset_in_sec = tasks[i].start_addr % 512;
    bios_sd_read(TASK_MEM_BASE, tasks[i].block_nums, start_sec);

    uint8_t *src_base = (uint8_t *)pa2kva(TASK_MEM_BASE) + offset_in_sec;
    uint64_t user_va = USER_ENTRY_POINT, user_va_end = USER_ENTRY_POINT + tasks[i].p_memsz;
    for (; user_va < user_va_end; user_va += PAGE_SIZE) {
        uintptr_t page_addr = alloc_page_helper(user_va, pgdir);
        uint64_t offset = user_va - USER_ENTRY_POINT;
        if (offset < task->p_filesz) {
            // 计算本页有多少有效数据
            uint64_t file_remain = task->p_filesz - offset;
            uint32_t copy_size = (file_remain > PAGE_SIZE) ? PAGE_SIZE : file_remain;

            // 拷贝数据
            memcpy((void *)page_addr, (void *)(src_base + offset), copy_size);
            if (copy_size < PAGE_SIZE) memset((void *)(page_addr + copy_size), 0, PAGE_SIZE - copy_size);
        } else {
            // bss 段，清零
            memset((uint8_t *)page_addr, 0, PAGE_SIZE);
        }

        // 代码/静态段不参与换出
        mark_page_nonswappable(pgdir, user_va);
    }

    // 刷新指令缓存，确保 CPU 能看到刚写入的代码
    local_flush_icache_all();
    return USER_ENTRY_POINT;
}

void do_list() {
    printk("User programs:\n");
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (strlen(tasks[i].task_name) > 0) {
            printk("%s ", tasks[i].task_name);
        }
    }
    printk("\n");
}