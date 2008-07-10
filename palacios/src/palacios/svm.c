#include <palacios/svm.h>
#include <palacios/vmm.h>

#include <palacios/vmcb.h>
#include <palacios/vmm_mem.h>
#include <palacios/vmm_paging.h>
#include <palacios/svm_handler.h>

#include <palacios/vmm_debug.h>
#include <palacios/vm_guest_mem.h>

#include <palacios/vmm_decoder.h>


extern struct vmm_os_hooks * os_hooks;

extern uint_t cpuid_ecx(uint_t op);
extern uint_t cpuid_edx(uint_t op);
extern void Get_MSR(uint_t MSR, uint_t * high_byte, uint_t * low_byte); 
extern void Set_MSR(uint_t MSR, uint_t high_byte, uint_t low_byte);
extern uint_t launch_svm(vmcb_t * vmcb_addr);
extern void safe_svm_launch(vmcb_t * vmcb_addr, struct v3_gprs * gprs);

extern void STGI();
extern void CLGI();

extern uint_t Get_CR3();


extern void DisableInts();







static vmcb_t * Allocate_VMCB() {
  vmcb_t * vmcb_page = (vmcb_t*)os_hooks->allocate_pages(1);


  memset(vmcb_page, 0, 4096);

  return vmcb_page;
}





