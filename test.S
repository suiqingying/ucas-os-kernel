// ...existing code...
    .data
    .align 2
prime_results:
    .space 200        # 每个数字对应一个字节，0=合数/非质数，1=质数

    .text
    .global main
    .global is_prime

# main: 对 1..200 的每个数字调用 is_prime，将返回值（0/1）存到 prime_results
main:
    la	t0, prime_results   # t0 = base 地址
    li	t1, 1               # 当前数字 n = 1
    li	t2, 200             # 上限 200

loop_main:
    mv	a0, t1              # 参数：a0 = n
    jal	ra, is_prime       # 调用 is_prime，返回值在 a0

    # 计算写入地址：base + (n-1)
    addi	t3, t1, -1         # t3 = n-1
    add	t4, t0, t3         # t4 = base + index
    sb	a0, 0(t4)           # 存回内存（0/1）

    addi	t1, t1, 1          # n++
    ble	t1, t2, loop_main  # 如果 n <= 200 继续

# 停在此处
end:
    j	end

# is_prime(n): 输入 a0 = n，返回 a0 = 1(质数) 或 0(非质数)
is_prime:
    # 处理 n <= 1 -> 非质数
    li	t0, 1
    ble	a0, t0, is_not_prime

    # n == 2 -> 质数
    li	t0, 2
    beq	a0, t0, is_prime_yes

    # 偶数且 >2 -> 非质数
    andi	t1, a0, 1
    beq	t1, x0, is_not_prime

    # 从 i = 3 开始，步长 2，检查 i*i <= n 且 n % i == 0
    li	t2, 3              # t2 = i
loop_check:
    mul	t3, t2, t2        # t3 = i*i
    bgt	t3, a0, is_prime_yes  # 如果 i*i > n，说明是质数

    rem	t4, a0, t2        # t4 = n % i
    beq	t4, x0, is_not_prime

    addi	t2, t2, 2        # i += 2
    j	loop_check

is_prime_yes:
    li	a0, 1
    jr	ra

is_not_prime:
    li	a0, 0
    jr	ra
// ...existing code...