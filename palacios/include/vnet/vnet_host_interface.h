/* 
 * This file is part of the Palacios Virtual Machine Monitor developed
 * by the V3VEE Project with funding from the United States National 
 * Science Foundation and the Department of Energy.  
 *
 * The V3VEE Project is a joint project between Northwestern University
 * and the University of New Mexico.  You can find out more at 
 * http://www.v3vee.org
 *
 * Copyright (c) 2011, Lei Xia <lxia@northwestern.edu> 
 * Copyright (c) 2011, The V3VEE Project <http://www.v3vee.org> 
 * All rights reserved.
 *
 * Author: Lei Xia <lxia@northwestern.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "V3VEE_LICENSE".
 */

#ifndef __VNET_HOST_INTERFACE_H__
#define __VNET_HOST_INTERFACE_H__

struct vnet_thread {
    void * host_thread;
    void * data;
};

struct vnet_timer {
    void * host_timer;
    void * data;
};

struct vnet_thread * vnet_thread_create(int (*func)(void *), void *arg, char * name);
void vnet_thread_sleep(int cond);
void vnet_thread_wakeup(struct vnet_thread *);
void vnet_thread_kill(struct vnet_thread *);
void vnet_thread_stop(struct vnet_thread *);
void vnet_thread_continue(struct vnet_thread *);

void vnet_udelay(unsigned long usecs);

// I know there is timer in palacios, but it has to be binded to specific VM, and the granularity is not
// guaranteed
// I need a timer that is global, not related to any specific VM, and also fine-granularity
struct vnet_timer * vnet_create_timer(int interval /*in us*/, void (*timer_fun)(uint64_t eclipsed_cycles, void * priv_data), void * pri_data);
int vnet_del_timer(struct vnet_timer *);
int vnet_start_timer(struct vnet_timer *);
int vnet_stop_timer(struct vnet_timer *);


#endif

 