static void Init_VMCB_BIOS(vmcb_t * vmcb, struct guest_info vm_info) {
  vmcb_ctrl_t * ctrl_area = GET_VMCB_CTRL_AREA(vmcb);
  vmcb_saved_state_t * guest_state = GET_VMCB_SAVE_STATE_AREA(vmcb);
  uint_t i;


  guest_state->rsp = vm_info.vm_regs.rsp;
  // guest_state->rip = vm_info.rip;
  guest_state->rip = 0xfff0;

  guest_state->cpl = 0;

  //ctrl_area->instrs.instrs.CR0 = 1;
  ctrl_area->cr_reads.cr0 = 1;
  ctrl_area->cr_writes.cr0 = 1;

  guest_state->efer |= EFER_MSR_svm_enable;
  guest_state->rflags = 0x00000002; // The reserved bit is always 1
  ctrl_area->svm_instrs.VMRUN = 1;
  ctrl_area->instrs.HLT = 1;
  // guest_state->cr0 = 0x00000001;    // PE 
  ctrl_area->guest_ASID = 1;

  ctrl_area->exceptions.de = 1;
  ctrl_area->exceptions.df = 1;
  ctrl_area->exceptions.pf = 1;
  ctrl_area->exceptions.ts = 1;
  ctrl_area->exceptions.ss = 1;
  ctrl_area->exceptions.ac = 1;
  ctrl_area->exceptions.mc = 1;
  ctrl_area->exceptions.gp = 1;
  ctrl_area->exceptions.ud = 1;
  ctrl_area->exceptions.np = 1;
  ctrl_area->exceptions.of = 1;
  ctrl_area->exceptions.nmi = 1;

  vm_info.vm_regs.rdx = 0x00000f00;

  guest_state->cr0 = 0x60000010;

  guest_state->cs.selector = 0xf000;
  guest_state->cs.limit=0xffff;
  guest_state->cs.base = 0x0000000f0000LL;
  guest_state->cs.attrib.raw = 0xf3;

  
  struct vmcb_selector *segregs [] = {&(guest_state->ss), &(guest_state->ds), &(guest_state->es), &(guest_state->fs), &(guest_state->gs), NULL};
  for ( i = 0; segregs[i] != NULL; i++) {
    struct vmcb_selector * seg = segregs[i];
    
    seg->selector = 0x0000;
    //    seg->base = seg->selector << 4;
    seg->base = 0x00000000;
    seg->attrib.raw = 0xf3;
    seg->limit = ~0u;
  }
  
  guest_state->gdtr.limit = 0x0000ffff;
  guest_state->gdtr.base = 0x0000000000000000LL;
  guest_state->idtr.limit = 0x0000ffff;
  guest_state->idtr.base = 0x0000000000000000LL;

  guest_state->ldtr.selector = 0x0000;
  guest_state->ldtr.limit = 0x0000ffff;
  guest_state->ldtr.base = 0x0000000000000000LL;
  guest_state->tr.selector = 0x0000;
  guest_state->tr.limit = 0x0000ffff;
  guest_state->tr.base = 0x0000000000000000LL;


  guest_state->dr6 = 0x00000000ffff0ff0LL;
  guest_state->dr7 = 0x0000000000000400LL;

  if (vm_info.io_map.num_ports > 0) {
    vmm_io_hook_t * iter;
    addr_t io_port_bitmap;
    
    io_port_bitmap = (addr_t)os_hooks->allocate_pages(3);
    memset((uchar_t*)io_port_bitmap, 0, PAGE_SIZE * 3);
    
    ctrl_area->IOPM_BASE_PA = io_port_bitmap;

    //PrintDebug("Setting up IO Map at 0x%x\n", io_port_bitmap);

    FOREACH_IO_HOOK(vm_info.io_map, iter) {
      ushort_t port = iter->port;
      uchar_t * bitmap = (uchar_t *)io_port_bitmap;

      bitmap += (port / 8);
      PrintDebug("Setting Bit for port 0x%x\n", port);
      *bitmap |= 1 << (port % 8);
    }


    //PrintDebugMemDump((uchar_t*)io_port_bitmap, PAGE_SIZE *2);

    ctrl_area->instrs.IOIO_PROT = 1;
  }



  PrintDebug("Exiting on interrupts\n");
  ctrl_area->guest_ctrl.V_INTR_MASKING = 1;
  ctrl_area->instrs.INTR = 1;


  if (vm_info.shdw_pg_mode == SHADOW_PAGING) {
    PrintDebug("Creating initial shadow page table\n");
    vm_info.shdw_pg_state.shadow_cr3 |= ((addr_t)create_passthrough_pde32_pts(&vm_info) & ~0xfff);
    PrintDebug("Created\n");

    guest_state->cr3 = vm_info.shdw_pg_state.shadow_cr3;

    //PrintDebugPageTables((pde32_t*)(vm_info.shdw_pg_state.shadow_cr3.e_reg.low));

    ctrl_area->cr_reads.cr3 = 1;
    ctrl_area->cr_writes.cr3 = 1;


    ctrl_area->instrs.INVLPG = 1;
    ctrl_area->instrs.INVLPGA = 1;

    /* JRL: This is a performance killer, and a simplistic solution */
    /* We need to fix this */
    ctrl_area->TLB_CONTROL = 1;
    


    guest_state->g_pat = 0x7040600070406ULL;

    guest_state->cr0 |= 0x80000000;

  } else if (vm_info.shdw_pg_mode == NESTED_PAGING) {
    // Flush the TLB on entries/exits


    // Enable Nested Paging
    //ctrl_area->NP_ENABLE = 1;

    //PrintDebug("NP_Enable at 0x%x\n", &(ctrl_area->NP_ENABLE));

        // Set the Nested Page Table pointer
    //    ctrl_area->N_CR3 = ((addr_t)vm_info.page_tables);
    // ctrl_area->N_CR3 = (addr_t)(vm_info.page_tables);

    //   ctrl_area->N_CR3 = Get_CR3();
    // guest_state->cr3 |= (Get_CR3() & 0xfffff000);

    //    guest_state->g_pat = 0x7040600070406ULL;
  }



}









static int init_svm_guest(struct guest_info *info) {
 
  PrintDebug("Allocating VMCB\n");
  info->vmm_data = (void*)Allocate_VMCB();


  //PrintDebug("Generating Guest nested page tables\n");
  //  info->page_tables = NULL;
  //info->page_tables = generate_guest_page_tables_64(&(info->mem_layout), &(info->mem_list));
  //info->page_tables = generate_guest_page_tables(&(info->mem_layout), &(info->mem_list));
  //  PrintDebugPageTables(info->page_tables);


  PrintDebug("Initializing VMCB (addr=%x)\n", info->vmm_data);
  Init_VMCB_BIOS((vmcb_t*)(info->vmm_data), *info);
  

  //  info->rip = 0;

  info->vm_regs.rdi = 0;
  info->vm_regs.rsi = 0;
  info->vm_regs.rbp = 0;
  info->vm_regs.rsp = 0;
  info->vm_regs.rbx = 0;
  info->vm_regs.rdx = 0;
  info->vm_regs.rcx = 0;
  info->vm_regs.rax = 0;
  
  return 0;
}


