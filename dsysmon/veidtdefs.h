/*
 * Copyright (c) 2013 JST DEOS R&D Center
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * vmm/include/kernel/veidtdefs.h:
 *	definition of symbols and data types common to both D-Visor86 VMM
 *	implementation and related Linux Kernel modules.  this file might
 *	be symbolic linked into arch/x86/include/asm/veidt directory of
 *	Linux Kernel source tree for allowing accessed from Linux Kernel
 *	build system.
 */
#ifndef _KERNEL_VEIDTDEFS_H
#define _KERNEL_VEIDTDEFS_H

/* maximum supported number of guest OS. */
#define VEIDT_NUM_MAX_GUEST			2


/*
 * exception vectors used by the guest OS for their IRQs.
 *
 * DV86-NOTE: for the Linux 2.6.32 kernel, hardware IRQs might be assigned
 * from vector number 0x30 through 0x3F, but our DV86 VMM does not supprort
 * those assignment.  therefor Linux kernel implementation must be fixed to
 * assigne hardware IRQs from vector 0x20 through 0x2F.
 */
#ifdef notdef
#define VEIDT_GUEST_IRQ_VECTOR_FIRST		(0x20 + 0x10)
#define VEIDT_GUEST_IRQ_VECTOR_LAST		(0x20 + 0x10 + 15)
#else
#define VEIDT_GUEST_IRQ_VECTOR_FIRST		(0x20)
#define VEIDT_GUEST_IRQ_VECTOR_LAST		(0x20 + 15)
#endif


/*
 * exception vectors used by DV86 VMM.  we reserve 5 vectors at the bottom
 * of x86 vector range, 251-255.
 *
 * VEIDT_VECTOR_VMSIGNAL
 *	inter-processor signal notification within DV86 VMM contexts.  it may
 *	be used to inform some internal event of VMM from one processor to
 *	another.  this type of signal notification is refered as 'vmsignal'
 *	in our implementation.  since 'vmsignal' is not a kind of function
 *	call but just an interrupt on the target processor, any parameter can
 *	not be passed synchronously via. registers.
 *
 * VEIDT_VECTOR_DXFEED
 *	direct cross-feed (DXFEED) is a way to transfer some data chunks from
 *	user-land process running under User Guest OS to the observer entity
 *	of Watcher Guest OS directly.  an interrupt of VEIDT_VECTOR_DXFEED is
 *	used as a special hypervisor call from user-land process to DV86 VMM
 *	bypassing potentially untrusted User Guest OS kernel.  EAX should be
 *	magic number which can be used to identify each other, while EBX and
 *	ECX are used to specify data buffer address and its size.
 *
 * VEIDT_VECTOR_SYSIRET
 *	special hypervisor call dedicated for IRET action.  it would be called
 *	by the guest OS kernel when it attempt to issue an 'iret' instruction
 *	as a return path from kernel mode to user mode.
 *
 * VEIDT_VECTOR_VEIDTCALL
 *	any other hypervisor calls from guest OS kernel to DV86 VMM.  operation
 *	code would be specified by EAX register and remaining arguments would
 *	be specified by EBX, ECX, EDX, and ESI registers.  this type of
 *	hypervisor call is refered as 'veidtcall' in our implementation.
 *
 * DV86-NOTE: Linux x86-32 implementation reserve vector range 234 (0xEA) to
 * 255 (0xFF) for the interrupts under SMP configuration.  DV86 hypercalls
 * should be mapped to not disturb those dedicated vectors.
 */
#define VEIDT_VECTOR_VMSIGNAL			(251)
#define VEIDT_VECTOR_DXFEED			(252)
#define VEIDT_VECTOR_SYSIRET			(253)
#define VEIDT_VECTOR_VEIDTCALL			(254)
#define VEIDT_VECTOR_255			(255)

#define VEIDT_VECTOR_FIRST			(251)
#define VEIDT_VECTOR_LAST			(255)


/*
 * IRQ number used to notify an arraival of VIRTIO device data from back-end
 * part of DV86 VMM to the dedicated device driver of Watcher Guest OS kernel.
 * we selected IRQ6 here to prevent unnecessary IRQ contention with some regacy
 * devices.
 */
#define VEIDT_VIRTIO_NOTIFY_IRQ			6

/*
 * for the DXFEED (direct cross-feed) channel, one more IRQ would be used to
 * notify an arraival of data in the channel from receiver part of DV86 VMM
 * to the dedicated driver in the Watcher Guest OS kernel.
 */
#define VEIDT_DXFEED_NOTIFY_IRQ			7

