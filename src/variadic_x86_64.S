.text

/* SysV x86-64. These signatures use integer/pointer argument registers.
   Preserve %al and leave the original stack arguments in place. */
.macro forward_variadic name
.globl \name
.type \name, @function
\name:
  .cfi_startproc
  endbr64
  subq $56, %rsp
  .cfi_adjust_cfa_offset 56
  movq %rax, 0(%rsp)
  movq %rdi, 8(%rsp)
  movq %rsi, 16(%rsp)
  movq %rdx, 24(%rsp)
  movq %rcx, 32(%rsp)
  movq %r8, 40(%rsp)
  movq %r9, 48(%rsp)
  call zstdio_target_\name
  movq %rax, %r11
  movq 0(%rsp), %rax
  movq 8(%rsp), %rdi
  movq 16(%rsp), %rsi
  movq 24(%rsp), %rdx
  movq 32(%rsp), %rcx
  movq 40(%rsp), %r8
  movq 48(%rsp), %r9
  addq $56, %rsp
  .cfi_adjust_cfa_offset -56
  testq %r11, %r11
  je 1f
  jmp *%r11
1:
  movl $-1, %eax
  ret
  .cfi_endproc
.size \name, .-\name
.endm

forward_variadic open64
forward_variadic fcntl64
forward_variadic execl
forward_variadic execlp

.section .note.GNU-stack,"",@progbits