// can we start a kernel thread here...
static int start_svm_guest(struct guest_info *info) {
  vmcb_saved_state_t * guest_state = GET_VMCB_SAVE_STATE_AREA((vmcb_t*)(info->vmm_data));
  vmcb_ctrl_t * guest_ctrl = GET_VMCB_CTRL_AREA((vmcb_t*)(info->vmm_data));

  PrintDebug("Launching SVM VM (vmcb=%x)\n", info->vmm_data);
  //PrintDebugVMCB((vmcb_t*)(info->vmm_data));

  while (1) {
    ullong_t tmp_tsc;


    CLGI();

    PrintDebug("SVM Entry to rip=%x...\n", info->rip);

    rdtscll(info->time_state.cached_host_tsc);
    guest_ctrl->TSC_OFFSET = info->time_state.guest_tsc - info->time_state.cached_host_tsc;

    safe_svm_launch((vmcb_t*)(info->vmm_data), &(info->vm_regs));

    rdtscll(tmp_tsc);
    //PrintDebug("SVM Returned\n");


    v3_update_time(info, tmp_tsc - info->time_state.cached_host_tsc);

    STGI();

     
    if (handle_svm_exit(info) != 0) {

      addr_t host_addr;
      addr_t linear_addr = 0;

      PrintDebug("SVM ERROR!!\n"); 
      
      PrintDebug("RIP: %x\n", guest_state->rip);


      linear_addr = get_addr_linear(info, guest_state->rip, &(info->segments.cs));


      PrintDebug("RIP Linear: %x\n", linear_addr);

      
      if (info->mem_mode == PHYSICAL_MEM) {
	guest_pa_to_host_pa(info, linear_addr, &host_addr);
      } else if (info->mem_mode == VIRTUAL_MEM) {
	guest_va_to_host_pa(info, linear_addr, &host_addr);
      }


      PrintDebug("Host Address of rip = 0x%x\n", host_addr);

      PrintDebug("Instr (15 bytes) at %x:\n", host_addr);
      PrintTraceMemDump((char*)host_addr, 15);

      break;
    }
  }
  return 0;
}




/* Checks machine SVM capability */
/* Implemented from: AMD Arch Manual 3, sect 15.4 */ 
int is_svm_capable() {

#if 1
  // Dinda

  uint_t ret;
  uint_t vm_cr_low = 0, vm_cr_high = 0;


  ret =  cpuid_ecx(CPUID_FEATURE_IDS);
  
  PrintDebug("CPUID_FEATURE_IDS_ecx=0x%x\n",ret);

  if ((ret & CPUID_FEATURE_IDS_ecx_svm_avail) == 0) {
    PrintDebug("SVM Not Available\n");
    return 0;
  }  else {
    Get_MSR(SVM_VM_CR_MSR, &vm_cr_high, &vm_cr_low);
    
    PrintDebug("SVM_VM_CR_MSR = 0x%x 0x%x\n",vm_cr_high,vm_cr_low);
    
    if ((vm_cr_low & SVM_VM_CR_MSR_svmdis) == 1) {
      PrintDebug("SVM is available but is disabled.\n");

      ret = cpuid_edx(CPUID_SVM_REV_AND_FEATURE_IDS);
      
      PrintDebug("CPUID_FEATURE_IDS_edx=0x%x\n",ret);
      
      if ((ret & CPUID_SVM_REV_AND_FEATURE_IDS_edx_svml) == 0) {
	PrintDebug("SVM BIOS Disabled, not unlockable\n");
      } else {
	PrintDebug("SVM is locked with a key\n");
      }
      return 0;

    } else {
      PrintDebug("SVM is available and  enabled.\n");

      ret = cpuid_edx(CPUID_SVM_REV_AND_FEATURE_IDS);
      
      PrintDebug("CPUID_FEATURE_IDS_edx=0x%x\n",ret);

      if ((ret & CPUID_SVM_REV_AND_FEATURE_IDS_edx_np) == 0) {
	PrintDebug("SVM Nested Paging not supported\n");
      } else {
	PrintDebug("SVM Nested Paging supported\n");
      }
      
      return 1;
      
    }
  }

#else

  uint_t ret =  cpuid_ecx(CPUID_FEATURE_IDS);
  uint_t vm_cr_low = 0, vm_cr_high = 0;


  if ((ret & CPUID_FEATURE_IDS_ecx_svm_avail) == 0) {
    PrintDebug("SVM Not Available\n");
    return 0;
  } 

  Get_MSR(SVM_VM_CR_MSR, &vm_cr_high, &vm_cr_low);

  PrintDebug("SVM_VM_CR_MSR = 0x%x 0x%x\n",vm_cr_high,vm_cr_low);


  // this part is clearly wrong, since the np bit is in 
  // edx, not ecx
  if ((ret & CPUID_SVM_REV_AND_FEATURE_IDS_edx_np) == 1) {
    PrintDebug("Nested Paging not supported\n");
  } else {
    PrintDebug("Nested Paging supported\n");
  }

  if ((vm_cr_low & SVM_VM_CR_MSR_svmdis) == 0) {
    PrintDebug("SVM is disabled.\n");
    return 1;
  }

  ret = cpuid_edx(CPUID_SVM_REV_AND_FEATURE_IDS);

  if ((ret & CPUID_SVM_REV_AND_FEATURE_IDS_edx_svml) == 0) {
    PrintDebug("SVM BIOS Disabled, not unlockable\n");
  } else {
    PrintDebug("SVM is locked with a key\n");
  }

  return 0;

#endif

}



