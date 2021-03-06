/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#include "afs/param.h"

RCSID
    ("$Header: /cvs/openafs/src/afs/LINUX/osi_file.c,v 1.40 2009/01/15 13:26:57 shadow Exp $");

#ifdef AFS_LINUX24_ENV
#include "h/module.h" /* early to avoid printf->printk mapping */
#endif
#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"	/* afs statistics */
#include "h/smp_lock.h"
#if defined(AFS_LINUX26_ENV)
#include "h/namei.h"
#endif
#if !defined(HAVE_IGET)
#include "h/exportfs.h"
#endif

afs_lock_t afs_xosi;		/* lock is for tvattr */
extern struct osi_dev cacheDev;
#if defined(AFS_LINUX24_ENV)
extern struct vfsmount *afs_cacheMnt;
#endif
extern struct super_block *afs_cacheSBp;

#if defined(AFS_LINUX26_ENV) 
void *
#if defined(LINUX_USE_FH)
osi_UFSOpen_fh(struct fid *fh, int fh_type)
#else
osi_UFSOpen(afs_int32 ainode)
#endif
{
    register struct osi_file *afile = NULL;
    extern int cacheDiskType;
    struct inode *tip = NULL;
    struct dentry *dp = NULL;
    struct file *filp = NULL;
#if !defined(HAVE_IGET) || defined(LINUX_USE_FH)
    struct fid fid;
#endif
    AFS_STATCNT(osi_UFSOpen);
    if (cacheDiskType != AFS_FCACHE_TYPE_UFS) {
	osi_Panic("UFSOpen called for non-UFS cache\n");
    }
    if (!afs_osicred_initialized) {
	/* valid for alpha_osf, SunOS, Ultrix */
	memset((char *)&afs_osi_cred, 0, sizeof(struct AFS_UCRED));
	crhold(&afs_osi_cred);	/* don't let it evaporate, since it is static */
	afs_osicred_initialized = 1;
    }
    afile = (struct osi_file *)osi_AllocLargeSpace(sizeof(struct osi_file));
    AFS_GUNLOCK();
    if (!afile) {
	osi_Panic("osi_UFSOpen: Failed to allocate %d bytes for osi_file.\n",
		  sizeof(struct osi_file));
    }
    memset(afile, 0, sizeof(struct osi_file));
#if defined(HAVE_IGET)
    tip = iget(afs_cacheSBp, (u_long) ainode);
    if (!tip)
	osi_Panic("Can't get inode %d\n", ainode);

    dp = d_alloc_anon(tip);
#else
#if defined(LINUX_USE_FH)
    dp = afs_cacheSBp->s_export_op->fh_to_dentry(afs_cacheSBp, fh, sizeof(struct fid), fh_type);
#else
    fid.i32.ino = ainode;
    fid.i32.gen = 0;
    dp = afs_cacheSBp->s_export_op->fh_to_dentry(afs_cacheSBp, &fid, sizeof(fid), FILEID_INO32_GEN);
#endif
    if (!dp) 
           osi_Panic("Can't get dentry\n");          
    tip = dp->d_inode;
#endif
    tip->i_flags |= MS_NOATIME;	/* Disable updating access times. */

#if defined(STRUCT_TASK_HAS_CRED)
    filp = dentry_open(dp, mntget(afs_cacheMnt), O_RDWR, current_cred());
#else
    filp = dentry_open(dp, mntget(afs_cacheMnt), O_RDWR);
#endif
    if (IS_ERR(filp))
#if defined(LINUX_USE_FH)
	osi_Panic("Can't open file\n");
#else
	osi_Panic("Can't open inode %d\n", ainode);
#endif
    afile->filp = filp;
    afile->size = i_size_read(FILE_INODE(filp));
    AFS_GLOCK();
    afile->offset = 0;
    afile->proc = (int (*)())0;
#if defined(LINUX_USE_FH)
    afile->inum = tip->i_ino;	/* for hint validity checking */
#else
    afile->inum = ainode;	/* for hint validity checking */
#endif
    return (void *)afile;
}
#else
void *
osi_UFSOpen(afs_int32 ainode)
{
    register struct osi_file *afile = NULL;
    extern int cacheDiskType;
    afs_int32 code = 0;
    struct inode *tip = NULL;
    struct file *filp = NULL;
    AFS_STATCNT(osi_UFSOpen);
    if (cacheDiskType != AFS_FCACHE_TYPE_UFS) {
	osi_Panic("UFSOpen called for non-UFS cache\n");
    }
    if (!afs_osicred_initialized) {
	/* valid for alpha_osf, SunOS, Ultrix */
	memset((char *)&afs_osi_cred, 0, sizeof(struct AFS_UCRED));
	crhold(&afs_osi_cred);	/* don't let it evaporate, since it is static */
	afs_osicred_initialized = 1;
    }
    afile = (struct osi_file *)osi_AllocLargeSpace(sizeof(struct osi_file));
    AFS_GUNLOCK();
    if (!afile) {
	osi_Panic("osi_UFSOpen: Failed to allocate %d bytes for osi_file.\n",
		  sizeof(struct osi_file));
    }
    memset(afile, 0, sizeof(struct osi_file));
    filp = &afile->file;
    filp->f_dentry = &afile->dentry;
    tip = iget(afs_cacheSBp, (u_long) ainode);
    if (!tip)
	osi_Panic("Can't get inode %d\n", ainode);
    FILE_INODE(filp) = tip;
    tip->i_flags |= MS_NOATIME;	/* Disable updating access times. */
    filp->f_flags = O_RDWR;
#if defined(AFS_LINUX24_ENV)
    filp->f_mode = FMODE_READ|FMODE_WRITE;
    filp->f_op = fops_get(tip->i_fop);
#else
    filp->f_op = tip->i_op->default_file_ops;
#endif
    if (filp->f_op && filp->f_op->open)
	code = filp->f_op->open(tip, filp);
    if (code)
	osi_Panic("Can't open inode %d\n", ainode);
    afile->size = i_size_read(tip);
    AFS_GLOCK();
    afile->offset = 0;
    afile->proc = (int (*)())0;
    afile->inum = ainode;	/* for hint validity checking */
    return (void *)afile;
}
#endif

