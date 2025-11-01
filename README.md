  Task 3 的目标与实验步骤

  核心目标：
   1. 掌握系统调用流程：理解用户态程序如何通过 ecall 指令陷入内核态，以及内核如何处理并返回。
   2. 实现异常（Exception）处理：系统调用是一种由程序主动触发的“异常”。你需要搭建起整个异常处理的骨架，包括入口、上下文保存
      /恢复、返回等。
   3. 实现新的系统调用：你需要重新实现 sys_yield 等函数，并新增 sys_sleep 函数。

  实验步骤概览：
  根据课件第19-20页，你需要修改以下文件，完成这些步骤：
   1. `init/main.c`: 初始化系统调用，并完善PCB内核栈的初始化函数 init_pcb_stack，为异常处理准备好栈环境。
   2. `arch/riscv/kernel/trap.S`: 设置 stvec 寄存器，让CPU知道发生异常时应该跳转到哪里。
   3. `kernel/irq/irq.c`: 初始化异常处理相关的跳转表。
   4. `arch/riscv/kernel/entry.S`: 实现异常处理的总入口 exception_handler_entry，核心是
      保存当前进程的上下文（所有寄存器）。
   5. `arch/riscv/kernel/entry.S`: 实现 SAVE_CONTEXT 和 RESTORE_CONTEXT 两个宏，用于保存和恢复上下文。
   6. `arch/riscv/kernel/entry.S`: 实现 ret_from_exception，用于从异常中安全返回用户态。
   7. `tiny_libc/syscall.c`: 实现 invoke_syscall 函数，使用 ecall 汇编指令真正地发起一次系统调用。
   8. `kernel/syscall/syscall.c`: 实现 handle_syscall
      函数，它作为内核中系统调用的分发中心，根据传入的系统调用号，调用对应的内核函数（如 do_sleep）。
   9. `kernel/sched/sched.c` 和 `kernel/sched/time.c`: 实现 do_sleep 和 check_sleeping 函数，让进程可以睡眠。
   10. 运行测试：运行所有给出的测试任务（syscall版本），并得到正确结果。