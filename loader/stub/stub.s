.nds

.create "stub.bin", 0x0

.arm
	; r0 = gsp::GPU handle, r1 = ns:s handle
	; save second parameters
	mov r4, r0
	mov r5, r1
	mov r6, r2

	; setup useful constant
	mrc p15, 0, r7, c13, c0, 3
	add r7, #0x80

	; ; sendSyncRequest so that the GX command is handled
	; .word 0xef000032

	; sleep for a bit
	mov r0, #0x05000000 ; ~100ms
	mov r1, #0x00000000
	.word 0xef00000a

	; GSPGPU_UnregisterInterruptRelayQueue
	ldr r1, =0x00140000
	str r1, [r7]
	mov r0, r4
	.word 0xef000032
	;cmp r0, #0
	;ldrne r0, [r0]

	; svc_unmapMemoryBlock(gspSharedMemHandle, 0x10002000);
	mov r0, r6
	ldr r1, =0x10002000
	.word 0xef000020
	;cmp r0, #0
	;ldrne r0, [r0]

	; GSPGPU_ReleaseRight
	ldr r1, =0x00170000
	str r1, [r7]
	mov r0, r4
	.word 0xef000032
	;cmp r0, #0
	;ldrne r0, [r0]

	; NSS_LaunchTitle
	ldr r1, =0x000200C0
	str r1, [r7]
	ldr r1, =0x00002A02 ; tid low
	str r1, [r7, #0x4]
	ldr r1, =0x00040130 ; tid high
	str r1, [r7, #0x8]
	ldr r1, =0x00000001 ; flags low
	str r1, [r7, #0xC]
	mov r0, r5
	.word 0xef000032
	;cmp r0, #0
	;ldrne r0, [r0]

	; sleep for a bit
	mov r0, #0x05000000 ; ~100ms
	mov r1, #0x00000000
	.word 0xef00000a

	; NSS_TerminateProcessTID
	ldr r1, =0x00110100
	str r1, [r7]
	ldr r1, =0x00002A02 ; tid low
	str r1, [r7, #0x4]
	ldr r1, =0x00040130 ; tid high
	str r1, [r7, #0x8]
	ldr r1, =0x05000000 ; timeout low
	str r1, [r7, #0xC]
	ldr r1, =0x00000000 ; timeout high
	str r1, [r7, #0x10]
	mov r0, r5
	.word 0xef000032
	;cmp r0, #0
	;ldrne r0, [r0]

	; close all the handles
	mov r4, #0
	handleCloseLoop:
		mov r5, #0
		handleCloseLoop2:
			mov r0, r5, lsl 15
			orr r0, r4
			.word 0xef000023
			cmp r0, #0
			addne r5, #1
			cmpne r5, #0x200
			bne handleCloseLoop2
		add r4, #1
		cmp r4, #0x26
		bne handleCloseLoop

	mov sp, #0x10000000
	mov pc, #0x00100000

	.pool

.Close
