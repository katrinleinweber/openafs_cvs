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

#define AFS_VFS_ENV	1
/* Used only in vfsck code; is it needed any more???? */
#define RXK_LISTENER_ENV	1
#define AFS_USERSPACE_IP_ADDR	1
#define AFS_GCPAGS		0       /* if nonzero, garbage collect PAGs */

#ifdef KERNEL

#define UKERNEL			1	/* user space kernel */
#define AFS_ENV			1
#define AFS_USR_AIX_ENV		1
#define AFS_USR_AIX41_ENV	1
#define AFS_USR_AIX42_ENV	1

#else  /* KERNEL */

#define	AFS_AIX_ENV		1
#define	AFS_AIX32_ENV		1
#define	AFS_AIX41_ENV		1
#define AFS_AIX42_ENV		1

#endif /* KERNEL */

#include <afs/afs_sysnames.h>

/*#define AFS_GLOBAL_SUNLOCK	1*/	/* For global locking */

#define	AFS_3DISPARES		1	/* Utilize the 3 available disk inode 'spares' */
#define	AFS_SYSCALL		105

/* File system entry (used if mount.h doesn't define MOUNT_AFS */
#define AFS_MOUNT_AFS	 4

/* Machine / Operating system information */
#define sys_rs_aix42	1
#define SYS_NAME	"rs_aix42"
#define SYS_NAME_ID	SYS_NAME_ID_rs_aix42
#define AFSBIG_ENDIAN	1
#define AFS_HAVE_FFS            1       /* Use system's ffs. */
#define AFS_HAVE_STATVFS	0	/* System doesn't support statvfs */

/* Extra kernel definitions (from kdefs file) */
#ifdef KERNEL
#define	AFS_UIOFMODE		1	/* Only in afs/afs_vnodeops.c (afs_ustrategy) */
#define	AFS_SYSVLOCK		1	/* sys v locking supported */
/*#define	AFS_USEBUFFERS	1*/
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
#endif /* KERNEL */
#define	AFS_DIRENT	
#ifndef CMSERVERPREF
#define CMSERVERPREF
#endif

#endif /* AFS_PARAM_H */
