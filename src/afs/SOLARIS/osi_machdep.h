/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * Solaris OSI header file. Extends afs_osi.h.
 *
 * afs_osi.h includes this file, which is the only way this file should
 * be included in a source file. This file can redefine macros declared in
 * afs_osi.h.
 */

#ifndef _OSI_MACHDEP_H_
#define _OSI_MACHDEP_H_

#ifdef AFS_SUN57_64BIT_ENV
#include <sys/model.h>	/* for get_udatamodel() */
#endif

#define getpid()		curproc->p_pid

/**
  * The location of the NFSSRV module
  * Used in osi_vfsops.c when checking to see if the nfssrv module is
  * loaded
  */
#define NFSSRV 		"/kernel/misc/nfssrv"
#define NFSSRV_V9 	"/kernel/misc/sparcv9/nfssrv"

#define AFS_UCRED cred
#define AFS_PROC struct proc

/* 
 * Time related macros
 */
#define	afs_hz	    hz
#define osi_Time() (hrestime.tv_sec)

#undef afs_osi_Alloc_NoSleep
extern void *afs_osi_Alloc_NoSleep(size_t size);

#define osi_vnhold(avc, r)  do { VN_HOLD((struct vnode *)(avc)); } while(0)
#define gop_rdwr(rw,gp,base,len,offset,segflg,ioflag,ulimit,cr,aresid) \
  vn_rdwr((rw),(gp),(base),(len),(offset),(segflg),(ioflag),(ulimit),(cr),(aresid))

#define	afs_suser	    suser


#ifdef KERNEL
/*
 * Global lock support. 
 */
#include <sys/mutex.h>
extern kmutex_t afs_global_lock;
extern kmutex_t afs_rxglobal_lock;

#define AFS_GLOCK()	mutex_enter(&afs_global_lock);		
#define AFS_GUNLOCK()	mutex_exit(&afs_global_lock);
#define ISAFS_GLOCK()	mutex_owned(&afs_global_lock)

#define AFS_RXGLOCK()
#define AFS_RXGUNLOCK()
#define ISAFS_RXGLOCK() 1
#endif


/* Associate the Berkley signal equivalent lock types to System V's */
#define	LOCK_SH 1	/* F_RDLCK */
#define	LOCK_EX	2	/* F_WRLCK */
#define	LOCK_NB	4	/* XXX */
#define	LOCK_UN	8	/* F_UNLCK */

#ifndef	IO_APPEND
#define	IO_APPEND 	FAPPEND
#endif

#ifndef	IO_SYNC
#define	IO_SYNC		FSYNC
#endif

#if	defined(AFS_SUN56_ENV)
/*
** Macro returns 1 if file is larger than 2GB; else returns 0 
*/
#undef AfsLargeFileUio
#define AfsLargeFileUio(uio)       ( (uio)->_uio_offset._p._u ? 1 : 0 )
#undef AfsLargeFileSize
#define AfsLargeFileSize(pos, off) ( ((offset_t)(pos)+(offset_t)(off) > (offset_t)MAXOFF_T)?1:0)
#endif

#endif /* _OSI_MACHDEP_H_ */

