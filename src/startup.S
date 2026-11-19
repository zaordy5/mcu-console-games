.syntax unified
.cpu cortex-m4
.thumb
.section .vectors,"a"
.word _stack_top, Reset_Handler
.rept 13
.word Default_Handler
.endr
.word SysTick_Handler

.text
.global Reset_Handler
.thumb_func
/** Copy initialized data, clear BSS and enter the application. */
Reset_Handler:
    ldr r0, =_idata
    ldr r1, =_data
    ldr r2, =_edata
1:  cmp r1, r2
    bhs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =_bss
    ldr r2, =_ebss
    movs r3, #0
3:  cmp r1, r2
    bhs 4f
    str r3, [r1], #4
    b 3b
4:  bl main
.thumb_func
/** Stop if main returns or an unexpected core exception occurs. */
Default_Handler:
    b Default_Handler
