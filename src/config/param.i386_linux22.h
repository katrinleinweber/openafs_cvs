#ifndef UKERNEL
/* This section for kernel libafs compiles only */

/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef AFS_PARAM_H
#define AFS_PARAM_H

/* In user space the AFS_LINUX20_ENV should be sufficient. In the kernel,
 * it's a judgment call. If something is obviously i386 specific, use that
 * #define instead. Note that "20" refers to the linux 2.0 kernel. The "2"
 * in the sysname is the current version of the client. This takes into
 * account the perferred OS user space configuration as well as the kernel.
 */

#define AFS_LINUX20_ENV	1
#define AFS_LINUX22_ENV	1
#define AFS_I386_LINUX20_ENV	1
#define AFS_I386_LINUX22_ENV	1
#define AFS_NONFSTRANS 1

#define AFS_MOUNT_AFS "afs"	/* The name of the filesystem type. */
#define AFS_SYSCALL 137
#define AFS_64BIT_IOPS_ENV  1
#define AFS_NAMEI_ENV     1   /* User space interface to file system */

#if defined(__KERNEL__) && !defined(KDUMP_KERNEL)
#include <linux/config.h>
#ifdef CONFIG_SMP
#undef CONFIG_SMP
#endif
/* Using "AFS_SMP" to map to however many #define's are required to get
 * MP to compile for Linux
 */
#ifdef AFS_SMP
#define CONFIG_SMP 1
#ifndef CONFIG_X86_LOCAL_APIC
#define CONFIG_X86_LOCAL_APIC
#endif
#ifndef __SMP__
#define __SMP__
#endif
#define AFS_GLOBAL_SUNLOCK
#endif

#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#define MODVERSIONS
#include <linux/modversions.h>
#endif

#endif /* __KERNEL__  && !DUMP_KERNEL*/

#include <afs/afs_sysnames.h>

#define AFS_USERSPACE_IP_ADDR 1
#define RXK_LISTENER_ENV 1
#define AFS_GCPAGS       2 /* Set to Userdisabled, allow sysctl to override */


/* Machine / Operating system information */
#define SYS_NAME	"i386_linux22"
#define SYS_NAME_ID	SYS_NAME_ID_i386_linux22
#define AFSLITTLE_ENDIAN    1
#define AFS_HAVE_FFS        1       /* Use system's ffs. */
#define AFS_HAVE_STATVFS    0	/* System doesn't support statvfs */
#define AFS_VM_RDWR_ENV	    1	/* read/write implemented via VM */

#ifdef KERNEL
#ifndef MIN
#define MIN(A,B) ((A) < (B) ? (A) : (B))
#endif
#ifndef MAX
#define MAX(A,B) ((A) > (B) ? (A) : (B))
#endif
#endif /* KERNEL */

#endif /* AFS_PARAM_H */

#else /* !defined(UKERNEL) */

/* This section for user space compiles only */

/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef AFS_PARAM_H
#define AFS_PARAM_H

/* In user space the AFS_LINUX20_ENV should be sufficient. In the kernel,
 * it's a judgment call. If something is obviously i386 specific, use that
 * #define instead. Note that "20" refers to the linux 2.0 kernel. The "2"
 * in the sysname is the current version of the client. This takes into
 * account the perferred OS user space configuration as well as the kernel.
 */

#define UKERNEL			1	/* user space kernel */
#define AFS_ENV			1
#define AFS_USR_LINUX20_ENV	1
#define AFS_USR_LINUX22_ENV	1
#define AFS_NONFSTRANS 1

#define AFS_MOUNT_AFS "afs"	/* The name of the filesystem type. */
#define AFS_SYSCALL 137
#define AFS_64BIT_IOPS_ENV  1
#define AFS_NAMEI_ENV     1   /* User space interface to file system */
#include <afs/afs_sysnames.h>

#define AFS_USERSPACE_IP_ADDR 1
#define RXK_LISTENER_ENV 1
#define AFS_GCPAGS		0       /* if nonzero, garbage collect PAGs */


/* Machine / Operating system information */
#define SYS_NAME	"i386_linux22"
#define SYS_NAME_ID	SYS_NAME_ID_i386_linux22
#define AFSLITTLE_ENDIAN    1
#define AFS_HAVE_FFS        1       /* Use system's ffs. */
#define AFS_HAVE_STATVFS    0	/* System doesn't support statvfs */
#define AFS_VM_RDWR_ENV	    1	/* read/write implemented via VM */

#define	afsio_iov	uio_iov
#define	afsio_iovcnt	uio_iovcnt
#define	afsio_offset	uio_offset
#define	afsio_seg	uio_segflg
#define	afsio_fmode	uio_fmode
#define	afsio_resid	uio_resid
#define	AFS_UIOSYS	1
#define	AFS_UIOUSER	UIO_USERSPACE
#define	AFS_CLBYTES	MCLBYTES
#define	AFS_MINCHANGE	2
#define	VATTR_NULL	usr_vattr_null

#define AFS_DIRENT
#ifndef CMSERVERPREF
#define CMSERVERPREF
#endif

#endif /* AFS_PARAM_H */

#endif /* !defined(UKERNEL) */