#if defined(LINUX_USE_FH)
int
osi_get_fh(struct dentry *dp, struct fid *fh, int *max_len) {
    int fh_type;

    if (dp->d_sb->s_export_op->encode_fh) {
           fh_type = dp->d_sb->s_export_op->encode_fh(dp, (__u32 *)fh, max_len, 0);
    } else {
           fh_type = FILEID_INO32_GEN;
           fh->i32.ino = dp->d_inode->i_ino;
           fh->i32.gen = dp->d_inode->i_generation;
    }
    dput(dp);
    return(fh_type);
}
#endif

int
afs_osi_Stat(register struct osi_file *afile, register struct osi_stat *astat)
{
    register afs_int32 code;
    AFS_STATCNT(osi_Stat);
    MObtainWriteLock(&afs_xosi, 320);
    astat->size = i_size_read(OSIFILE_INODE(afile));
#if defined(AFS_LINUX26_ENV)
    astat->mtime = OSIFILE_INODE(afile)->i_mtime.tv_sec;
    astat->atime = OSIFILE_INODE(afile)->i_atime.tv_sec;
#else
    astat->mtime = OSIFILE_INODE(afile)->i_mtime;
    astat->atime = OSIFILE_INODE(afile)->i_atime;
#endif
    code = 0;
    MReleaseWriteLock(&afs_xosi);
    return code;
}

