.intel_syntax noprefix

.global spawn_clone3
.type   spawn_clone3, @function

spawn_clone3:
    mov  rax, 435
    syscall # SYS_clone3

    test rax, rax
    jz   .Lchild
    ret

.Lchild:
    pop  rdi # data
    pop  rax # func
    call rax # func(data)

    mov  edi, eax
    mov  eax, 60
    syscall # SYS_exit

    ud2

.size spawn_clone3, . - spawn_clone3
