#ifndef UKERNEL
/* This section for kernel libafs compiles only */

#ifndef	AFS_PARAM_H
#define	AFS_PARAM_H

#define AFS_VFS_ENV		1
/* Used only in vfsck code; is it needed any more???? */

#define AFS_VFSINCL_ENV		1	/* NOBODY uses this.... */
#define AFS_GREEDY43_ENV	1	/* Used only in rx/rx_user.c */
#define AFS_ENV			1
#define AFS_SUN_ENV		1
#define AFS_SUN5_ENV		1
#define	AFS_SUN52_ENV		1
#define	AFS_SUN53_ENV		1
#define	AFS_SUN54_ENV		1
#define	AFS_SUN55_ENV		1
#define	AFS_SUN56_ENV		1
#define AFS_SUN57_ENV		1
#define AFS_SUN58_ENV		1
#define AFS_SUN59_ENV		1
#define AFS_SUN510_ENV		1

#define AFS_64BIT_ENV		1	/* Defines afs_int32 as int, not long. */
/*
 * Only define on 64 bit kernel, until problems with 32 bit
 * and large file support are resolved
 */
#if defined(__sparcv9)
#define AFS_64BIT_CLIENT	1	
#endif

#define AFS_HAVE_FLOCK_SYSID	1

#ifdef AFS_NAMEI_ENV
#define AFS_64BIT_IOPS_ENV	1	/* needed for NAMEI... */
#else /* AFS_NAMEI_ENV */
#define AFS_3DISPARES		1	/* Utilize the 3 available disk inode 'spares' */
#endif /* AFS_NAMEI_ENV */

#include <afs/afs_sysnames.h>

#define AFS_GLOBAL_SUNLOCK	1	/* For global locking */
#define RXK_LISTENER_ENV	1
#define AFS_GCPAGS		1	/* if nonzero, garbage collect PAGs */

#define	AFS_SYSCALL		65

/* File system entry (used if mount.h doesn't define MOUNT_AFS */
#define AFS_MOUNT_AFS	 "afs"

/* Machine / Operating system information */
#define sys_sun4x_510		1
#define SYS_NAME		"sun4x_510"
#define SYS_NAME_ID		SYS_NAME_ID_sun4x_510
#define AFSBIG_ENDIAN		1
#define AFS_HAVE_FFS		1	/* Use system's ffs. */
#define AFS_HAVE_VXFS		1	/* Support cache on Veritas vxfs file system */
#define AFS_HAVE_STATVFS	1	/* System supports statvfs */
#define AFS_VM_RDWR_ENV		1	/* read/write implemented via VM */
#define AFS_USE_GETTIMEOFDAY	1	/* use gettimeofday to implement rx clock */

#define NEARINODE_HINT 		1	/* hint to ufs module to scatter inodes on disk */
#define nearInodeHash(volid, hval) {                                 \
                unsigned char*  ts = (unsigned char*)&(volid)+sizeof(volid)-1;\
                for ( (hval)=0; ts >= (unsigned char*)&(volid); ts--){\
                    (hval) *= 173;                      \
                    (hval) += *ts;                      \
                }                                       \
                }

/* Extra kernel definitions (from kdefs file) */
#ifdef KERNEL
/* sun definitions here */
#define	AFS_UIOFMODE		1	/* Only in afs/afs_vnodeops.c (afs_ustrategy) */
#define	AFS_SYSVLOCK		1	/* sys v locking supported */
/*#define	AFS_USEBUFFERS	1*/
#define	afsio_iov		uio_iov
#define	afsio_iovcnt		uio_iovcnt
#define	afsio_offset		uio_offset
#define	afsio_seg		uio_segflg
#define	afsio_fmode		uio_fmode
#define	afsio_resid		uio_resid
#define	AFS_UIOSYS		UIO_SYSSPACE
#define	AFS_UIOUSER		UIO_USERSPACE
#define	AFS_CLBYTES		MCLBYTES
#define	AFS_MINCHANGE		2
#define	osi_GetTime(x)		uniqtime(x)

