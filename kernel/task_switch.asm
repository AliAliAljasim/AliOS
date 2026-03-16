bits 32

global task_switch
extern current_task

; ── task_t field offsets (must match task.h) ─────────────────────────────────
;
;   Verified at compile time by _Static_assert in task.c.
;
%define TASK_ESP      8    ; uint32_t esp      — saved kernel stack pointer
%define TASK_PAGEDIR 16    ; uint32_t page_dir — physical address for CR3

; ── void task_switch(task_t *prev, task_t *next) ─────────────────────────────
;
; Saves the callee-saved registers of 'prev' onto its kernel stack, records
; the stack pointer in prev->esp, then loads next->esp and restores next's
; callee-saved registers before returning into next's saved EIP.
;
; Callee-saved registers (System V i386 ABI): EBX, ESI, EDI, EBP.
; EAX, ECX, EDX are caller-saved and are NOT preserved across this call;
; the compiler saves them before calling if they contain live values.
;
; Stack layout after the four pushes below (low address → high address):
;
;   [ESP+ 0]  EBX   ← pushed last
;   [ESP+ 4]  EDI
;   [ESP+ 8]  ESI
;   [ESP+12]  EBP
;   [ESP+16]  return address (pushed by the call instruction)
;   [ESP+20]  prev  (argument 1)
;   [ESP+24]  next  (argument 2)
;
task_switch:
    ; ── Save prev's callee-saved registers ───────────────────────────────
    push ebp
    push esi
    push edi
    push ebx

    ; ── Save prev's stack pointer ─────────────────────────────────────────
    ; EAX = prev (arg 1 is now at [ESP+20] after four pushes)
    mov  eax, [esp + 20]
    mov  [eax + TASK_ESP], esp      ; prev->esp = current ESP

    ; ── Load next's stack pointer ─────────────────────────────────────────
    ; EAX = next (arg 2 is at [ESP+24]; ESP has not changed yet, so this
    ; still reads from prev's stack — correct).
    mov  eax, [esp + 24]            ; eax = next

    ; ── Switch CR3 if page directories differ ────────────────────────────
    ; Skipping the MOV CR3 when unnecessary avoids a full TLB flush, which
    ; is expensive.  When next->page_dir is 0 (boot context before paging
    ; is fully initialised) we also skip to avoid loading CR3 = 0.
    mov  ecx, [eax + TASK_PAGEDIR]  ; ecx = next->page_dir
    test ecx, ecx
    jz   .switch_stack              ; 0 → don't touch CR3
    mov  edx, cr3
    cmp  ecx, edx
    je   .switch_stack              ; same directory → skip TLB flush
    mov  cr3, ecx

.switch_stack:
    ; ── Switch to next's kernel stack ────────────────────────────────────
    mov  esp, [eax + TASK_ESP]      ; ESP = next->esp

    ; ── Update current_task ──────────────────────────────────────────────
    mov  [current_task], eax        ; current_task = next

    ; ── Restore next's callee-saved registers ────────────────────────────
    ; These were pushed by a previous call to task_switch (or by task_create
    ; for a brand-new task, which initialises them all to 0).
    pop  ebx
    pop  edi
    pop  esi
    pop  ebp

    ; ── Return into next's saved EIP ─────────────────────────────────────
    ; For an existing task this resumes execution right after its last call
    ; to task_switch.  For a new task this jumps to the entry function.
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