void Init_SVM(struct vmm_ctrl_ops * vmm_ops) {
  reg_ex_t msr;
  void * host_state;


  // Enable SVM on the CPU
  Get_MSR(EFER_MSR, &(msr.e_reg.high), &(msr.e_reg.low));
  msr.e_reg.low |= EFER_MSR_svm_enable;
  Set_MSR(EFER_MSR, 0, msr.e_reg.low);
  
  PrintDebug("SVM Enabled\n");


  // Setup the host state save area
  host_state = os_hooks->allocate_pages(4);
  
  msr.e_reg.high = 0;
  msr.e_reg.low = (uint_t)host_state;


  PrintDebug("Host State being saved at %x\n", (uint_t)host_state);
  Set_MSR(SVM_VM_HSAVE_PA_MSR, msr.e_reg.high, msr.e_reg.low);



  // Setup the SVM specific vmm operations
  vmm_ops->init_guest = &init_svm_guest;
  vmm_ops->start_guest = &start_svm_guest;


  return;
}




















































/*static void Init_VMCB(vmcb_t * vmcb, struct guest_info vm_info) {
  vmcb_ctrl_t * ctrl_area = GET_VMCB_CTRL_AREA(vmcb);
  vmcb_saved_state_t * guest_state = GET_VMCB_SAVE_STATE_AREA(vmcb);
  uint_t i;


  guest_state->rsp = vm_info.vm_regs.rsp;
  guest_state->rip = vm_info.rip;


  //ctrl_area->instrs.instrs.CR0 = 1;
  ctrl_area->cr_reads.cr0 = 1;
  ctrl_area->cr_writes.cr0 = 1;

  guest_state->efer |= EFER_MSR_svm_enable;
  guest_state->rflags = 0x00000002; // The reserved bit is always 1
  ctrl_area->svm_instrs.VMRUN = 1;
  // guest_state->cr0 = 0x00000001;    // PE 
  ctrl_area->guest_ASID = 1;


  ctrl_area->exceptions.de = 1;
  ctrl_area->exceptions.df = 1;
  ctrl_area->exceptions.pf = 1;
  ctrl_area->exceptions.ts = 1;
  ctrl_area->exceptions.ss = 1;
  ctrl_area->exceptions.ac = 1;
  ctrl_area->exceptions.mc = 1;
  ctrl_area->exceptions.gp = 1;
  ctrl_area->exceptions.ud = 1;
  ctrl_area->exceptions.np = 1;
  ctrl_area->exceptions.of = 1;
  ctrl_area->exceptions.nmi = 1;

  guest_state->cs.selector = 0x0000;
  guest_state->cs.limit=~0u;
  guest_state->cs.base = guest_state->cs.selector<<4;
  guest_state->cs.attrib.raw = 0xf3;

  
  struct vmcb_selector *segregs [] = {&(guest_state->ss), &(guest_state->ds), &(guest_state->es), &(guest_state->fs), &(guest_state->gs), NULL};
  for ( i = 0; segregs[i] != NULL; i++) {
    struct vmcb_selector * seg = segregs[i];
    
    seg->selector = 0x0000;
    seg->base = seg->selector << 4;
    seg->attrib.raw = 0xf3;
    seg->limit = ~0u;
  }
  
  if (vm_info.io_map.num_ports > 0) {
    vmm_io_hook_t * iter;
    addr_t io_port_bitmap;
    
    io_port_bitmap = (addr_t)os_hooks->allocate_pages(3);
    memset((uchar_t*)io_port_bitmap, 0, PAGE_SIZE * 3);
    
    ctrl_area->IOPM_BASE_PA = io_port_bitmap;

    //PrintDebug("Setting up IO Map at 0x%x\n", io_port_bitmap);

    FOREACH_IO_HOOK(vm_info.io_map, iter) {
      ushort_t port = iter->port;
      uchar_t * bitmap = (uchar_t *)io_port_bitmap;

      bitmap += (port / 8);
      PrintDebug("Setting Bit in block %x\n", bitmap);
      *bitmap |= 1 << (port % 8);
    }


    //PrintDebugMemDump((uchar_t*)io_port_bitmap, PAGE_SIZE *2);

    ctrl_area->instrs.IOIO_PROT = 1;
  }

  ctrl_area->instrs.INTR = 1;



  if (vm_info.page_mode == SHADOW_PAGING) {
    PrintDebug("Creating initial shadow page table\n");
    vm_info.shdw_pg_state.shadow_cr3 |= ((addr_t)create_passthrough_pde32_pts(&vm_info) & ~0xfff);
    PrintDebug("Created\n");

    guest_state->cr3 = vm_info.shdw_pg_state.shadow_cr3;

    ctrl_area->cr_reads.cr3 = 1;
    ctrl_area->cr_writes.cr3 = 1;


    ctrl_area->instrs.INVLPG = 1;
    ctrl_area->instrs.INVLPGA = 1;

    guest_state->g_pat = 0x7040600070406ULL;

    guest_state->cr0 |= 0x80000000;
  } else if (vm_info.page_mode == NESTED_PAGING) {
    // Flush the TLB on entries/exits
    //ctrl_area->TLB_CONTROL = 1;

    // Enable Nested Paging
    //ctrl_area->NP_ENABLE = 1;

    //PrintDebug("NP_Enable at 0x%x\n", &(ctrl_area->NP_ENABLE));

        // Set the Nested Page Table pointer
    //    ctrl_area->N_CR3 = ((addr_t)vm_info.page_tables);
    // ctrl_area->N_CR3 = (addr_t)(vm_info.page_tables);

    //   ctrl_area->N_CR3 = Get_CR3();
    // guest_state->cr3 |= (Get_CR3() & 0xfffff000);

    //    guest_state->g_pat = 0x7040600070406ULL;
  }



}
*/







