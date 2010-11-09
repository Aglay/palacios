/* 
 * This file is part of the Palacios Virtual Machine Monitor developed
 * by the V3VEE Project with funding from the United States National 
 * Science Foundation and the Department of Energy.  
 *
 * The V3VEE Project is a joint project between Northwestern University
 * and the University of New Mexico.  You can find out more at 
 * http://www.v3vee.org
 *
 * Copyright (c) 2008, Jack Lange <jarusl@cs.northwestern.edu> 
 * Copyright (c) 2008, The V3VEE Project <http://www.v3vee.org> 
 * All rights reserved.
 *
 * Author: Jack Lange <jarusl@cs.northwestern.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "V3VEE_LICENSE".
 */

#include <palacios/vmm.h>
#include <palacios/vmm_dev_mgr.h>

#include <palacios/vmm_file.h>
#include <palacios/vm_guest.h>

#ifndef CONFIG_DEBUG_FILEDISK
#undef PrintDebug
#define PrintDebug(fmt, args...)
#endif

struct disk_state {
    uint64_t capacity; // in bytes

    void * fd;
};



static int write_all(void * fd, char * buf, int offset, int length) {
    int bytes_written = 0;
    
    PrintDebug("Writing %d bytes\n", length - bytes_written);
    while (bytes_written < length) {
	int tmp_bytes = V3_FileWrite(fd, offset+bytes_written, buf + bytes_written, length - bytes_written);
	PrintDebug("Wrote %d bytes\n", tmp_bytes);
	
	if (tmp_bytes <= 0 ) {
	    PrintError("Write failed\n");
	    return -1;
	}
	
	bytes_written += tmp_bytes;
    }
    
    return 0;
}


static int read_all(void * fd, char * buf, int offset, int length) {
    int bytes_read = 0;
    
    PrintDebug("Reading %d bytes\n", length - bytes_read);
    while (bytes_read < length) {
	int tmp_bytes = V3_FileRead(fd, offset+bytes_read, buf + bytes_read, length - bytes_read);
	PrintDebug("Read %d bytes\n", tmp_bytes);
	
	if (tmp_bytes <= 0) {
	    PrintError("Read failed\n");
	    return -1;
	}
	
	bytes_read += tmp_bytes;
    }
    
    return 0;
}

static int read(uint8_t * buf, uint64_t lba, uint64_t num_bytes, void * private_data) {
    struct disk_state * disk = (struct disk_state *)private_data;

    PrintDebug("Reading %d bytes from %p to %p\n", (uint32_t)num_bytes, (uint8_t *)(disk->disk_image + lba), buf);

    return read_all(disk->fd, buf, lba, num_bytes);
}


static int write(uint8_t * buf, uint64_t lba, uint64_t num_bytes, void * private_data) {
    struct disk_state * disk = (struct disk_state *)private_data;

    PrintDebug("Writing %d bytes from %p to %p\n", (uint32_t)num_bytes,  buf, (uint8_t *)(disk->disk_image + lba));

    return write_all(disk->fd,  buf, lba, num_bytes);
}


static uint64_t get_capacity(void * private_data) {
    struct disk_state * disk = (struct disk_state *)private_data;

    PrintDebug("Querying RAMDISK capacity %d\n", 
	       (uint32_t)(disk->capacity));

    return disk->capacity;
}

static struct v3_dev_blk_ops blk_ops = {
    .read = read, 
    .write = write,
    .get_capacity = get_capacity,
};




static int disk_free(struct vm_device * dev) {
    return 0;
}

static struct v3_device_ops dev_ops = {
    .free = disk_free,
    .reset = NULL,
    .start = NULL,
    .stop = NULL,
};




static int disk_init(struct v3_vm_info * vm, v3_cfg_tree_t * cfg) {
    struct disk_state * disk = NULL;
    char * path = v3_cfg_val(cfg, "path");
    char * dev_id = v3_cfg_val(cfg, "ID");

    char * writable = v3_cfg_val(cfg, "writable");
    char * readable = v3_cfg_val(cfg, "readable");
    
    int allowWrite = ( writable && writable[0] == '1' );
    int allowRead = ( !readable || readable[0] == '1' );

    v3_cfg_tree_t * frontend_cfg = v3_cfg_subtree(cfg, "frontend");

    disk = (struct disk_state *)V3_Malloc(sizeof(struct disk_state));
    memset(disk, 0, sizeof(struct disk_state));


    if (path == NULL) {
	PrintError("Missing path (%s) for %s\n", path, dev_id);
	return -1;

    }
    
    if ( (allowRead == 1) && (allowWrite == 1) ) {
    	disk->fd = V3_FileOpen(path, FILE_OPEN_MODE_READ | FILE_OPEN_MODE_WRITE, vm->host_priv_data );
    } else if ( (allowRead == 1) && (allowWrite == 0) ) {
    	disk->fd = V3_FileOpen(path, FILE_OPEN_MODE_READ, vm->host_priv_data );
    } else if ( (allowRead == 0) && (allowWrite == 1) ) {
    	disk->fd = V3_FileOpen(path, FILE_OPEN_MODE_WRITE, vm->host_priv_data );
    } else {
    	PrintError("Error on %s: No file mode specified\n", dev_id );
    	return -1;

    }
    
    disk->capacity = V3_FileSize(disk->fd);

    PrintDebug("Registering FILEDISK %s (path=%s, fd=%lu, size=%lu)\n",
	       dev_id, path, file->fd, file->capacity);

    struct vm_device * dev = v3_allocate_device(dev_id, &dev_ops, disk);

    if (v3_attach_device(vm, dev) == -1) {

	PrintError("Could not attach device %s\n", dev_id);
	return -1;

    }

    if (v3_dev_connect_blk(vm, v3_cfg_val(frontend_cfg, "tag"), 
			   &blk_ops, frontend_cfg, disk) == -1) {

	PrintError("Could not connect %s to frontend %s\n", 
		   dev_id, v3_cfg_val(frontend_cfg, "tag"));
	return -1;

    }
    

    return 0;
}


device_register("FILEDISK", disk_init)
