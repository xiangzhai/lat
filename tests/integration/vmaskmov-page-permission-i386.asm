; SPDX-License-Identifier: GPL-2.0-only
;
; On a 16 KiB host page, protect one guest 4 KiB subpage and access it with
; VMASKMOVPS. LATX_MT must deliver SIGSEGV for selected elements, including
; the all-selected vector fast path.

%define __NR_exit 1
%define __NR_mprotect 125
%define __NR_rt_sigaction 174
%define __NR_mmap2 192

%define SIGSEGV 11
%define PROT_NONE 0
%define PROT_READ 1
%define PROT_WRITE 2
%define MAP_PRIVATE 2
%define MAP_ANONYMOUS 0x20
%define SA_SIGINFO 4
%ifndef MASKOP
%define MASKOP vmaskmovps
%endif
%define vmaskmovps MASKOP
%ifndef QUAD
%define QUAD 0
%endif

%ifndef WIDE
%define WIDE 0
%endif
%if WIDE
%define xmm0 ymm0
%define xmm1 ymm1
%endif

section .text
global _start
_start:
    mov [initial_sp], esp
    mov eax, __NR_rt_sigaction
    mov ebx, SIGSEGV
    mov ecx, segv_action
    xor edx, edx
    mov esi, 8
    int 0x80
    test eax, eax
    jnz fail_signal

    mov eax, __NR_mmap2
    xor ebx, ebx
    mov ecx, 16384
    mov edx, PROT_READ | PROT_WRITE
    mov esi, MAP_PRIVATE | MAP_ANONYMOUS
    mov edi, -1
    xor ebp, ebp
    int 0x80
    cmp eax, -4095
    jae fail_map
    mov edi, eax

    cmp dword [esp], 2
    jne fail_args
    mov eax, [esp + 8]
    cmp byte [eax], 'r'
    je protect_none
    cmp byte [eax], 'w'
    je protect_read
    jmp fail_args

protect_none:
    mov edx, PROT_NONE
    jmp protect
protect_read:
    mov edx, PROT_READ
protect:
    mov eax, __NR_mprotect
    lea ebx, [edi + 4096]
    mov ecx, 4096
    int 0x80
    test eax, eax
    js fail_protect

    mov eax, [esp + 8]
    cmp byte [eax + 1], 's'
    je suppressed_tail
    cmp byte [eax + 1], 'z'
    je zero_mask
    cmp byte [eax + 1], 'p'
    je partial
    cmp byte [eax + 1], 'f'
    je full
    jmp fail_args

suppressed_tail:
    lea ebp, [edi + 4096 - 4]
%if QUAD
    sub ebp, 4
%endif
    mov dword [ebp], 123
%if QUAD
    mov dword [ebp + 4], 0
%endif
    vmovdqu xmm1, [mask_partial]
    cmp byte [eax], 'r'
    je suppressed_read
    vmovdqu xmm0, [value]
    vmaskmovps [ebp], xmm1, xmm0
    xor ebx, ebx
    jmp exit
suppressed_read:
    vmaskmovps xmm0, xmm1, [ebp]
    xor ebx, ebx
    jmp exit

zero_mask:
    vpxor xmm1, xmm1, xmm1
    cmp byte [eax], 'r'
    je zero_read
    vmaskmovps [edi + 4096], xmm1, xmm0
    xor ebx, ebx
    jmp exit
zero_read:
    vmaskmovps xmm0, xmm1, [edi + 4096]
    xor ebx, ebx
    jmp exit

partial:
    vmovdqu xmm0, [value]
    vmovdqu xmm1, [mask_partial]
    cmp byte [eax], 'r'
    je read_partial
fault_write_partial:
    vmaskmovps [edi + 4096], xmm1, xmm0
    jmp fail_access
read_partial:
fault_read_partial:
    vmaskmovps xmm0, xmm1, [edi + 4096]
    jmp fail_access

full:
    vmovdqu xmm0, [value]
    vmovdqu xmm1, [mask_full]
    cmp byte [eax], 'r'
    je read_full
fault_write_full:
    vmaskmovps [edi + 4096], xmm1, xmm0
    jmp fail_access
read_full:
fault_read_full:
    vmaskmovps xmm0, xmm1, [edi + 4096]
    jmp fail_access

; i386 SA_SIGINFO: handler(signo, siginfo, ucontext). The handler exits so it
; only needs to prove that the selected-element access raised SIGSEGV.
segv_handler:
    mov eax, [initial_sp]
    mov eax, [eax + 8]
    cmp byte [eax + 1], 'z'
    je fail_signal
    cmp byte [eax + 1], 's'
    je fail_signal
    cmp dword [esp + 4], SIGSEGV
    jne fail_signal
    mov eax, [esp + 8]
    cmp dword [eax + 8], 2 ; SEGV_ACCERR
    jne fail_signal
    lea edx, [edi + 4096]
    cmp dword [eax + 12], edx
    jne fail_signal
    xor ebx, ebx
    jmp exit

fail_access:
    mov ebx, 12
    jmp exit
fail_map:
    mov ebx, 10
    jmp exit
fail_protect:
    mov ebx, 11
    jmp exit
fail_args:
    mov ebx, 13
    jmp exit
fail_signal:
    mov ebx, 14
exit:
    mov eax, __NR_exit
    int 0x80

section .data
initial_sp: dd 0
align 4
segv_action:
    dd segv_handler, SA_SIGINFO, 0
    dq 0
align 16
value: dd 1, 2, 3, 4
%if WIDE
    dd 5, 6, 7, 8
%endif
mask_partial:
%if QUAD
    dd 0, 0x80000000, 0, 0
%else
    dd 0x80000000, 0, 0, 0
%endif
%if WIDE
    dd 0, 0, 0, 0
%endif
mask_full: dd 0x80000000, 0x80000000, 0x80000000, 0x80000000
%if WIDE
    dd 0x80000000, 0x80000000, 0x80000000, 0x80000000
%endif
