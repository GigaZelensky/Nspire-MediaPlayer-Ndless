.syntax unified
.arm
.text
.balign 4
/* Copied position-independent callbacks outlive the player while registered.
 * ctx: callbacks+0, host+16, native_free+20, allocation+24, active+28,
 * retired+32. C callbacks run masked and remain counted across native yields.
 * Only this resident code releases the count after returning from player code.
 * Cleanup tail-frees its own allocation after the final callback has returned. */
.global usb_callback_start,usb_callback_end,usb_callback_context
.global usb_callback_match,usb_callback_attach,usb_callback_detach,usb_callback_cleanup
usb_callback_start:
usb_callback_match:
    mov r2,#0
    b 1f
usb_callback_attach:
    mov r2,#4
    b 1f
usb_callback_detach:
    mov r2,#8
    b 1f
usb_callback_cleanup:
    mov r2,#12
1:
    push {r4-r8,lr}
    mrs r4,cpsr
    orr r12,r4,#0xc0
    msr cpsr_c,r12
    mov r6,r2
    ldr r5,usb_callback_context
    ldr r3,[r5,#28]
    add r3,r3,#1
    str r3,[r5,#28]
    ldr r1,[r5,#16]
    cmp r1,#0
    beq 2f
    ldr r12,[r5,r6]
    blx r12
    b 3f
2:
    cmp r6,#4
    moveq r0,#6
    movne r0,#0
3:
    mov r7,r0
    mrs r12,cpsr
    orr r12,r12,#0xc0
    msr cpsr_c,r12
    cmp r6,#12
    moveq r3,#1
    streq r3,[r5,#32]
    ldr r3,[r5,#28]
    sub r3,r3,#1
    str r3,[r5,#28]
    cmp r3,#0
    bne 4f
    ldr r3,[r5,#32]
    cmp r3,#0
    beq 4f
    ldr r1,[r5,#16]
    mov r0,#0
    cmp r1,#0
    strne r0,[r1] /* UsbController.stub: no dangling owner after tail-free. */
    str r0,[r5,#16]
    ldr r12,[r5,#20]
    ldr r0,[r5,#24]
    b 5f
4:
    mov r0,r7
    ldr r12,[sp,#20] /* Return address, outside the copied allocation. */
5:
    mrs r2,cpsr
    bic r2,r2,#0xc0
    and r4,r4,#0xc0
    orr r2,r2,r4
    pop {r4-r8,lr}
    msr cpsr_c,r2
    bx r12
.balign 4
usb_callback_context:
    .word 0
usb_callback_end:
.section .note.GNU-stack,"",%progbits
