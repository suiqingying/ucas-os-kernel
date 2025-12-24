# Commit Message Convention (项目内约定)

1. 标题格式  
   `type(scope): [Task N] 简要描述`  
   - `type` 常见：`feat` / `fix` / `chore` / `refactor` / `docs`  
   - `scope` 示例：`p5`、`mm`、`ipc`、`console`、`image` 等  
   - `Task N` 可选，用于项目阶段标识，如 `[Task 4]`；非阶段性工作可省略  
   - 描述用简洁中文，说明本次核心变化

2. 正文格式  
   - 空一行后，用条目列出具体改动；每行以 `-` 开头，首词可用子模块前缀（如 `-net:`、`-irq:`、`-docs:`）  
   - 条目内容偏事实陈述，避免冗长；多点可分行  
   - 如需多段说明，条目之间可空一行，保持可读性

3. 示例
   ```
   fix(p5): [Task 4] 隔离流式接收定时唤醒

   -net: 流式接收改用独立阻塞队列，避免影响 Task3 RXDMT0 唤醒
   -irq: 时钟中断定时唤醒流式接收线程，板卡无需 RXT0
   -docs: README 修正 RXT0 依赖说明并追加本次修改记录
   ```

   ```
   feat(mm/ipc/sched): swappable tracking, pipe stability, and stack reclamation

   - mm: 引入可换出页链表，OOM 时先尝试 swap_out
   - pipe: 支持 swapped 管道页，修复大载荷死锁
   - sched: 记录内核栈基址，退出时归还
   - docs: README 记录 bug 根因与修复
   ```

4. 其他约束  
   - 语言：中文为主，可混用英文术语  
   - 一次提交只覆盖一个主题；分支/合并冲突解决可用 `chore` 或 `fix` 视情况而定  
   - 若有外部接口/任务编号，放在标题中方括号；正文避免重复