/**
  * These defines are for the 64 bit Solaris 7 port
  * AFS_SYSCALL32 is used to protect the ILP32 syscall interface
  * AFS_64BIT_ENV is for use of 64 bit inode numbers
  */
#if defined(__sparcv9)
#define	AFS_SUN57_64BIT_ENV	1
#define AFS_64BIT_INO   	1
#endif

/**
  * Solaris 7 64 bit has two versions of uniqtime. Since we consistently
  * use 32 bit quantities for time in afs, we now use uniqtime32
  */
#if defined(AFS_SUN57_64BIT_ENV)
#undef osi_GetTime
#define osi_GetTime(x)		uniqtime32(x)
#endif



#define	AFS_KALLOC(n)		kmem_alloc(n, KM_SLEEP)
#define AFS_KALLOC_NOSLEEP(n)   kmem_alloc(n, KM_NOSLEEP)
#define	AFS_KFREE		kmem_free
#define	VATTR_NULL		vattr_null
#define memset(A, B, S)		bzero(A, S)
#define memcpy(B, A, S)		bcopy(A, B, S)
#define memcmp(A, B, S)		bcmp(A, B, S)
#endif /* KERNEL */
#define	AFS_DIRENT
#ifndef CMSERVERPREF
#define CMSERVERPREF
#endif
#define	ROOTINO			UFSROOTINO

#endif /* AFS_PARAM_H */

#else /* !defined(UKERNEL) */

/* This section for user space compiles only */

#ifndef	AFS_PARAM_H
#define	AFS_PARAM_H

#define AFS_VFS_ENV		1
/* Used only in vfsck code; is it needed any more???? */
#define RXK_LISTENER_ENV	1
#define AFS_USERSPACE_IP_ADDR	1
#define AFS_GCPAGS		0	/* if nonzero, garbage collect PAGs */

#define UKERNEL			1	/* user space kernel */
#define AFS_GREEDY43_ENV	1	/* Used only in rx/rx_user.c */
#define AFS_ENV			1
#define AFS_USR_SUN5_ENV	1
#define AFS_USR_SUN6_ENV	1
#define AFS_USR_SUN7_ENV	1
#define AFS_USR_SUN8_ENV	1
#define AFS_USR_SUN9_ENV	1

#define AFS_64BIT_ENV		1

#include <afs/afs_sysnames.h>

#if 0
#define AFS_GLOBAL_SUNLOCK	1	/* For global locking */
#endif

#define	AFS_3DISPARES		1	/* Utilize the 3 available disk inode 'spares' */
#define	AFS_SYSCALL		65

/* File system entry (used if mount.h doesn't define MOUNT_AFS */
#define AFS_MOUNT_AFS		1

/* Machine / Operating system information */
#define sys_sun4x_59		1
#define SYS_NAME		"sun4x_59"
#define SYS_NAME_ID		SYS_NAME_ID_sun4x_59
#define AFSBIG_ENDIAN		1
#define AFS_HAVE_FFS            1	/* Use system's ffs. */
#define AFS_HAVE_STATVFS	0	/* System doesn't support statvfs */

/* Extra kernel definitions (from kdefs file) */
#ifdef KERNEL
#define	AFS_UIOFMODE		1	/* Only in afs/afs_vnodeops.c (afs_ustrategy) */
#define	AFS_SYSVLOCK		1	/* sys v locking supported */
/*#define	AFS_USEBUFFERS	1*/
#define	afsio_iov		uio_iov
#define	afsio_iovcnt		uio_iovcnt
#define	afsio_offset		uio_offset
#define	afsio_seg		uio_segflg
#define	afsio_fmode		uio_fmode
#define	afsio_resid		uio_resid
#define	AFS_UIOSYS		1
#define	AFS_UIOUSER		UIO_USERSPACE
#define	AFS_CLBYTES		MCLBYTES
#define	AFS_MINCHANGE		2
#define	VATTR_NULL		usr_vattr_null
#endif /* KERNEL */
#define	AFS_DIRENT
#ifndef CMSERVERPREF
#define CMSERVERPREF
#endif
#define	ROOTINO			UFSROOTINO

#endif /* AFS_PARAM_H */

#endif /* !defined(UKERNEL) */