#ifdef AFS_LINUX26_ENV
int
osi_UFSClose(register struct osi_file *afile)
{
    AFS_STATCNT(osi_Close);
    if (afile) {
	if (OSIFILE_INODE(afile)) {
	    filp_close(afile->filp, NULL);
	}
    }

    osi_FreeLargeSpace(afile);
    return 0;
}
#else
int
osi_UFSClose(register struct osi_file *afile)
{
    AFS_STATCNT(osi_Close);
    if (afile) {
	if (FILE_INODE(&afile->file)) {
	    struct file *filp = &afile->file;
	    if (filp->f_op && filp->f_op->release)
		filp->f_op->release(FILE_INODE(filp), filp);
	    iput(FILE_INODE(filp));
	}
    }

    osi_FreeLargeSpace(afile);
    return 0;
}
#endif

int
osi_UFSTruncate(register struct osi_file *afile, afs_int32 asize)
{
    register afs_int32 code;
    struct osi_stat tstat;
    struct iattr newattrs;
    struct inode *inode = OSIFILE_INODE(afile);
    AFS_STATCNT(osi_Truncate);

    /* This routine only shrinks files, and most systems
     * have very slow truncates, even when the file is already
     * small enough.  Check now and save some time.
     */
    code = afs_osi_Stat(afile, &tstat);
    if (code || tstat.size <= asize)
	return code;
    MObtainWriteLock(&afs_xosi, 321);
    AFS_GUNLOCK();
#ifdef STRUCT_INODE_HAS_I_ALLOC_SEM
    down_write(&inode->i_alloc_sem);
#endif
#ifdef STRUCT_INODE_HAS_I_MUTEX
    mutex_lock(&inode->i_mutex);
#else
    down(&inode->i_sem);
#endif
    newattrs.ia_size = asize;
    newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
#if defined(AFS_LINUX24_ENV)
    newattrs.ia_ctime = CURRENT_TIME;

    /* avoid notify_change() since it wants to update dentry->d_parent */
    lock_kernel();
    code = inode_change_ok(inode, &newattrs);
    if (!code)
#ifdef INODE_SETATTR_NOT_VOID
	code = inode_setattr(inode, &newattrs);
#else
	inode_setattr(inode, &newattrs);
#endif
    unlock_kernel();
    if (!code)
	truncate_inode_pages(&inode->i_data, asize);
#else
    i_size_write(inode, asize);
    if (inode->i_sb->s_op && inode->i_sb->s_op->notify_change) {
	code = inode->i_sb->s_op->notify_change(&afile->dentry, &newattrs);
    }
    if (!code) {
	truncate_inode_pages(inode, asize);
	if (inode->i_op && inode->i_op->truncate)
	    inode->i_op->truncate(inode);
    }
#endif
    code = -code;
#ifdef STRUCT_INODE_HAS_I_MUTEX
    mutex_unlock(&inode->i_mutex);
#else
    up(&inode->i_sem);
#endif
#ifdef STRUCT_INODE_HAS_I_ALLOC_SEM
    up_write(&inode->i_alloc_sem);
#endif
    AFS_GLOCK();
    MReleaseWriteLock(&afs_xosi);
    return code;
}


/* Generic read interface */
int
afs_osi_Read(register struct osi_file *afile, int offset, void *aptr,
	     afs_int32 asize)
{
    struct uio auio;
    struct iovec iov;
    afs_int32 code;

    AFS_STATCNT(osi_Read);

    /*
     * If the osi_file passed in is NULL, panic only if AFS is not shutting
     * down. No point in crashing when we are already shutting down
     */
    if (!afile) {
	if (!afs_shuttingdown)
	    osi_Panic("osi_Read called with null param");
	else
	    return EIO;
    }

    if (offset != -1)
	afile->offset = offset;
    setup_uio(&auio, &iov, aptr, afile->offset, asize, UIO_READ, AFS_UIOSYS);
    AFS_GUNLOCK();
    code = osi_rdwr(afile, &auio, UIO_READ);
    AFS_GLOCK();
    if (code == 0) {
	code = asize - auio.uio_resid;
	afile->offset += code;
    } else {
	afs_Trace2(afs_iclSetp, CM_TRACE_READFAILED, ICL_TYPE_INT32, auio.uio_resid,
		   ICL_TYPE_INT32, code);
	code = -1;
    }
    return code;
}

