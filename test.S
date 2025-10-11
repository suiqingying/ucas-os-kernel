
.global main
main:

// TODO: [p0-task1] Add your code here!

main:
	li s1,1
	li s2,50
	li s3,0

loop:
	add s3,s3,s1
	addi s1,s1,1
	blt s1,s2,loop

	la	t0, RESULT_ADDR
    sw	s3, 0(t0)
end:
	j end





    