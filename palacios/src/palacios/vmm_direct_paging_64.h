/*
 * This file is part of the Palacios Virtual Machine Monitor developed
 * by the V3VEE Project with funding from the United States National
 * Science Foundation and the Department of Energy.
 *
 * The V3VEE Project is a joint project between Northwestern University
 * and the University of New Mexico.  You can find out more at
 * http://www.v3vee.org
 *
 * Copyright (c) 2008, Steven Jaconette <stevenjaconette2007@u.northwestern.edu>
 * Copyright (c) 2008, Jack Lange <jarusl@cs.northwestern.edu>
 * Copyright (c) 2008, The V3VEE Project <http://www.v3vee.org>
 * All rights reserved.
 *
 * Author: Steven Jaconette <stevenjaconette2007@u.northwestern.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "V3VEE_LICENSE".
 */

#ifndef __VMM_DIRECT_PAGING_64_H__
#define __VMM_DIRECT_PAGING_64_H__

#include <palacios/vmm_mem.h>
#include <palacios/vmm_paging.h>
#include <palacios/vmm.h>
#include <palacios/vm_guest_mem.h>
#include <palacios/vm_guest.h>



static inline int handle_passthrough_pagefault_64(struct guest_info * info, 
						  addr_t fault_addr, 
						  pf_error_t error_code) {
    pml4e64_t * pml = NULL;
    pdpe64_t * pdpe = NULL;
    pde64_t * pde = NULL;
    pte64_t * pte = NULL;
    addr_t host_addr = 0;

    int pml_index = PML4E64_INDEX(fault_addr);
    int pdpe_index = PDPE64_INDEX(fault_addr);
    int pde_index = PDE64_INDEX(fault_addr);
    int pte_index = PTE64_INDEX(fault_addr);


    

    struct v3_shadow_region * region =  v3_get_shadow_region(info->vm_info, info->cpu_id, fault_addr);
  
    if (region == NULL) {
	PrintError("Invalid region in passthrough page fault 64, addr=%p\n", 
		   (void *)fault_addr);
	return -1;
    }

    host_addr = v3_get_shadow_addr(region, info->cpu_id, fault_addr);
    //

    // Lookup the correct PML address based on the PAGING MODE
    if (info->shdw_pg_mode == SHADOW_PAGING) {
	pml = CR3_TO_PML4E64_VA(info->ctrl_regs.cr3);
    } else {
	pml = CR3_TO_PML4E64_VA(info->direct_map_pt);
    }

    //Fix up the PML entry
    if (pml[pml_index].present == 0) {
	pdpe = (pdpe64_t *)create_generic_pt_page();
   
	// Set default PML Flags...
	pml[pml_index].present = 1;
        pml[pml_index].writable = 1;
        pml[pml_index].user_page = 1;

	pml[pml_index].pdp_base_addr = PAGE_BASE_ADDR((addr_t)V3_PAddr(pdpe));    
    } else {
	pdpe = V3_VAddr((void*)BASE_TO_PAGE_ADDR(pml[pml_index].pdp_base_addr));
    }

    // Fix up the PDPE entry
    if (pdpe[pdpe_index].present == 0) {
	pde = (pde64_t *)create_generic_pt_page();
	
	// Set default PDPE Flags...
	pdpe[pdpe_index].present = 1;
	pdpe[pdpe_index].writable = 1;
	pdpe[pdpe_index].user_page = 1;

	pdpe[pdpe_index].pd_base_addr = PAGE_BASE_ADDR((addr_t)V3_PAddr(pde));    
    } else {
	pde = V3_VAddr((void*)BASE_TO_PAGE_ADDR(pdpe[pdpe_index].pd_base_addr));
    }


    // Fix up the PDE entry
    if (pde[pde_index].present == 0) {
	pte = (pte64_t *)create_generic_pt_page();
	
	pde[pde_index].present = 1;
	pde[pde_index].writable = 1;
	pde[pde_index].user_page = 1;
	
	pde[pde_index].pt_base_addr = PAGE_BASE_ADDR((addr_t)V3_PAddr(pte));
    } else {
	pte = V3_VAddr((void*)BASE_TO_PAGE_ADDR(pde[pde_index].pt_base_addr));
    }


    // Fix up the PTE entry
    if (pte[pte_index].present == 0) {
	pte[pte_index].user_page = 1;
	
	if ((region->flags.alloced == 1) && 
	    (region->flags.read == 1)) {
	    // Full access
	    pte[pte_index].present = 1;

	    if (region->flags.write == 1) {
		pte[pte_index].writable = 1;
	    } else {
		pte[pte_index].writable = 0;
	    }

	    pte[pte_index].page_base_addr = PAGE_BASE_ADDR(host_addr);
	}
    }
   
    if (region->flags.hook == 1) {
	if ((error_code.write == 1) || (region->flags.read == 0)) {
	    return v3_handle_mem_hook(info, fault_addr, fault_addr, region, error_code);
	}
    }

    return 0;
}

static inline int invalidate_addr_64(struct guest_info * info, addr_t inv_addr) {
    pml4e64_t * pml = NULL;
    pdpe64_t * pdpe = NULL;
    pde64_t * pde = NULL;
    pte64_t * pte = NULL;


    // TODO:
    // Call INVLPGA

    // clear the page table entry
    int pml_index = PML4E64_INDEX(inv_addr);
    int pdpe_index = PDPE64_INDEX(inv_addr);
    int pde_index = PDE64_INDEX(inv_addr);
    int pte_index = PTE64_INDEX(inv_addr);

    
    // Lookup the correct PDE address based on the PAGING MODE
    if (info->shdw_pg_mode == SHADOW_PAGING) {
	pml = CR3_TO_PML4E64_VA(info->ctrl_regs.cr3);
    } else {
	pml = CR3_TO_PML4E64_VA(info->direct_map_pt);
    }

    if (pml[pml_index].present == 0) {
	return 0;
    }

    pdpe = V3_VAddr((void*)BASE_TO_PAGE_ADDR(pml[pml_index].pdp_base_addr));

    if (pdpe[pdpe_index].present == 0) {
	return 0;
    } else if (pdpe[pdpe_index].large_page == 1) {
	pdpe[pdpe_index].present = 0;
	return 0;
    }

    pde = V3_VAddr((void*)BASE_TO_PAGE_ADDR(pdpe[pdpe_index].pd_base_addr));

    if (pde[pde_index].present == 0) {
	return 0;
    } else if (pde[pde_index].large_page == 1) {
	pde[pde_index].present = 0;
	return 0;
    }

    pte = V3_VAddr((void*)BASE_TO_PAGE_ADDR(pde[pde_index].pt_base_addr));

    pte[pte_index].present = 0;

    return 0;
}



#endif