/* Generic write interface */
int
afs_osi_Write(register struct osi_file *afile, afs_int32 offset, void *aptr,
	      afs_int32 asize)
{
    struct uio auio;
    struct iovec iov;
    afs_int32 code;

    AFS_STATCNT(osi_Write);

    if (!afile) {
	if (!afs_shuttingdown)
	    osi_Panic("afs_osi_Write called with null param");
	else
	    return EIO;
    }

    if (offset != -1)
	afile->offset = offset;
    setup_uio(&auio, &iov, aptr, afile->offset, asize, UIO_WRITE, AFS_UIOSYS);
    AFS_GUNLOCK();
    code = osi_rdwr(afile, &auio, UIO_WRITE);
    AFS_GLOCK();
    if (code == 0) {
	code = asize - auio.uio_resid;
	afile->offset += code;
    } else {
	if (code == ENOSPC)
	    afs_warnuser
		("\n\n\n*** Cache partition is FULL - Decrease cachesize!!! ***\n\n");
	code = -1;
    }

    if (afile->proc)
	(*afile->proc)(afile, code);

    return code;
}


/*  This work should be handled by physstrat in ca/machdep.c.
    This routine written from the RT NFS port strategy routine.
    It has been generalized a bit, but should still be pretty clear. */
int
afs_osi_MapStrategy(int (*aproc) (struct buf * bp), register struct buf *bp)
{
    afs_int32 returnCode;

    AFS_STATCNT(osi_MapStrategy);
    returnCode = (*aproc) (bp);

    return returnCode;
}

void
shutdown_osifile(void)
{
    AFS_STATCNT(shutdown_osifile);
    if (afs_cold_shutdown) {
	afs_osicred_initialized = 0;
    }
}

/* Intialize cache device info and fragment size for disk cache partition. */
int
osi_InitCacheInfo(char *aname)
{
    int code;
#if defined(LINUX_USE_FH)
    int max_len = sizeof(struct fid);
#else
    extern ino_t cacheInode;
#endif
    struct dentry *dp;
    extern ino_t cacheInode;
    extern struct osi_dev cacheDev;
    extern afs_int32 afs_fsfragsize;
    extern struct super_block *afs_cacheSBp;
    extern struct vfsmount *afs_cacheMnt;
    code = osi_lookupname_internal(aname, 1, &afs_cacheMnt, &dp);
    if (code)
	return ENOENT;

#if defined(LINUX_USE_FH)
    if (dp->d_sb->s_export_op->encode_fh) {
	cacheitems_fh_type = dp->d_sb->s_export_op->encode_fh(dp, (__u32 *)&cacheitems_fh, &max_len, 0);
    } else {
        cacheitems_fh_type = FILEID_INO32_GEN;
	cacheitems_fh.i32.ino = dp->d_inode->i_ino;
        cacheitems_fh.i32.gen = dp->d_inode->i_generation;
    }
#else
    cacheInode = dp->d_inode->i_ino;
#endif
    cacheDev.dev = dp->d_inode->i_sb->s_dev;
    afs_fsfragsize = dp->d_inode->i_sb->s_blocksize - 1;
    afs_cacheSBp = dp->d_inode->i_sb;

    dput(dp);

    return 0;
}


#define FOP_READ(F, B, C) (F)->f_op->read(F, B, (size_t)(C), &(F)->f_pos)
#define FOP_WRITE(F, B, C) (F)->f_op->write(F, B, (size_t)(C), &(F)->f_pos)

/* osi_rdwr
 * seek, then read or write to an open inode. addrp points to data in
 * kernel space.
 */
