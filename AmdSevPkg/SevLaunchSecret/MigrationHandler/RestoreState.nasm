  DEFAULT REL
  SECTION .text

extern ASM_PFX(MyTarget)
extern ASM_PFX(gSavedCR0)
extern ASM_PFX(gSavedCR2)
extern ASM_PFX(gSavedCR3)
extern ASM_PFX(gSavedCR4)
extern ASM_PFX(gSavedRIP)
extern ASM_PFX(gSavedGDTDesc)
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

;
;arg 1:Char to print
;
%macro DEBUG_PUT_CHAR   1
    mov     dx, 0x402
    mov     al, %1
    out     dx, al
%endmacro


ALIGN EFI_PAGE_SIZE
global ASM_PFX(RestoreRegisters)
ASM_PFX(RestoreRegisters):

    DEBUG_PUT_CHAR 'A'
    mov     r9, [gSavedCR3]
    DEBUG_PUT_CHAR '2'
    mov     cr3, r9

    DEBUG_PUT_CHAR 'B'
    mov     r9, [gSavedCR0]
    mov     cr0, r9
    DEBUG_PUT_CHAR 'C'
    mov     r9, [gSavedCR2]
    mov     cr2, r9

    DEBUG_PUT_CHAR 'D'
    mov     rax, [gSavedCR4]
    mov     rdx, rax
    and     rdx, ~X86_CR4_PGE
    mov     cr4, rdx    ; turn off PGE
    DEBUG_PUT_CHAR 'E'
    mov     rcx, cr3    ; flush TLB
    mov     cr3, rcx    ; flush TLB
    DEBUG_PUT_CHAR 'F'
    mov     cr4, rax    ; turn PGE back on
    DEBUG_PUT_CHAR 'G'

    ;mov     rax, [gSavedCR3] ; dummy reference
    ;mov     rax, [MyTarget]  ; dummy reference


    DEBUG_PUT_CHAR 'M'
    mov     rax, gSavedContext

    DEBUG_PUT_CHAR 'N'
    ; Restore all registers except rax
    mov     rsp, [rax + PT_REGS_SP]
    mov     rbp, [rax + PT_REGS_BP]
    mov     rsi, [rax + PT_REGS_SI]
    mov     rdi, [rax + PT_REGS_DI]
    DEBUG_PUT_CHAR 'O'
    mov     rbx, [rax + PT_REGS_BX]
    mov     rcx, [rax + PT_REGS_CX]
    mov     rdx, [rax + PT_REGS_DX]
    mov     r8,  [rax + PT_REGS_R8]
    DEBUG_PUT_CHAR 'P'
    mov     r9,  [rax + PT_REGS_R9]
    mov     r10, [rax + PT_REGS_R10]
    mov     r11, [rax + PT_REGS_R11]
    mov     r12, [rax + PT_REGS_R12]
    DEBUG_PUT_CHAR 'Q'
    mov     r13, [rax + PT_REGS_R13]
    mov     r14, [rax + PT_REGS_R14]
    mov     r15, [rax + PT_REGS_R15]
    DEBUG_PUT_CHAR 'R'

    ; Restore flags
    push    qword [rax + PT_REGS_FLAGS]
    popf

    DEBUG_PUT_CHAR 'S'
    mov     ax, 0x0010
    mov     cs, ax
    DEBUG_PUT_CHAR 'T'
    mov     ax, 0x0018
    mov     ss, ax
    DEBUG_PUT_CHAR 'U'

    DEBUG_PUT_CHAR 'V'
    xor     ax, ax
    mov     es, ax
    mov     ds, ax
    DEBUG_PUT_CHAR 'W'
    mov     fs, ax
    mov     gs, ax

    DEBUG_PUT_CHAR 'X'
    ; Restore GDT
    lgdt    [gSavedGDTDesc]

    DEBUG_PUT_CHAR 'Y'
    mov     rax, [gSavedRIP]
    DEBUG_PUT_CHAR 'Z'
    jmp     rax
    DEBUG_PUT_CHAR 'a'
    ret



  SECTION .data

global ASM_PFX(pgd)
global ASM_PFX(pud)
global ASM_PFX(pmd)
global ASM_PFX(pte)

ALIGN EFI_PAGE_SIZE
ASM_PFX(pgd):
    TIMES EFI_PAGE_SIZE DB 0

ALIGN EFI_PAGE_SIZE
ASM_PFX(pud):
    TIMES EFI_PAGE_SIZE DB 0

ALIGN EFI_PAGE_SIZE
ASM_PFX(pmd):
    TIMES EFI_PAGE_SIZE DB 0

ALIGN EFI_PAGE_SIZE
ASM_PFX(pte):
    TIMES EFI_PAGE_SIZE DB 0
