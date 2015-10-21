.section ".init"
.arm
.align 4
.global _heap_size
.global _firm_appmemalloc

_firm_appmemalloc:
	.word 0x07C00000

@ reset stack
mov sp, #0x10000000

@ allocate bss/heap
@ no need to initialize as OS does that already.
stmfd sp!, {r0, r1, r2, r3, r4}

	@ MEMOP COMMIT
	ldr r0, =0x3
	@ addr0
	mov r1, #0x08000000
	@ addr1
	mov r2, #0
	@ size
	adr r3, _heap_size
	ldr r3, [r3]
	@ RW permissions
	mov r4, #3

	@ svcControlMemory
	svc 0x01

	@ save heap address
	mov r1, #0x08000000
	ldr r2, =_heap_base
	str r1, [r2]

ldmfd sp!, {r0, r1, r2, r3, r4}

blx _main

_heap_size:
	.word 0x01000000
	
.global svc_queryMemory
.type svc_queryMemory, %function
svc_queryMemory:
	push {r0, r1, r4-r6}
	svc  0x02
	ldr  r6, [sp]
	str  r1, [r6]
	str  r2, [r6, #4]
	str  r3, [r6, #8]
	str  r4, [r6, #0xc]
	ldr  r6, [sp, #4]
	str  r5, [r6]
	add  sp, sp, #8
	pop  {r4-r6}
	bx   lr