int
osi_rdwr(struct osi_file *osifile, uio_t * uiop, int rw)
{
#ifdef AFS_LINUX26_ENV
    struct file *filp = osifile->filp;
#else
    struct file *filp = &osifile->file;
#endif
    KERNEL_SPACE_DECL;
    int code = 0;
    struct iovec *iov;
    afs_size_t count;
    unsigned long savelim;

    savelim = current->TASK_STRUCT_RLIM[RLIMIT_FSIZE].rlim_cur;
    current->TASK_STRUCT_RLIM[RLIMIT_FSIZE].rlim_cur = RLIM_INFINITY;

    if (uiop->uio_seg == AFS_UIOSYS)
	TO_USER_SPACE();

    /* seek to the desired position. Return -1 on error. */
    if (filp->f_op->llseek) {
	if (filp->f_op->llseek(filp, (loff_t) uiop->uio_offset, 0) != uiop->uio_offset)
	    return -1;
    } else
	filp->f_pos = uiop->uio_offset;

    while (code == 0 && uiop->uio_resid > 0 && uiop->uio_iovcnt > 0) {
	iov = uiop->uio_iov;
	count = iov->iov_len;
	if (count == 0) {
	    uiop->uio_iov++;
	    uiop->uio_iovcnt--;
	    continue;
	}

	if (rw == UIO_READ)
	    code = FOP_READ(filp, iov->iov_base, count);
	else
	    code = FOP_WRITE(filp, iov->iov_base, count);

	if (code < 0) {
	    code = -code;
	    break;
	} else if (code == 0) {
	    /*
	     * This is bad -- we can't read any more data from the
	     * file, but we have no good way of signaling a partial
	     * read either.
	     */
	    code = EIO;
	    break;
	}

	iov->iov_base += code;
	iov->iov_len -= code;
	uiop->uio_resid -= code;
	uiop->uio_offset += code;
	code = 0;
    }

    if (uiop->uio_seg == AFS_UIOSYS)
	TO_KERNEL_SPACE();

    current->TASK_STRUCT_RLIM[RLIMIT_FSIZE].rlim_cur = savelim;

    return code;
}

/* setup_uio 
 * Setup a uio struct.
 */
void
setup_uio(uio_t * uiop, struct iovec *iovecp, const char *buf, afs_offs_t pos,
	  int count, uio_flag_t flag, uio_seg_t seg)
{
    iovecp->iov_base = (char *)buf;
    iovecp->iov_len = count;
    uiop->uio_iov = iovecp;
    uiop->uio_iovcnt = 1;
    uiop->uio_offset = pos;
    uiop->uio_seg = seg;
    uiop->uio_resid = count;
    uiop->uio_flag = flag;
}


/* uiomove
 * UIO_READ : dp -> uio
 * UIO_WRITE : uio -> dp
 */
int
uiomove(char *dp, int length, uio_flag_t rw, uio_t * uiop)
{
    int count;
    struct iovec *iov;
    int code;

    while (length > 0 && uiop->uio_resid > 0 && uiop->uio_iovcnt > 0) {
	iov = uiop->uio_iov;
	count = iov->iov_len;

	if (!count) {
	    uiop->uio_iov++;
	    uiop->uio_iovcnt--;
	    continue;
	}

	if (count > length)
	    count = length;

	switch (uiop->uio_seg) {
	case AFS_UIOSYS:
	    switch (rw) {
	    case UIO_READ:
		memcpy(iov->iov_base, dp, count);
		break;
	    case UIO_WRITE:
		memcpy(dp, iov->iov_base, count);
		break;
	    default:
		printf("uiomove: Bad rw = %d\n", rw);
		return -EINVAL;
	    }
	    break;
	case AFS_UIOUSER:
	    switch (rw) {
	    case UIO_READ:
		AFS_COPYOUT(dp, iov->iov_base, count, code);
		break;
	    case UIO_WRITE:
		AFS_COPYIN(iov->iov_base, dp, count, code);
		break;
	    default:
		printf("uiomove: Bad rw = %d\n", rw);
		return -EINVAL;
	    }
	    break;
	default:
	    printf("uiomove: Bad seg = %d\n", uiop->uio_seg);
	    return -EINVAL;
	}

	dp += count;
	length -= count;
	iov->iov_base += count;
	iov->iov_len -= count;
	uiop->uio_offset += count;
	uiop->uio_resid -= count;
    }
    return 0;
}

