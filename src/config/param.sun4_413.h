/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef	AFS_PARAM_H
#define	AFS_PARAM_H

#define AFS_VFSINCL_ENV	1	/* NOBODY uses this.... */
#define AFS_ENV			1
#define AFS_SUN_ENV		1
#define AFS_SUN4_ENV		1

#include <afs/afs_sysnames.h>

#define AFS_GCPAGS		1       /* if nonzero, garbage collect PAGs */
#define AFS_GLOBAL_SUNLOCK	1	/* For global locking */

#define	AFS_3DISPARES		1	/* Utilize the 3 available disk inode 'spares' */
#define	AFS_SYSCALL		31

/* File system entry (used if mount.h doesn't define MOUNT_AFS */
#define AFS_MOUNT_AFS	 "afs"

/* Machine / Operating system information */
#define sys_sun4_413	1
#define SYS_NAME	"sun4_413"
#define SYS_NAME_ID	SYS_NAME_ID_sun4_411
#define AFSBIG_ENDIAN	1
#define AFS_HAVE_FFS    1       /* Use system's ffs. */
#define AFS_HAVE_STATVFS 0
#define AFS_VM_RDWR_ENV	1	/* read/write implemented via VM */

#define KERNEL_HAVE_SETUERROR 1

/* Extra kernel definitions (from kdefs file) */
#ifdef KERNEL
/* sun definitions here */
#define	AFS_UIOFMODE		1	/* Only in afs/afs_vnodeops.c (afs_ustrategy) */
#define	afsio_iov		uio_iov
#define	afsio_iovcnt	uio_iovcnt
#define	afsio_offset	uio_offset
#define	afsio_seg		uio_segflg
#define	afsio_fmode	uio_fmode
#define	afsio_resid	uio_resid
#define	AFS_UIOSYS	UIO_SYSSPACE
#define	AFS_UIOUSER	UIO_USERSPACE
#define	AFS_CLBYTES	MCLBYTES
#define	AFS_MINCHANGE	2
#define	osi_GetTime(x)	uniqtime(x)
#define	AFS_KALLOC(n)	kmem_alloc(n, KMEM_SLEEP)
#define	AFS_KALLOC_NOSLEEP(n)	kmem_alloc(n, KMEM_NOSLEEP)
#define	AFS_KFREE	kmem_free
#define	VATTR_NULL	vattr_null
#endif /* KERNEL */
#define memset(A, B, S) bzero(A, S)
#define memcpy(B, A, S) bcopy(A, B, S) 
#define memcmp(A, B, S) bcmp(A, B, S)
#define memmove(B, A, S) bcopy(A, B, S)
#define	AFS_DIRENT	
#ifndef CMSERVERPREF
#define CMSERVERPREF
#endif

#endif /* AFS_PARAM_H */