#if 0
void Init_VMCB_pe(vmcb_t *vmcb, struct guest_info vm_info) {
  vmcb_ctrl_t * ctrl_area = GET_VMCB_CTRL_AREA(vmcb);
  vmcb_saved_state_t * guest_state = GET_VMCB_SAVE_STATE_AREA(vmcb);
  uint_t i = 0;


  guest_state->rsp = vm_info.vm_regs.rsp;
  guest_state->rip = vm_info.rip;


  /* I pretty much just gutted this from TVMM */
  /* Note: That means its probably wrong */

  // set the segment registers to mirror ours
  guest_state->cs.selector = 1<<3;
  guest_state->cs.attrib.fields.type = 0xa; // Code segment+read
  guest_state->cs.attrib.fields.S = 1;
  guest_state->cs.attrib.fields.P = 1;
  guest_state->cs.attrib.fields.db = 1;
  guest_state->cs.attrib.fields.G = 1;
  guest_state->cs.limit = 0xfffff;
  guest_state->cs.base = 0;
  
  struct vmcb_selector *segregs [] = {&(guest_state->ss), &(guest_state->ds), &(guest_state->es), &(guest_state->fs), &(guest_state->gs), NULL};
  for ( i = 0; segregs[i] != NULL; i++) {
    struct vmcb_selector * seg = segregs[i];
    
    seg->selector = 2<<3;
    seg->attrib.fields.type = 0x2; // Data Segment+read/write
    seg->attrib.fields.S = 1;
    seg->attrib.fields.P = 1;
    seg->attrib.fields.db = 1;
    seg->attrib.fields.G = 1;
    seg->limit = 0xfffff;
    seg->base = 0;
  }


  {
    /* JRL THIS HAS TO GO */
    
    //    guest_state->tr.selector = GetTR_Selector();
    guest_state->tr.attrib.fields.type = 0x9; 
    guest_state->tr.attrib.fields.P = 1;
    // guest_state->tr.limit = GetTR_Limit();
    //guest_state->tr.base = GetTR_Base();// - 0x2000;
    /* ** */
  }


  /* ** */


  guest_state->efer |= EFER_MSR_svm_enable;
  guest_state->rflags = 0x00000002; // The reserved bit is always 1
  ctrl_area->svm_instrs.VMRUN = 1;
  guest_state->cr0 = 0x00000001;    // PE 
  ctrl_area->guest_ASID = 1;


  //  guest_state->cpl = 0;



  // Setup exits

  ctrl_area->cr_writes.cr4 = 1;
  
  ctrl_area->exceptions.de = 1;
  ctrl_area->exceptions.df = 1;
  ctrl_area->exceptions.pf = 1;
  ctrl_area->exceptions.ts = 1;
  ctrl_area->exceptions.ss = 1;
  ctrl_area->exceptions.ac = 1;
  ctrl_area->exceptions.mc = 1;
  ctrl_area->exceptions.gp = 1;
  ctrl_area->exceptions.ud = 1;
  ctrl_area->exceptions.np = 1;
  ctrl_area->exceptions.of = 1;
  ctrl_area->exceptions.nmi = 1;

  

  ctrl_area->instrs.IOIO_PROT = 1;
  ctrl_area->IOPM_BASE_PA = (uint_t)os_hooks->allocate_pages(3);
  
  {
    reg_ex_t tmp_reg;
    tmp_reg.r_reg = ctrl_area->IOPM_BASE_PA;
    memset((void*)(tmp_reg.e_reg.low), 0xffffffff, PAGE_SIZE * 2);
  }

  ctrl_area->instrs.INTR = 1;

  
  {
    char gdt_buf[6];
    char idt_buf[6];

    memset(gdt_buf, 0, 6);
    memset(idt_buf, 0, 6);


    uint_t gdt_base, idt_base;
    ushort_t gdt_limit, idt_limit;
    
    GetGDTR(gdt_buf);
    gdt_base = *(ulong_t*)((uchar_t*)gdt_buf + 2) & 0xffffffff;
    gdt_limit = *(ushort_t*)(gdt_buf) & 0xffff;
    PrintDebug("GDT: base: %x, limit: %x\n", gdt_base, gdt_limit);

    GetIDTR(idt_buf);
    idt_base = *(ulong_t*)(idt_buf + 2) & 0xffffffff;
    idt_limit = *(ushort_t*)(idt_buf) & 0xffff;
    PrintDebug("IDT: base: %x, limit: %x\n",idt_base, idt_limit);


    // gdt_base -= 0x2000;
    //idt_base -= 0x2000;

    guest_state->gdtr.base = gdt_base;
    guest_state->gdtr.limit = gdt_limit;
    guest_state->idtr.base = idt_base;
    guest_state->idtr.limit = idt_limit;


  }
  
  
  // also determine if CPU supports nested paging
  /*
  if (vm_info.page_tables) {
    //   if (0) {
    // Flush the TLB on entries/exits
    ctrl_area->TLB_CONTROL = 1;

    // Enable Nested Paging
    ctrl_area->NP_ENABLE = 1;

    PrintDebug("NP_Enable at 0x%x\n", &(ctrl_area->NP_ENABLE));

        // Set the Nested Page Table pointer
    ctrl_area->N_CR3 |= ((addr_t)vm_info.page_tables & 0xfffff000);


    //   ctrl_area->N_CR3 = Get_CR3();
    // guest_state->cr3 |= (Get_CR3() & 0xfffff000);

    guest_state->g_pat = 0x7040600070406ULL;

    PrintDebug("Set Nested CR3: lo: 0x%x  hi: 0x%x\n", (uint_t)*(&(ctrl_area->N_CR3)), (uint_t)*((unsigned char *)&(ctrl_area->N_CR3) + 4));
    PrintDebug("Set Guest CR3: lo: 0x%x  hi: 0x%x\n", (uint_t)*(&(guest_state->cr3)), (uint_t)*((unsigned char *)&(guest_state->cr3) + 4));
    // Enable Paging
    //    guest_state->cr0 |= 0x80000000;
  }
  */

}





#endif