/*
 * maximum number of bytes can be transfered at a time using DXFEED channel
 * is limited.  data chunks greater than this size of limit will entirely be
 * ignored.  also we must define a magic signature of DXFEED service which
 * must match between caller (user-land process) and callee (DV86 VMM).
 *
 * NOTE: we have decided to use 'sysfs' node as the reader side of DXFEED
 * channel.  due to native limitation of Linux 'sysfs' implementation, 4KB
 * is maximum allowed data transfer size for each read operation.
 */
#define VEIDT_DXFEED_MAXDATASIZE		(4 * 1024)
#define VEIDT_DXFEED_MAGIC			0x3458FEED


#ifndef __ASSEMBLY__


/* error status code used by veidtcalls. */
typedef enum {
	VEIDTCALL_NOERROR		= 0,
	VEIDTCALL_EPRIVILEGE		= 1,
	VEIDTCALL_EINVALID		= 2,
} veidtcall_error_t;

/* data type representing CPU ID number. */
typedef unsigned int veidt_cpuid_t;

/* state of the guest OS. */
typedef enum {
	VEIDT_GUEST_STOPPED,
	VEIDT_GUEST_RUNNING,
} veidt_guest_state_t;

/* virtio message structure. */
struct veidt_virtio_msg {
	unsigned int cmd;		/* command word */
	unsigned int arg;		/* argument word */
};

/* physical memory region information of the guest OS. */
struct veidt_guest_meminfo {
	unsigned long base;		/* physical memory base */
	unsigned long size;		/* memory region size */
};

/* privilege ring change over activities of the guest OS. */
struct veidt_guest_activity {
	unsigned long long upcalls;	/* # of total upcalls taken */
	unsigned long long sysiret;	/* # of total SYSIRET taken */
	unsigned long long kenter;	/* # of kernel enter upcall */
	unsigned long long kexit;	/* # of kernel exit SYSIRET */
};

/*
 * enumeration of type veidtcall_opcode_t describes operation code of each
 * veidtcall functions.  we have 4 categories of veidtcall operations.
 *
 * VEIDTCALL_SYS operations
 *	system controlling veidtcall operations. normally used by the guest OS
 *	kernel to implement para-virtualization behaviour.
 *
 * VEIDTCALL_VIRTIO operations
 *	this category implements a set of veidtcall operations which support
 *	VIRTIO transport layer.
 *
 * VEIDTCALL_UTIL operations
 *	this category implementes various utility operations offen used by
 *	the observer guest OS.
 *
 * VEIDTCALL_DEBUG operations
 *	few debugging support veitcall operations are collected in this
 *	category.
 */
typedef enum {
	VEIDTCALL_SYS_START = 0,
	VEIDTCALL_SYS_SET_EFLAGS,
	VEIDTCALL_SYS_UPDATE_IDT_ENTRY,
	VEIDTCALL_SYS_UPDATE_GDT_ENTRY,
	VEIDTCALL_SYS_GADDR2P,

	VEIDTCALL_VIRTIO_START = 50,
	VEIDTCALL_VIRTIO_USER_WRITE,
	VEIDTCALL_VIRTIO_WATCHER_READ,
	VEIDTCALL_VIRTIO_WATCHER_POLL,
	VEIDTCALL_VIRTIO_WATCHER_WRITE,

	VEIDTCALL_UTIL_START = 100,
	VEIDTCALL_UTIL_QUERY_CPUID,
	VEIDTCALL_UTIL_GET_NUM_OF_GUESTS,
	VEIDTCALL_UTIL_GET_GUEST_STATE,
	VEIDTCALL_UTIL_GET_GUEST_BOOT_COUNT,
	VEIDTCALL_UTIL_GET_GUEST_MEMINFO,
	VEIDTCALL_UTIL_GET_GUEST_ACTIVITY,
	VEIDTCALL_UTIL_RESTART_GUEST,
	VEIDTCALL_UTIL_READ_GUEST_KMEM,
	VEIDTCALL_UTIL_PRINT_PROFILE,
	VEIDTCALL_UTIL_PRINT_PAGING,
	VEIDTCALL_UTIL_READ_DXFEED,

	VEIDTCALL_DEBUG_START = 200,
	VEIDTCALL_DEBUG_PRINT_INT,
	VEIDTCALL_DEBUG_PRINT_STR,
	VEIDTCALL_DEBUG_SET_LOGLEVEL,
	VEIDTCALL_DEBUG_HELLO_GUEST,

} veidtcall_opcode_t;


#endif /* __ASSEMBLY__ */

#endif /* _KERNEL_VEIDTDEFS_H */
