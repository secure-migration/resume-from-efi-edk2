  DEFAULT REL
  SECTION .text

extern ASM_PFX(gSavedCR3)
extern ASM_PFX(gSavedCR4)
extern ASM_PFX(gSavedRIP)
extern ASM_PFX(gSavedGDT)
extern ASM_PFX(gSavedContext)

; Based on struct pt_regs from Linux
%define PT_REGS_R15     0
%define PT_REGS_R14     8
%define PT_REGS_R13     16
%define PT_REGS_R12     24
%define PT_REGS_BP      32
%define PT_REGS_BX      40
%define PT_REGS_R11     48
%define PT_REGS_R10     56
%define PT_REGS_R9      64
%define PT_REGS_R8      72
%define PT_REGS_CX      88
%define PT_REGS_DX      96
%define PT_REGS_SI      104
%define PT_REGS_DI      112
%define PT_REGS_FLAGS   144
%define PT_REGS_SP      152

%define X86_CR4_PGE     BIT7

; TODO?	.align PAGE_SIZE

global ASM_PFX(RestoreRegisters)
ASM_PFX(RestoreRegisters):

    mov     r9, gSavedCR3
    mov     cr3, r9

    mov     rax, gSavedCR4
    mov     rdx, rax
    and     rdx, ~X86_CR4_PGE
    mov     cr4, rdx    ; turn off PGE
    mov     rcx, cr3    ; flush TLB
    mov     cr3, rcx    ; flush TLB
    mov     cr4, rax    ; turn PGE back on

    mov     rax, gSavedContext

    ; Restore all registers except rax
    mov     rsp, [rax + PT_REGS_SP]
    mov     rbp, [rax + PT_REGS_BP]
    mov     rsi, [rax + PT_REGS_SI]
    mov     rdi, [rax + PT_REGS_DI]
    mov     rbx, [rax + PT_REGS_BX]
    mov     rcx, [rax + PT_REGS_CX]
    mov     rdx, [rax + PT_REGS_DX]
    mov     r8,  [rax + PT_REGS_R8]
    mov     r9,  [rax + PT_REGS_R9]
    mov     r10, [rax + PT_REGS_R10]
    mov     r11, [rax + PT_REGS_R11]
    mov     r12, [rax + PT_REGS_R12]
    mov     r13, [rax + PT_REGS_R13]
    mov     r14, [rax + PT_REGS_R14]
    mov     r15, [rax + PT_REGS_R15]

    ; Restore flags
    push    qword [rax + PT_REGS_FLAGS]
    popf

    ; Restore GDT
    lgdt    [gSavedGDT]

    mov     rax, gSavedRIP
    jmp     rax
