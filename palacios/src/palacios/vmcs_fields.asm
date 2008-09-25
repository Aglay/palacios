;; (c) 2008, Peter Dinda <pdinda@northwestern.edu> 
;; (c) 2008, Jack Lange <jarusl@cs.northwestern.edu> 
;; (c) 2008, The V3VEE Project <http://www.v3vee.org> 


%ifndef VMCS_FIELDS_ASM
%define VMCS_FIELDS_ASM

VMCS_GUEST_ES_SELECTOR equ 0x00000800
VMCS_GUEST_CS_SELECTOR equ 0x00000802
VMCS_GUEST_SS_SELECTOR equ 0x00000804
VMCS_GUEST_DS_SELECTOR equ 0x00000806
VMCS_GUEST_FS_SELECTOR equ 0x00000808
VMCS_GUEST_GS_SELECTOR equ 0x0000080A
VMCS_GUEST_LDTR_SELECTOR equ 0x0000080C
VMCS_GUEST_TR_SELECTOR equ 0x0000080E
VMCS_HOST_ES_SELECTOR equ 0x00000C00
VMCS_HOST_CS_SELECTOR equ 0x00000C02
VMCS_HOST_SS_SELECTOR equ 0x00000C04
VMCS_HOST_DS_SELECTOR equ 0x00000C06
VMCS_HOST_FS_SELECTOR equ 0x00000C08
VMCS_HOST_GS_SELECTOR equ 0x00000C0A
VMCS_HOST_TR_SELECTOR equ 0x00000C0C
IO_BITMAP_A_ADDR equ 0x00002000
IO_BITMAP_A_ADDR_HIGH equ 0x00002001
IO_BITMAP_B_ADDR equ 0x00002002
IO_BITMAP_B_ADDR_HIGH equ 0x00002003
MSR_BITMAPS equ 0x00002004
MSR_BITMAPS_HIGH equ 0x00002005
VM_EXIT_MSR_STORE_ADDR equ 0x00002006
VM_EXIT_MSR_STORE_ADDR_HIGH equ 0x00002007
VM_EXIT_MSR_LOAD_ADDR equ 0x00002008
VM_EXIT_MSR_LOAD_ADDR_HIGH equ 0x00002009
VM_ENTRY_MSR_LOAD_ADDR equ 0x0000200A
VM_ENTRY_MSR_LOAD_ADDR_HIGH equ 0x0000200B
VMCS_EXEC_PTR equ 0x0000200C
VMCS_EXEC_PTR_HIGH equ 0x0000200D
TSC_OFFSET equ 0x00002010
TSC_OFFSET_HIGH equ 0x00002011
VIRT_APIC_PAGE_ADDR equ 0x00002012
VIRT_APIC_PAGE_ADDR_HIGH equ 0x00002013
VMCS_LINK_PTR equ 0x00002800
VMCS_LINK_PTR_HIGH equ 0x00002801
GUEST_IA32_DEBUGCTL equ 0x00002802
GUEST_IA32_DEBUGCTL_HIGH equ 0x00002803
PIN_VM_EXEC_CTRLS equ 0x00004000
PROC_VM_EXEC_CTRLS equ 0x00004002
EXCEPTION_BITMAP equ 0x00004004
PAGE_FAULT_ERROR_MASK equ 0x00004006
PAGE_FAULT_ERROR_MATCH equ 0x00004008
CR3_TARGET_COUNT equ 0x0000400A
VM_EXIT_CTRLS equ 0x0000400C
VM_EXIT_MSR_STORE_COUNT equ 0x0000400E
VM_EXIT_MSR_LOAD_COUNT equ 0x00004010
VM_ENTRY_CTRLS equ 0x00004012
VM_ENTRY_MSR_LOAD_COUNT equ 0x00004014
VM_ENTRY_INT_INFO_FIELD equ 0x00004016
VM_ENTRY_EXCEPTION_ERROR equ 0x00004018
VM_ENTRY_INSTR_LENGTH equ 0x0000401A
TPR_THRESHOLD equ 0x0000401C
VM_INSTR_ERROR equ 0x00004400
EXIT_REASON equ 0x00004402
VM_EXIT_INT_INFO equ 0x00004404
VM_EXIT_INT_ERROR equ 0x00004406
IDT_VECTOR_INFO equ 0x00004408
IDT_VECTOR_ERROR equ 0x0000440A
VM_EXIT_INSTR_LENGTH equ 0x0000440C
VMX_INSTR_INFO equ 0x0000440E
GUEST_ES_LIMIT equ 0x00004800
GUEST_CS_LIMIT equ 0x00004802
GUEST_SS_LIMIT equ 0x00004804
GUEST_DS_LIMIT equ 0x00004806
GUEST_FS_LIMIT equ 0x00004808
GUEST_GS_LIMIT equ 0x0000480A
GUEST_LDTR_LIMIT equ 0x0000480C
GUEST_TR_LIMIT equ 0x0000480E
GUEST_GDTR_LIMIT equ 0x00004810
GUEST_IDTR_LIMIT equ 0x00004812
GUEST_ES_ACCESS equ 0x00004814
GUEST_CS_ACCESS equ 0x00004816
GUEST_SS_ACCESS equ 0x00004818
GUEST_DS_ACCESS equ 0x0000481A
GUEST_FS_ACCESS equ 0x0000481C
GUEST_GS_ACCESS equ 0x0000481E
GUEST_LDTR_ACCESS equ 0x00004820
GUEST_TR_ACCESS equ 0x00004822
GUEST_INT_STATE equ 0x00004824
GUEST_ACTIVITY_STATE equ 0x00004826
GUEST_SMBASE equ 0x00004828
GUEST_IA32_SYSENTER_CS equ 0x0000482A
HOST_IA32_SYSENTER_CS equ 0x00004C00
CR0_GUEST_HOST_MASK equ 0x00006000
CR4_GUEST_HOST_MASK equ 0x00006002
CR0_READ_SHADOW equ 0x00006004
CR4_READ_SHADOW equ 0x00006006
CR3_TARGET_VALUE_0 equ 0x00006008
CR3_TARGET_VALUE_1 equ 0x0000600A
CR3_TARGET_VALUE_2 equ 0x0000600C
CR3_TARGET_VALUE_3 equ 0x0000600E
EXIT_QUALIFICATION equ 0x00006400
IO_RCX equ 0x00006402
IO_RSI equ 0x00006404
IO_RDI equ 0x00006406
IO_RIP equ 0x00006408
GUEST_LINEAR_ADDR equ 0x0000640A
GUEST_CR0 equ 0x00006800
GUEST_CR3 equ 0x00006802
GUEST_CR4 equ 0x00006804
GUEST_ES_BASE equ 0x00006806
GUEST_CS_BASE equ 0x00006808
GUEST_SS_BASE equ 0x0000680A
GUEST_DS_BASE equ 0x0000680C
GUEST_FS_BASE equ 0x0000680E
GUEST_GS_BASE equ 0x00006810
GUEST_LDTR_BASE equ 0x00006812
GUEST_TR_BASE equ 0x00006814
GUEST_GDTR_BASE equ 0x00006816
GUEST_IDTR_BASE equ 0x00006818
GUEST_DR7 equ 0x0000681A
GUEST_RSP equ 0x0000681C
GUEST_RIP equ 0x0000681E
GUEST_RFLAGS equ 0x00006820
GUEST_PENDING_DEBUG_EXCS equ 0x00006822
GUEST_IA32_SYSENTER_ESP equ 0x00006824
GUEST_IA32_SYSENTER_EIP equ 0x00006826
HOST_CR0 equ 0x00006C00
HOST_CR3 equ 0x00006C02
HOST_CR4 equ 0x00006C04
HOST_FS_BASE equ 0x00006C06
HOST_GS_BASE equ 0x00006C08
HOST_TR_BASE equ 0x00006C0A
HOST_GDTR_BASE equ 0x00006C0C
HOST_IDTR_BASE equ 0x00006C0E
HOST_IA32_SYSENTER_ESP equ 0x00006C10
HOST_IA32_SYSENTER_EIP equ 0x00006C12
HOST_RSP equ 0x00006C14
HOST_RIP equ 0x00006C16

%endif

