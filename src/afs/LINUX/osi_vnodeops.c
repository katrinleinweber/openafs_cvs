/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * Linux specific vnodeops. Also includes the glue routines required to call
 * AFS vnodeops.
 *
 * So far the only truly scary part is that Linux relies on the inode cache
 * to be up to date. Don't you dare break a callback and expect an fstat
 * to give you meaningful information. This appears to be fixed in the 2.1
 * development kernels. As it is we can fix this now by intercepting the 
 * stat calls.
 */

#include <afsconfig.h>
#include "afs/param.h"

RCSID
    ("$Header: /cvs/openafs/src/afs/LINUX/osi_vnodeops.c,v 1.84 2004/10/11 16:21:31 shadow Exp $");

#include "afs/sysincludes.h"
#include "afsincludes.h"
#include "afs/afs_stats.h"
#include "afs/afs_osidnlc.h"
#include "h/mm.h"
#ifdef HAVE_MM_INLINE_H
#include "h/mm_inline.h"
#endif
#include "h/pagemap.h"
#if defined(AFS_LINUX24_ENV)
#include "h/smp_lock.h"
#endif

#ifdef pgoff2loff
#define pageoff(pp) pgoff2loff((pp)->index)
#else
#define pageoff(pp) pp->offset
#endif

#if defined(AFS_LINUX26_ENV)
#define UnlockPage(pp) unlock_page(pp)
#endif

extern struct vcache *afs_globalVp;
extern afs_rwlock_t afs_xvcache;

#if defined(AFS_LINUX24_ENV)
extern struct inode_operations afs_file_iops;
extern struct address_space_operations afs_file_aops;
struct address_space_operations afs_symlink_aops;
#endif
extern struct inode_operations afs_dir_iops;
extern struct inode_operations afs_symlink_iops;


static ssize_t
afs_linux_read(struct file *fp, char *buf, size_t count, loff_t * offp)
{
    ssize_t code;
    struct vcache *vcp = ITOAFS(fp->f_dentry->d_inode);
    cred_t *credp = crref();
    struct vrequest treq;

    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_READOP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_OFFSET, offp, ICL_TYPE_INT32, count, ICL_TYPE_INT32,
	       99999);

    /* get a validated vcache entry */
    code = afs_InitReq(&treq, credp);
    if (!code)
	code = afs_VerifyVCache(vcp, &treq);

    if (code)
	code = -code;
    else {
#ifdef AFS_64BIT_CLIENT
	if (*offp + count > afs_vmMappingEnd) {
	    uio_t tuio;
	    struct iovec iov;
	    afs_size_t oldOffset = *offp;
	    afs_int32 xfered = 0;

	    if (*offp < afs_vmMappingEnd) {
		/* special case of a buffer crossing the VM mapping end */
		afs_int32 tcount = afs_vmMappingEnd - *offp;
		count -= tcount;
		osi_FlushPages(vcp, credp);	/* ensure stale pages are gone */
		AFS_GUNLOCK();
		code = generic_file_read(fp, buf, tcount, offp);
		AFS_GLOCK();
		if (code != tcount) {
		    goto done;
		}
		xfered = tcount;
	    }
	    setup_uio(&tuio, &iov, buf + xfered, (afs_offs_t) * offp, count,
		      UIO_READ, AFS_UIOSYS);
	    code = afs_read(vcp, &tuio, credp, 0, 0, 0);
	    xfered += count - tuio.uio_resid;
	    if (code != 0) {
		afs_Trace4(afs_iclSetp, CM_TRACE_READOP, ICL_TYPE_POINTER,
			   vcp, ICL_TYPE_OFFSET, offp, ICL_TYPE_INT32, -1,
			   ICL_TYPE_INT32, code);
		code = xfered;
		*offp += count - tuio.uio_resid;
	    } else {
		code = xfered;
		*offp += count;
	    }
	  done:
		;
	} else {
#endif /* AFS_64BIT_CLIENT */
	    osi_FlushPages(vcp, credp);	/* ensure stale pages are gone */
	    AFS_GUNLOCK();
	    code = generic_file_read(fp, buf, count, offp);
	    AFS_GLOCK();
#ifdef AFS_64BIT_CLIENT
	}
#endif /* AFS_64BIT_CLIENT */
    }

    afs_Trace4(afs_iclSetp, CM_TRACE_READOP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_OFFSET, offp, ICL_TYPE_INT32, count, ICL_TYPE_INT32,
	       code);

    AFS_GUNLOCK();
    crfree(credp);
    return code;
}


/* Now we have integrated VM for writes as well as reads. generic_file_write
 * also takes care of re-positioning the pointer if file is open in append
 * mode. Call fake open/close to ensure we do writes of core dumps.
 */
static ssize_t
afs_linux_write(struct file *fp, const char *buf, size_t count, loff_t * offp)
{
    ssize_t code = 0;
    int code2;
    struct vcache *vcp = ITOAFS(fp->f_dentry->d_inode);
    struct vrequest treq;
    cred_t *credp = crref();
    afs_offs_t toffs;

    AFS_GLOCK();

    afs_Trace4(afs_iclSetp, CM_TRACE_WRITEOP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_OFFSET, offp, ICL_TYPE_INT32, count, ICL_TYPE_INT32,
	       (fp->f_flags & O_APPEND) ? 99998 : 99999);


    /* get a validated vcache entry */
    code = (ssize_t) afs_InitReq(&treq, credp);
    if (!code)
	code = (ssize_t) afs_VerifyVCache(vcp, &treq);

    ObtainWriteLock(&vcp->lock, 529);
    afs_FakeOpen(vcp);
    ReleaseWriteLock(&vcp->lock);
    if (code)
	code = -code;
    else {
#ifdef AFS_64BIT_CLIENT
	toffs = *offp;
	if (fp->f_flags & O_APPEND)
	    toffs += vcp->m.Length;
	if (toffs + count > afs_vmMappingEnd) {
	    uio_t tuio;
	    struct iovec iov;
	    afs_size_t oldOffset = *offp;
	    afs_int32 xfered = 0;

	    if (toffs < afs_vmMappingEnd) {
		/* special case of a buffer crossing the VM mapping end */
		afs_int32 tcount = afs_vmMappingEnd - *offp;
		count -= tcount;
		AFS_GUNLOCK();
		code = generic_file_write(fp, buf, tcount, offp);
		AFS_GLOCK();
		if (code != tcount) {
		    goto done;
		}
		xfered = tcount;
		toffs += tcount;
	    }
	    setup_uio(&tuio, &iov, buf + xfered, (afs_offs_t) toffs, count,
		      UIO_WRITE, AFS_UIOSYS);
	    code = afs_write(vcp, &tuio, fp->f_flags, credp, 0);
	    xfered += count - tuio.uio_resid;
	    if (code != 0) {
		code = xfered;
		*offp += count - tuio.uio_resid;
	    } else {
		/* Purge dirty chunks of file if there are too many dirty chunks.
		 * Inside the write loop, we only do this at a chunk boundary.
		 * Clean up partial chunk if necessary at end of loop.
		 */
		if (AFS_CHUNKBASE(tuio.afsio_offset) !=
		    AFS_CHUNKBASE(oldOffset)) {
		    ObtainWriteLock(&vcp->lock, 402);
		    code = afs_DoPartialWrite(vcp, &treq);
		    vcp->states |= CDirty;
		    ReleaseWriteLock(&vcp->lock);
		}
		code = xfered;
		*offp += count;
		toffs += count;
		ObtainWriteLock(&vcp->lock, 400);
		vcp->m.Date = osi_Time();	/* Set file date (for ranlib) */
		/* extend file */
		if (!(fp->f_flags & O_APPEND) && toffs > vcp->m.Length) {
		    vcp->m.Length = toffs;
		}
		ReleaseWriteLock(&vcp->lock);
	    }
	  done:
		;
	} else {
#endif /* AFS_64BIT_CLIENT */
	    AFS_GUNLOCK();
	    code = generic_file_write(fp, buf, count, offp);
	    AFS_GLOCK();
#ifdef AFS_64BIT_CLIENT
	}
#endif /* AFS_64BIT_CLIENT */
    }

    ObtainWriteLock(&vcp->lock, 530);
    vcp->m.Date = osi_Time();	/* set modification time */
    afs_FakeClose(vcp, credp);
    if (code >= 0)
	code2 = afs_DoPartialWrite(vcp, &treq);
    if (code2 && code >= 0)
	code = (ssize_t) - code2;
    ReleaseWriteLock(&vcp->lock);

    afs_Trace4(afs_iclSetp, CM_TRACE_WRITEOP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_OFFSET, offp, ICL_TYPE_INT32, count, ICL_TYPE_INT32,
	       code);

    AFS_GUNLOCK();
    crfree(credp);
    return code;
}

/* This is a complete rewrite of afs_readdir, since we can make use of
 * filldir instead of afs_readdir_move. Note that changes to vcache/dcache
 * handling and use of bulkstats will need to be reflected here as well.
 */
static int
afs_linux_readdir(struct file *fp, void *dirbuf, filldir_t filldir)
{
    extern struct DirEntry *afs_dir_GetBlob();
    struct vcache *avc = ITOAFS(FILE_INODE(fp));
    struct vrequest treq;
    register struct dcache *tdc;
    int code;
    int offset;
    int dirpos;
    struct DirEntry *de;
    ino_t ino;
    int len;
    afs_size_t origOffset, tlen;
    cred_t *credp = crref();
    struct afs_fakestat_state fakestat;

#if defined(AFS_LINUX26_ENV)
    lock_kernel();
#endif
    AFS_GLOCK();
    AFS_STATCNT(afs_readdir);

    code = afs_InitReq(&treq, credp);
    crfree(credp);
    if (code)
	goto out1;

    afs_InitFakeStat(&fakestat);
    code = afs_EvalFakeStat(&avc, &fakestat, &treq);
    if (code)
	goto out;

    /* update the cache entry */
  tagain:
    code = afs_VerifyVCache(avc, &treq);
    if (code)
	goto out;

    /* get a reference to the entire directory */
    tdc = afs_GetDCache(avc, (afs_size_t) 0, &treq, &origOffset, &tlen, 1);
    len = tlen;
    if (!tdc) {
	code = -ENOENT;
	goto out;
    }
    ObtainReadLock(&avc->lock);
    ObtainReadLock(&tdc->lock);
    /*
     * Make sure that the data in the cache is current. There are two
     * cases we need to worry about:
     * 1. The cache data is being fetched by another process.
     * 2. The cache data is no longer valid
     */
    while ((avc->states & CStatd)
	   && (tdc->dflags & DFFetching)
	   && hsame(avc->m.DataVersion, tdc->f.versionNo)) {
	ReleaseReadLock(&tdc->lock);
	ReleaseReadLock(&avc->lock);
	afs_osi_Sleep(&tdc->validPos);
	ObtainReadLock(&avc->lock);
	ObtainReadLock(&tdc->lock);
    }
    if (!(avc->states & CStatd)
	|| !hsame(avc->m.DataVersion, tdc->f.versionNo)) {
	ReleaseReadLock(&tdc->lock);
	ReleaseReadLock(&avc->lock);
	afs_PutDCache(tdc);
	goto tagain;
    }

    /* Fill in until we get an error or we're done. This implementation
     * takes an offset in units of blobs, rather than bytes.
     */
    code = 0;
    offset = (int) fp->f_pos;
    while (1) {
	dirpos = BlobScan(&tdc->f.inode, offset);
	if (!dirpos)
	    break;

	de = afs_dir_GetBlob(&tdc->f.inode, dirpos);
	if (!de)
	    break;

	ino = (avc->fid.Fid.Volume << 16) + ntohl(de->fid.vnode);
	ino &= 0x7fffffff;	/* Assumes 32 bit ino_t ..... */
	if (de->name)
	    len = strlen(de->name);
	else {
	    printf("afs_linux_readdir: afs_dir_GetBlob failed, null name (inode %x, dirpos %d)\n", 
		   &tdc->f.inode, dirpos);
	    DRelease((struct buffer *) de, 0);
	    afs_PutDCache(tdc);
	    ReleaseReadLock(&avc->lock);
	    code = -ENOENT;
	    goto out;
	}

	/* filldir returns -EINVAL when the buffer is full. */
#if defined(AFS_LINUX26_ENV) || ((defined(AFS_LINUX24_ENV) || defined(pgoff2loff)) && defined(DECLARE_FSTYPE))
	{
	    unsigned int type = DT_UNKNOWN;
	    struct VenusFid afid;
	    struct vcache *tvc;
	    int vtype;
	    afid.Cell = avc->fid.Cell;
	    afid.Fid.Volume = avc->fid.Fid.Volume;
	    afid.Fid.Vnode = ntohl(de->fid.vnode);
	    afid.Fid.Unique = ntohl(de->fid.vunique);
	    if ((avc->states & CForeign) == 0 && (ntohl(de->fid.vnode) & 1)) {
		type = DT_DIR;
	    } else if ((tvc = afs_FindVCache(&afid, 0, 0))) {
		if (tvc->mvstat) {
		    type = DT_DIR;
		} else if (((tvc->states) & (CStatd | CTruth))) {
		    /* CTruth will be set if the object has
		     *ever* been statd */
		    vtype = vType(tvc);
		    if (vtype == VDIR)
			type = DT_DIR;
		    else if (vtype == VREG)
			type = DT_REG;
		    /* Don't do this until we're sure it can't be a mtpt */
		    /* else if (vtype == VLNK)
		     * type=DT_LNK; */
		    /* what other types does AFS support? */
		}
		/* clean up from afs_FindVCache */
		afs_PutVCache(tvc);
	    }
	    code = (*filldir) (dirbuf, de->name, len, offset, ino, type);
	}
#else
	code = (*filldir) (dirbuf, de->name, len, offset, ino);
#endif
	DRelease((struct buffer *)de, 0);
	if (code)
	    break;
	offset = dirpos + 1 + ((len + 16) >> 5);
    }
    /* If filldir didn't fill in the last one this is still pointing to that
     * last attempt.
     */
    fp->f_pos = (loff_t) offset;

    ReleaseReadLock(&tdc->lock);
    afs_PutDCache(tdc);
    ReleaseReadLock(&avc->lock);
    code = 0;

out:
    afs_PutFakeStat(&fakestat);
out1:
    AFS_GUNLOCK();
#if defined(AFS_LINUX26_ENV)
    unlock_kernel();
#endif
    return code;
}


/* in afs_pioctl.c */
extern int afs_xioctl(struct inode *ip, struct file *fp, unsigned int com,
		      unsigned long arg);


/* We need to detect unmap's after close. To do that, we need our own
 * vm_operations_struct's. And we need to set them up for both the
 * private and shared mappings. The fun part is that these are all static
 * so we'll have to initialize on the fly!
 */
static struct vm_operations_struct afs_private_mmap_ops;
static int afs_private_mmap_ops_inited = 0;
static struct vm_operations_struct afs_shared_mmap_ops;
static int afs_shared_mmap_ops_inited = 0;

void
afs_linux_vma_close(struct vm_area_struct *vmap)
{
    struct vcache *vcp;
    cred_t *credp;
    int need_unlock = 0;

    if (!vmap->vm_file)
	return;

    vcp = ITOAFS(FILE_INODE(vmap->vm_file));
    if (!vcp)
	return;

    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_VM_CLOSE, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_INT32, vcp->mapcnt, ICL_TYPE_INT32, vcp->opens,
	       ICL_TYPE_INT32, vcp->execsOrWriters);
    if ((&vcp->lock)->excl_locked == 0 || (&vcp->lock)->pid_writer == MyPidxx) {
	ObtainWriteLock(&vcp->lock, 532);
	need_unlock = 1;
    } else
	printk("AFS_VMA_CLOSE(%d): Skipping Already locked vcp=%p vmap=%p\n",
	       MyPidxx, &vcp, &vmap);
    if (vcp->mapcnt) {
	vcp->mapcnt--;
	if (need_unlock)
	    ReleaseWriteLock(&vcp->lock);
	if (!vcp->mapcnt) {
	    if (need_unlock && vcp->execsOrWriters < 2) {
		credp = crref();
		(void)afs_close(vcp, vmap->vm_file->f_flags, credp);
		/* only decrement the execsOrWriters flag if this is not a
		 * writable file. */
		if (!(vcp->states & CRO) )
		    if (! (vmap->vm_file->f_flags & (FWRITE | FTRUNC)))
			vcp->execsOrWriters--;
		vcp->states &= ~CMAPPED;
		crfree(credp);
	    } else if ((vmap->vm_file->f_flags & (FWRITE | FTRUNC)))
		vcp->execsOrWriters--;
	    /* If we did not have the lock */
	    if (!need_unlock) {
		vcp->mapcnt++;
		if (!vcp->execsOrWriters)
		    vcp->execsOrWriters = 1;
	    }
	}
    } else {
	if (need_unlock)
	    ReleaseWriteLock(&vcp->lock);
    }

  unlock_exit:
    AFS_GUNLOCK();
}

static int
afs_linux_mmap(struct file *fp, struct vm_area_struct *vmap)
{
    struct vcache *vcp = ITOAFS(FILE_INODE(fp));
    cred_t *credp = crref();
    struct vrequest treq;
    int code;

    AFS_GLOCK();
#if defined(AFS_LINUX24_ENV)
    afs_Trace3(afs_iclSetp, CM_TRACE_GMAP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, vmap->vm_start, ICL_TYPE_INT32,
	       vmap->vm_end - vmap->vm_start);
#else
    afs_Trace4(afs_iclSetp, CM_TRACE_GMAP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, vmap->vm_start, ICL_TYPE_INT32,
	       vmap->vm_end - vmap->vm_start, ICL_TYPE_INT32,
	       vmap->vm_offset);
#endif

    /* get a validated vcache entry */
    code = afs_InitReq(&treq, credp);
    if (!code)
	code = afs_VerifyVCache(vcp, &treq);


    if (code)
	code = -code;
    else {
	osi_FlushPages(vcp, credp);	/* ensure stale pages are gone */

	AFS_GUNLOCK();
	code = generic_file_mmap(fp, vmap);
	AFS_GLOCK();
    }

    if (code == 0) {
	ObtainWriteLock(&vcp->lock, 531);
	/* Set out vma ops so we catch the close. The following test should be
	 * the same as used in generic_file_mmap.
	 */
	if ((vmap->vm_flags & VM_SHARED) && (vmap->vm_flags & VM_MAYWRITE)) {
	    if (!afs_shared_mmap_ops_inited) {
		afs_shared_mmap_ops_inited = 1;
		afs_shared_mmap_ops = *vmap->vm_ops;
		afs_shared_mmap_ops.close = afs_linux_vma_close;
	    }
	    vmap->vm_ops = &afs_shared_mmap_ops;
	} else {
	    if (!afs_private_mmap_ops_inited) {
		afs_private_mmap_ops_inited = 1;
		afs_private_mmap_ops = *vmap->vm_ops;
		afs_private_mmap_ops.close = afs_linux_vma_close;
	    }
	    vmap->vm_ops = &afs_private_mmap_ops;
	}


	/* Add an open reference on the first mapping. */
	if (vcp->mapcnt == 0) {
	    if (!(vcp->states & CRO))
		vcp->execsOrWriters++;
	    vcp->opens++;
	    vcp->states |= CMAPPED;
	}
	ReleaseWriteLock(&vcp->lock);
	vcp->mapcnt++;
    }

    AFS_GUNLOCK();
    crfree(credp);
    return code;
}

int
afs_linux_open(struct inode *ip, struct file *fp)
{
    int code;
    cred_t *credp = crref();

#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    AFS_GLOCK();
    code = afs_open((struct vcache **)&ip, fp->f_flags, credp);
    AFS_GUNLOCK();
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif

    crfree(credp);
    return -code;
}

/* afs_Close is called from release, since release is used to handle all
 * file closings. In addition afs_linux_flush is called from sys_close to
 * handle flushing the data back to the server. The kicker is that we could
 * ignore flush completely if only sys_close took it's return value from
 * fput. See afs_linux_flush for notes on interactions between release and
 * flush.
 */
static int
afs_linux_release(struct inode *ip, struct file *fp)
{
    int code = 0;
    cred_t *credp = crref();
    struct vcache *vcp = ITOAFS(ip);

#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    AFS_GLOCK();
    if (vcp->flushcnt) {
	vcp->flushcnt--;	/* protected by AFS global lock. */
    } else {
	code = afs_close(vcp, fp->f_flags, credp);
    }
    AFS_GUNLOCK();
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif

    crfree(credp);
    return -code;
}

#if defined(AFS_LINUX24_ENV)
static int
afs_linux_fsync(struct file *fp, struct dentry *dp, int datasync)
#else
static int
afs_linux_fsync(struct file *fp, struct dentry *dp)
#endif
{
    int code;
    struct inode *ip = FILE_INODE(fp);
    cred_t *credp = crref();

#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    AFS_GLOCK();
    code = afs_fsync(ITOAFS(ip), credp);
    AFS_GUNLOCK();
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif
    crfree(credp);
    return -code;

}


static int
afs_linux_lock(struct file *fp, int cmd, struct file_lock *flp)
{
    int code = 0;
    struct vcache *vcp = ITOAFS(FILE_INODE(fp));
    cred_t *credp = crref();
    struct AFS_FLOCK flock;
    /* Convert to a lock format afs_lockctl understands. */
    memset((char *)&flock, 0, sizeof(flock));
    flock.l_type = flp->fl_type;
    flock.l_pid = flp->fl_pid;
    flock.l_whence = 0;
    flock.l_start = flp->fl_start;
    flock.l_len = flp->fl_end - flp->fl_start;

    /* Safe because there are no large files, yet */
#if defined(F_GETLK64) && (F_GETLK != F_GETLK64)
    if (cmd == F_GETLK64)
	cmd = F_GETLK;
    else if (cmd == F_SETLK64)
	cmd = F_SETLK;
    else if (cmd == F_SETLKW64)
	cmd = F_SETLKW;
#endif /* F_GETLK64 && F_GETLK != F_GETLK64 */

    AFS_GLOCK();
    code = afs_lockctl(vcp, &flock, cmd, credp);
    AFS_GUNLOCK();

    /* Convert flock back to Linux's file_lock */
    flp->fl_type = flock.l_type;
    flp->fl_pid = flock.l_pid;
    flp->fl_start = flock.l_start;
    flp->fl_end = flock.l_start + flock.l_len;

    crfree(credp);
    return -code;

}

/* afs_linux_flush
 * flush is called from sys_close. We could ignore it, but sys_close return
 * code comes from flush, not release. We need to use release to keep
 * the vcache open count correct. Note that flush is called before release
 * (via fput) in sys_close. vcp->flushcnt is a bit of ugliness to avoid
 * races and also avoid calling afs_close twice when closing the file.
 * If we merely checked for opens > 0 in afs_linux_release, then if an
 * new open occurred when storing back the file, afs_linux_release would
 * incorrectly close the file and decrement the opens count. Calling afs_close
 * on the just flushed file is wasteful, since the background daemon will
 * execute the code that finally decides there is nothing to do.
 */
int
afs_linux_flush(struct file *fp)
{
    struct vcache *vcp = ITOAFS(FILE_INODE(fp));
    int code = 0;
    cred_t *credp;

    /* Only do this on the last close of the file pointer. */
#if defined(AFS_LINUX24_ENV)
    if (atomic_read(&fp->f_count) > 1)
#else
    if (fp->f_count > 1)
#endif
	return 0;

    credp = crref();

    AFS_GLOCK();
    code = afs_close(vcp, fp->f_flags, credp);
    vcp->flushcnt++;		/* protected by AFS global lock. */
    AFS_GUNLOCK();

    crfree(credp);
    return -code;
}

#if !defined(AFS_LINUX24_ENV)
/* Not allowed to directly read a directory. */
ssize_t
afs_linux_dir_read(struct file * fp, char *buf, size_t count, loff_t * ppos)
{
    return -EISDIR;
}
#endif



struct file_operations afs_dir_fops = {
#if !defined(AFS_LINUX24_ENV)
  .read =	afs_linux_dir_read,
  .lock =	afs_linux_lock,
  .fsync =	afs_linux_fsync,
#else
  .read =	generic_read_dir,
#endif
  .readdir =	afs_linux_readdir,
  .ioctl =	afs_xioctl,
  .open =	afs_linux_open,
  .release =	afs_linux_release,
};

struct file_operations afs_file_fops = {
  .read =	afs_linux_read,
  .write =	afs_linux_write,
  .ioctl =	afs_xioctl,
  .mmap =	afs_linux_mmap,
  .open =	afs_linux_open,
  .flush =	afs_linux_flush,
  .release =	afs_linux_release,
  .fsync =	afs_linux_fsync,
  .lock =	afs_linux_lock,
};


/**********************************************************************
 * AFS Linux dentry operations
 **********************************************************************/

/* afs_linux_revalidate
 * Ensure vcache is stat'd before use. Return 0 if entry is valid.
 */
static int
afs_linux_revalidate(struct dentry *dp)
{
    int code;
    cred_t *credp;
    struct vrequest treq;
    struct vcache *vcp = ITOAFS(dp->d_inode);
    struct vcache *rootvp = NULL;

#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    AFS_GLOCK();

    if (afs_fakestat_enable && vcp->mvstat == 1 && vcp->mvid
	&& (vcp->states & CMValid) && (vcp->states & CStatd)) {
	ObtainSharedLock(&afs_xvcache, 680);
	rootvp = afs_FindVCache(vcp->mvid, 0, 0);
	ReleaseSharedLock(&afs_xvcache);
    }

    /* Make this a fast path (no crref), since it's called so often. */
    if (vcp->states & CStatd) {
	if (*dp->d_name.name != '/' && vcp->mvstat == 2)	/* root vnode */
	    check_bad_parent(dp);	/* check and correct mvid */
	if (rootvp)
	    vcache2fakeinode(rootvp, vcp);
	else
	    vcache2inode(vcp);
	if (rootvp)
	    afs_PutVCache(rootvp);
	AFS_GUNLOCK();
#ifdef AFS_LINUX24_ENV
	unlock_kernel();
#endif
	return 0;
    }

    credp = crref();
    code = afs_InitReq(&treq, credp);
    if (!code)
	code = afs_VerifyVCache(vcp, &treq);

    AFS_GUNLOCK();
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif
    crfree(credp);

    return -code;
}

#if defined(AFS_LINUX26_ENV)
static int
afs_linux_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
        int err = afs_linux_revalidate(dentry);
        if (!err)
                generic_fillattr(dentry->d_inode, stat);
        return err;
}
#endif

/* Validate a dentry. Return 1 if unchanged, 0 if VFS layer should re-evaluate.
 * In kernels 2.2.10 and above, we are passed an additional flags var which
 * may have either the LOOKUP_FOLLOW OR LOOKUP_DIRECTORY set in which case
 * we are advised to follow the entry if it is a link or to make sure that 
 * it is a directory. But since the kernel itself checks these possibilities
 * later on, we shouldn't have to do it until later. Perhaps in the future..
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,10)
static int
afs_linux_dentry_revalidate(struct dentry *dp, int flags)
#else
static int
afs_linux_dentry_revalidate(struct dentry *dp)
#endif
{
    char *name;
    cred_t *credp = crref();
    struct vrequest treq;
    struct vcache *lookupvcp = NULL;
    int code, bad_dentry = 1;
    struct sysname_info sysState;
    struct vcache *vcp, *parentvcp;

    sysState.allocked = 0;

#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    AFS_GLOCK();

    vcp = ITOAFS(dp->d_inode);
    parentvcp = ITOAFS(dp->d_parent->d_inode);

    /* If it's a negative dentry, then there's nothing to do. */
    if (!vcp || !parentvcp)
	goto done;

    /* If it is the AFS root, then there's no chance it needs 
     * revalidating */
    if (vcp == afs_globalVp) {
	bad_dentry = 0;
	goto done;
    }

    if ((code = afs_InitReq(&treq, credp)))
	goto done;

    Check_AtSys(parentvcp, dp->d_name.name, &sysState, &treq);
    name = sysState.name;

    /* First try looking up the DNLC */
    if ((lookupvcp = osi_dnlc_lookup(parentvcp, name, WRITE_LOCK))) {
	/* Verify that the dentry does not point to an old inode */
	if (vcp != lookupvcp)
	    goto done;
	/* Check and correct mvid */
	if (*name != '/' && vcp->mvstat == 2)
	    check_bad_parent(dp);
	vcache2inode(vcp);
	bad_dentry = 0;
	goto done;
    }

    /* A DNLC lookup failure cannot be trusted. Try a real lookup. 
       Make sure to try the real name and not the @sys expansion; 
       afs_lookup will expand @sys itself. */
  
    code = afs_lookup(parentvcp, dp->d_name.name, &lookupvcp, credp);

    /* Verify that the dentry does not point to an old inode */
    if (vcp != lookupvcp)
	goto done;

    bad_dentry = 0;

  done:
    /* Clean up */
    if (lookupvcp)
	afs_PutVCache(lookupvcp);
    if (sysState.allocked)
	osi_FreeLargeSpace(name);

    AFS_GUNLOCK();

    if (bad_dentry) {
	shrink_dcache_parent(dp);
	d_drop(dp);
    }

#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif
    crfree(credp);

    return !bad_dentry;
}

#if !defined(AFS_LINUX26_ENV)
/* afs_dentry_iput */
static void
afs_dentry_iput(struct dentry *dp, struct inode *ip)
{
    int isglock;

    if (ICL_SETACTIVE(afs_iclSetp)) {
	isglock = ISAFS_GLOCK();
	if (!isglock) AFS_GLOCK();
	afs_Trace3(afs_iclSetp, CM_TRACE_DENTRYIPUT, ICL_TYPE_POINTER, ip,
		   ICL_TYPE_STRING, dp->d_parent->d_name.name,
		   ICL_TYPE_STRING, dp->d_name.name);
	if (!isglock) AFS_GUNLOCK();
    }

    osi_iput(ip);
}
#endif

static int
afs_dentry_delete(struct dentry *dp)
{
    int isglock;
    if (ICL_SETACTIVE(afs_iclSetp)) {
	isglock = ISAFS_GLOCK();
	if (!isglock) AFS_GLOCK();
	afs_Trace3(afs_iclSetp, CM_TRACE_DENTRYDELETE, ICL_TYPE_POINTER,
		   dp->d_inode, ICL_TYPE_STRING, dp->d_parent->d_name.name,
		   ICL_TYPE_STRING, dp->d_name.name);
	if (!isglock) AFS_GUNLOCK();
    }

    if (dp->d_inode && (ITOAFS(dp->d_inode)->states & CUnlinked))
	return 1;		/* bad inode? */

    return 0;
}

struct dentry_operations afs_dentry_operations = {
  .d_revalidate =	afs_linux_dentry_revalidate,
  .d_delete =		afs_dentry_delete,
#if !defined(AFS_LINUX26_ENV)
  .d_iput =		afs_dentry_iput,
#endif
};

/**********************************************************************
 * AFS Linux inode operations
 **********************************************************************/

/* afs_linux_create
 *
 * Merely need to set enough of vattr to get us through the create. Note
 * that the higher level code (open_namei) will take care of any tuncation
 * explicitly. Exclusive open is also taken care of in open_namei.
 *
 * name is in kernel space at this point.
 */
int
afs_linux_create(struct inode *dip, struct dentry *dp, int mode)
{
    int code;
    cred_t *credp = crref();
    struct vattr vattr;
    enum vcexcl excl;
    const char *name = dp->d_name.name;
    struct inode *ip;

    VATTR_NULL(&vattr);
    vattr.va_mode = mode;

#if defined(AFS_LINUX26_ENV)
    lock_kernel();
#endif
    AFS_GLOCK();
    code =
	afs_create(ITOAFS(dip), name, &vattr, NONEXCL, mode,
		   (struct vcache **)&ip, credp);

    if (!code) {
	vattr2inode(ip, &vattr);
	/* Reset ops if symlink or directory. */
#if defined(AFS_LINUX24_ENV)
	if (S_ISREG(ip->i_mode)) {
	    ip->i_op = &afs_file_iops;
	    ip->i_fop = &afs_file_fops;
	    ip->i_data.a_ops = &afs_file_aops;
	} else if (S_ISDIR(ip->i_mode)) {
	    ip->i_op = &afs_dir_iops;
	    ip->i_fop = &afs_dir_fops;
	} else if (S_ISLNK(ip->i_mode)) {
	    ip->i_op = &afs_symlink_iops;
	    ip->i_data.a_ops = &afs_symlink_aops;
	    ip->i_mapping = &ip->i_data;
	} else
	    printk("afs_linux_create: FIXME\n");
#else
	if (S_ISDIR(ip->i_mode))
	    ip->i_op = &afs_dir_iops;
	else if (S_ISLNK(ip->i_mode))
	    ip->i_op = &afs_symlink_iops;
#endif

	dp->d_op = &afs_dentry_operations;
	dp->d_time = jiffies;
	d_instantiate(dp, ip);
    }

    AFS_GUNLOCK();
#if defined(AFS_LINUX26_ENV)
    unlock_kernel();
#endif
    crfree(credp);
    return -code;
}

/* afs_linux_lookup */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,10)
struct dentry *
afs_linux_lookup(struct inode *dip, struct dentry *dp)
#else
int
afs_linux_lookup(struct inode *dip, struct dentry *dp)
#endif
{
    int code = 0;
    cred_t *credp = crref();
    struct vcache *vcp = NULL;
    const char *comp = dp->d_name.name;

#if defined(AFS_LINUX26_ENV)
    lock_kernel();
#endif
    AFS_GLOCK();
    code = afs_lookup(ITOAFS(dip), comp, &vcp, credp);
    AFS_GUNLOCK();
    
    if (vcp) {
	struct inode *ip = AFSTOI(vcp);
	/* Reset ops if symlink or directory. */
#if defined(AFS_LINUX24_ENV)
	if (S_ISREG(ip->i_mode)) {
	    ip->i_op = &afs_file_iops;
	    ip->i_fop = &afs_file_fops;
	    ip->i_data.a_ops = &afs_file_aops;
	} else if (S_ISDIR(ip->i_mode)) {
	    ip->i_op = &afs_dir_iops;
	    ip->i_fop = &afs_dir_fops;
	} else if (S_ISLNK(ip->i_mode)) {
	    ip->i_op = &afs_symlink_iops;
	    ip->i_data.a_ops = &afs_symlink_aops;
	    ip->i_mapping = &ip->i_data;
	} else
	    printk
		("afs_linux_lookup: ip->i_mode 0x%x  dp->d_name.name %s  code %d\n",
		 ip->i_mode, dp->d_name.name, code);
#ifdef STRUCT_INODE_HAS_I_SECURITY
       if (ip->i_security == NULL) {
           if (security_inode_alloc(ip))
               panic("afs_linux_lookup: Cannot allocate inode security");
       }
#endif
#else
	if (S_ISDIR(ip->i_mode))
	    ip->i_op = &afs_dir_iops;
	else if (S_ISLNK(ip->i_mode))
	    ip->i_op = &afs_symlink_iops;
#endif
    }
    dp->d_time = jiffies;
    dp->d_op = &afs_dentry_operations;
    d_add(dp, AFSTOI(vcp));

#if defined(AFS_LINUX26_ENV)
    unlock_kernel();
#endif
    crfree(credp);

    /* It's ok for the file to not be found. That's noted by the caller by
     * seeing that the dp->d_inode field is NULL.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,10)
    if (code == ENOENT)
	return ERR_PTR(0);
    else
	return ERR_PTR(-code);
#else
    if (code == ENOENT)
	code = 0;
    return -code;
#endif
}

int
afs_linux_link(struct dentry *olddp, struct inode *dip, struct dentry *newdp)
{
    int code;
    cred_t *credp = crref();
    const char *name = newdp->d_name.name;
    struct inode *oldip = olddp->d_inode;

    /* If afs_link returned the vnode, we could instantiate the
     * dentry. Since it's not, we drop this one and do a new lookup.
     */
    d_drop(newdp);

    AFS_GLOCK();
    code = afs_link(ITOAFS(oldip), ITOAFS(dip), name, credp);

    AFS_GUNLOCK();
    crfree(credp);
    return -code;
}

int
afs_linux_unlink(struct inode *dip, struct dentry *dp)
{
    int code;
    cred_t *credp = crref();
    const char *name = dp->d_name.name;

#if defined(AFS_LINUX26_ENV)
    lock_kernel();
#endif
    AFS_GLOCK();
    code = afs_remove(ITOAFS(dip), name, credp);
    AFS_GUNLOCK();
    if (!code)
	d_drop(dp);
#if defined(AFS_LINUX26_ENV)
    unlock_kernel();
#endif
    crfree(credp);
    return -code;
}


int
afs_linux_symlink(struct inode *dip, struct dentry *dp, const char *target)
{
    int code;
    cred_t *credp = crref();
    struct vattr vattr;
    const char *name = dp->d_name.name;

    /* If afs_symlink returned the vnode, we could instantiate the
     * dentry. Since it's not, we drop this one and do a new lookup.
     */
    d_drop(dp);

    AFS_GLOCK();
    VATTR_NULL(&vattr);
    code = afs_symlink(ITOAFS(dip), name, &vattr, target, credp);
    AFS_GUNLOCK();
    crfree(credp);
    return -code;
}

int
afs_linux_mkdir(struct inode *dip, struct dentry *dp, int mode)
{
    int code;
    cred_t *credp = crref();
    struct vcache *tvcp = NULL;
    struct vattr vattr;
    const char *name = dp->d_name.name;

#if defined(AFS_LINUX26_ENV)
    lock_kernel();
#endif
    AFS_GLOCK();
    VATTR_NULL(&vattr);
    vattr.va_mask = ATTR_MODE;
    vattr.va_mode = mode;
    code = afs_mkdir(ITOAFS(dip), name, &vattr, &tvcp, credp);
    AFS_GUNLOCK();

    if (tvcp) {
	tvcp->v.v_op = &afs_dir_iops;
#if defined(AFS_LINUX24_ENV)
	tvcp->v.v_fop = &afs_dir_fops;
#endif
	dp->d_op = &afs_dentry_operations;
	dp->d_time = jiffies;
	d_instantiate(dp, AFSTOI(tvcp));
    }

#if defined(AFS_LINUX26_ENV)
    unlock_kernel();
#endif
    crfree(credp);
    return -code;
}

int
afs_linux_rmdir(struct inode *dip, struct dentry *dp)
{
    int code;
    cred_t *credp = crref();
    const char *name = dp->d_name.name;

#if defined(AFS_LINUX26_ENV)
    lock_kernel();
#endif
    AFS_GLOCK();
    code = afs_rmdir(ITOAFS(dip), name, credp);
    AFS_GUNLOCK();

    /* Linux likes to see ENOTEMPTY returned from an rmdir() syscall
     * that failed because a directory is not empty. So, we map
     * EEXIST to ENOTEMPTY on linux.
     */
    if (code == EEXIST) {
	code = ENOTEMPTY;
    }

    if (!code) {
	d_drop(dp);
    }

#if defined(AFS_LINUX26_ENV)
    unlock_kernel();
#endif
    crfree(credp);
    return -code;
}



int
afs_linux_rename(struct inode *oldip, struct dentry *olddp,
		 struct inode *newip, struct dentry *newdp)
{
    int code;
    cred_t *credp = crref();
    const char *oldname = olddp->d_name.name;
    const char *newname = newdp->d_name.name;

#if defined(AFS_LINUX26_ENV)
    lock_kernel();
#endif
    /* Remove old and new entries from name hash. New one will change below.
     * While it's optimal to catch failures and re-insert newdp into hash,
     * it's also error prone and in that case we're already dealing with error
     * cases. Let another lookup put things right, if need be.
     */
#if defined(AFS_LINUX26_ENV)
    if (!d_unhashed(olddp))
	d_drop(olddp);
    if (!d_unhashed(newdp))
	d_drop(newdp);
#else
    if (!list_empty(&olddp->d_hash))
	d_drop(olddp);
    if (!list_empty(&newdp->d_hash))
	d_drop(newdp);
#endif
    AFS_GLOCK();
    code = afs_rename(ITOAFS(oldip), oldname, ITOAFS(newip), newname, credp);
    AFS_GUNLOCK();

    if (!code) {
	/* update time so it doesn't expire immediately */
	newdp->d_time = jiffies;
	d_move(olddp, newdp);
    }

#if defined(AFS_LINUX26_ENV)
    unlock_kernel();
#endif

    crfree(credp);
    return -code;
}


/* afs_linux_ireadlink 
 * Internal readlink which can return link contents to user or kernel space.
 * Note that the buffer is NOT supposed to be null-terminated.
 */
static int
afs_linux_ireadlink(struct inode *ip, char *target, int maxlen, uio_seg_t seg)
{
    int code;
    cred_t *credp = crref();
    uio_t tuio;
    struct iovec iov;

    setup_uio(&tuio, &iov, target, (afs_offs_t) 0, maxlen, UIO_READ, seg);
    code = afs_readlink(ITOAFS(ip), &tuio, credp);
    crfree(credp);

    if (!code)
	return maxlen - tuio.uio_resid;
    else
	return -code;
}

#if !defined(AFS_LINUX24_ENV)
/* afs_linux_readlink 
 * Fill target (which is in user space) with contents of symlink.
 */
int
afs_linux_readlink(struct dentry *dp, char *target, int maxlen)
{
    int code;
    struct inode *ip = dp->d_inode;

    AFS_GLOCK();
    code = afs_linux_ireadlink(ip, target, maxlen, AFS_UIOUSER);
    AFS_GUNLOCK();
    return code;
}


/* afs_linux_follow_link
 * a file system dependent link following routine.
 */
struct dentry *
afs_linux_follow_link(struct dentry *dp, struct dentry *basep,
		      unsigned int follow)
{
    int code = 0;
    char *name;
    struct dentry *res;


    AFS_GLOCK();
    name = osi_Alloc(PATH_MAX + 1);
    if (!name) {
	AFS_GUNLOCK();
	dput(basep);
	return ERR_PTR(-EIO);
    }

    code = afs_linux_ireadlink(dp->d_inode, name, PATH_MAX, AFS_UIOSYS);
    AFS_GUNLOCK();

    if (code < 0) {
	dput(basep);
	res = ERR_PTR(code);
    } else {
	name[code] = '\0';
	res = lookup_dentry(name, basep, follow);
    }

    AFS_GLOCK();
    osi_Free(name, PATH_MAX + 1);
    AFS_GUNLOCK();
    return res;
}
#endif

/* afs_linux_readpage
 * all reads come through here. A strategy-like read call.
 */
int
afs_linux_readpage(struct file *fp, struct page *pp)
{
    int code;
    cred_t *credp = crref();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    char *address;
    afs_offs_t offset = pp->index << PAGE_CACHE_SHIFT;
#else
    ulong address = afs_linux_page_address(pp);
    afs_offs_t offset = pageoff(pp);
#endif
    uio_t tuio;
    struct iovec iovec;
    struct inode *ip = FILE_INODE(fp);
    int cnt = page_count(pp);
    struct vcache *avc = ITOAFS(ip);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    address = kmap(pp);
    ClearPageError(pp);
#else
    atomic_add(1, &pp->count);
    set_bit(PG_locked, &pp->flags);	/* other bits? See mm.h */
    clear_bit(PG_error, &pp->flags);
#endif

    setup_uio(&tuio, &iovec, (char *)address, offset, PAGESIZE, UIO_READ,
	      AFS_UIOSYS);
#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_READPAGE, ICL_TYPE_POINTER, ip, ICL_TYPE_POINTER, pp, ICL_TYPE_INT32, cnt, ICL_TYPE_INT32, 99999);	/* not a possible code value */
    code = afs_rdwr(avc, &tuio, UIO_READ, 0, credp);
    afs_Trace4(afs_iclSetp, CM_TRACE_READPAGE, ICL_TYPE_POINTER, ip,
	       ICL_TYPE_POINTER, pp, ICL_TYPE_INT32, cnt, ICL_TYPE_INT32,
	       code);
    AFS_GUNLOCK();
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif

    if (!code) {
	if (tuio.uio_resid)	/* zero remainder of page */
	    memset((void *)(address + (PAGESIZE - tuio.uio_resid)), 0,
		   tuio.uio_resid);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	flush_dcache_page(pp);
	SetPageUptodate(pp);
#else
	set_bit(PG_uptodate, &pp->flags);
#endif
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    kunmap(pp);
    UnlockPage(pp);
#else
    clear_bit(PG_locked, &pp->flags);
    wake_up(&pp->wait);
    free_page(address);
#endif

    if (!code && AFS_CHUNKOFFSET(offset) == 0) {
	struct dcache *tdc;
	struct vrequest treq;

	AFS_GLOCK();
	code = afs_InitReq(&treq, credp);
	if (!code && !NBObtainWriteLock(&avc->lock, 534)) {
	    tdc = afs_FindDCache(avc, offset);
	    if (tdc) {
		if (!(tdc->mflags & DFNextStarted))
		    afs_PrefetchChunk(avc, tdc, credp, &treq);
		afs_PutDCache(tdc);
	    }
	    ReleaseWriteLock(&avc->lock);
	}
	AFS_GUNLOCK();
    }

    crfree(credp);
    return -code;
}

#if defined(AFS_LINUX24_ENV)
int
afs_linux_writepage(struct page *pp)
{
    struct address_space *mapping = pp->mapping;
    struct inode *inode;
    unsigned long end_index;
    unsigned offset = PAGE_CACHE_SIZE;
    long status;

    inode = (struct inode *)mapping->host;
    end_index = inode->i_size >> PAGE_CACHE_SHIFT;

    /* easy case */
    if (pp->index < end_index)
	goto do_it;
    /* things got complicated... */
    offset = inode->i_size & (PAGE_CACHE_SIZE - 1);
    /* OK, are we completely out? */
    if (pp->index >= end_index + 1 || !offset)
	return -EIO;
  do_it:
    AFS_GLOCK();
    status = afs_linux_writepage_sync(inode, pp, 0, offset);
    AFS_GUNLOCK();
    SetPageUptodate(pp);
    UnlockPage(pp);
    if (status == offset)
	return 0;
    else
	return status;
}
#endif

/* afs_linux_permission
 * Check access rights - returns error if can't check or permission denied.
 */
int
afs_linux_permission(struct inode *ip, int mode)
{
    int code;
    cred_t *credp = crref();
    int tmp = 0;

    AFS_GLOCK();
    if (mode & MAY_EXEC)
	tmp |= VEXEC;
    if (mode & MAY_READ)
	tmp |= VREAD;
    if (mode & MAY_WRITE)
	tmp |= VWRITE;
    code = afs_access(ITOAFS(ip), tmp, credp);

    AFS_GUNLOCK();
    crfree(credp);
    return -code;
}


#if defined(AFS_LINUX24_ENV)
int
afs_linux_writepage_sync(struct inode *ip, struct page *pp,
			 unsigned long offset, unsigned int count)
{
    struct vcache *vcp = ITOAFS(ip);
    char *buffer;
    afs_offs_t base;
    int code = 0;
    cred_t *credp;
    uio_t tuio;
    struct iovec iovec;
    int f_flags = 0;

    buffer = kmap(pp) + offset;
    base = (pp->index << PAGE_CACHE_SHIFT) + offset;

    credp = crref();
    afs_Trace4(afs_iclSetp, CM_TRACE_UPDATEPAGE, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, pp, ICL_TYPE_INT32, page_count(pp),
	       ICL_TYPE_INT32, 99999);

    setup_uio(&tuio, &iovec, buffer, base, count, UIO_WRITE, AFS_UIOSYS);

    code = afs_write(vcp, &tuio, f_flags, credp, 0);

    vcache2inode(vcp);

    if (!code
	&& afs_stats_cmperf.cacheCurrDirtyChunks >
	afs_stats_cmperf.cacheMaxDirtyChunks) {
	struct vrequest treq;

	ObtainWriteLock(&vcp->lock, 533);
	if (!afs_InitReq(&treq, credp))
	    code = afs_DoPartialWrite(vcp, &treq);
	ReleaseWriteLock(&vcp->lock);
    }
    code = code ? -code : count - tuio.uio_resid;

    afs_Trace4(afs_iclSetp, CM_TRACE_UPDATEPAGE, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, pp, ICL_TYPE_INT32, page_count(pp),
	       ICL_TYPE_INT32, code);

    crfree(credp);
    kunmap(pp);

    return code;
}

static int
afs_linux_updatepage(struct file *file, struct page *page,
		     unsigned long offset, unsigned int count)
{
    struct dentry *dentry = file->f_dentry;

    return afs_linux_writepage_sync(dentry->d_inode, page, offset, count);
}
#else
/* afs_linux_updatepage
 * What one would have thought was writepage - write dirty page to file.
 * Called from generic_file_write. buffer is still in user space. pagep
 * has been filled in with old data if we're updating less than a page.
 */
int
afs_linux_updatepage(struct file *fp, struct page *pp, unsigned long offset,
		     unsigned int count, int sync)
{
    struct vcache *vcp = ITOAFS(FILE_INODE(fp));
    u8 *page_addr = (u8 *) afs_linux_page_address(pp);
    int code = 0;
    cred_t *credp;
    uio_t tuio;
    struct iovec iovec;

    set_bit(PG_locked, &pp->flags);

    credp = crref();
    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_UPDATEPAGE, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, pp, ICL_TYPE_INT32, page_count(pp),
	       ICL_TYPE_INT32, 99999);
    setup_uio(&tuio, &iovec, page_addr + offset,
	      (afs_offs_t) (pageoff(pp) + offset), count, UIO_WRITE,
	      AFS_UIOSYS);

    code = afs_write(vcp, &tuio, fp->f_flags, credp, 0);

    vcache2inode(vcp);

    code = code ? -code : count - tuio.uio_resid;
    afs_Trace4(afs_iclSetp, CM_TRACE_UPDATEPAGE, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, pp, ICL_TYPE_INT32, page_count(pp),
	       ICL_TYPE_INT32, code);

    AFS_GUNLOCK();
    crfree(credp);

    clear_bit(PG_locked, &pp->flags);
    return code;
}
#endif

#if defined(AFS_LINUX24_ENV)
static int
afs_linux_commit_write(struct file *file, struct page *page, unsigned offset,
		       unsigned to)
{
    int code;

    lock_kernel();
    AFS_GLOCK();
    code = afs_linux_updatepage(file, page, offset, to - offset);
    AFS_GUNLOCK();
    unlock_kernel();
    kunmap(page);

    return code;
}

static int
afs_linux_prepare_write(struct file *file, struct page *page, unsigned from,
			unsigned to)
{
    kmap(page);
    return 0;
}

extern int afs_notify_change(struct dentry *dp, struct iattr *iattrp);
#endif

struct inode_operations afs_file_iops = {
#if defined(AFS_LINUX26_ENV)
  .permission =		afs_linux_permission,
  .getattr =		afs_linux_getattr,
  .setattr =		afs_notify_change,
#elif defined(AFS_LINUX24_ENV)
  .permission =		afs_linux_permission,
  .revalidate =		afs_linux_revalidate,
  .setattr =		afs_notify_change,
#else
  .default_file_ops =	&afs_file_fops,
  .readpage =		afs_linux_readpage,
  .revalidate =		afs_linux_revalidate,
  .updatepage =		afs_linux_updatepage,
#endif
};

#if defined(AFS_LINUX24_ENV)
struct address_space_operations afs_file_aops = {
  .readpage =		afs_linux_readpage,
  .writepage =		afs_linux_writepage,
  .commit_write =	afs_linux_commit_write,
  .prepare_write =	afs_linux_prepare_write,
};
#endif


/* Separate ops vector for directories. Linux 2.2 tests type of inode
 * by what sort of operation is allowed.....
 */

struct inode_operations afs_dir_iops = {
#if !defined(AFS_LINUX24_ENV)
  .default_file_ops =	&afs_dir_fops,
#else
  .setattr =		afs_notify_change,
#endif
  .create =		afs_linux_create,
  .lookup =		afs_linux_lookup,
  .link =		afs_linux_link,
  .unlink =		afs_linux_unlink,
  .symlink =		afs_linux_symlink,
  .mkdir =		afs_linux_mkdir,
  .rmdir =		afs_linux_rmdir,
  .rename =		afs_linux_rename,
#if defined(AFS_LINUX26_ENV)
  .getattr =		afs_linux_getattr,
#else
  .revalidate =		afs_linux_revalidate,
#endif
  .permission =		afs_linux_permission,
};

/* We really need a separate symlink set of ops, since do_follow_link()
 * determines if it _is_ a link by checking if the follow_link op is set.
 */
#if defined(AFS_LINUX24_ENV)
static int
afs_symlink_filler(struct file *file, struct page *page)
{
    struct inode *ip = (struct inode *)page->mapping->host;
    char *p = (char *)kmap(page);
    int code;

    lock_kernel();
    AFS_GLOCK();
    code = afs_linux_ireadlink(ip, p, PAGE_SIZE, AFS_UIOSYS);
    AFS_GUNLOCK();

    if (code < 0)
	goto fail;
    p[code] = '\0';		/* null terminate? */
    unlock_kernel();

    SetPageUptodate(page);
    kunmap(page);
    UnlockPage(page);
    return 0;

  fail:
    unlock_kernel();

    SetPageError(page);
    kunmap(page);
    UnlockPage(page);
    return code;
}

struct address_space_operations afs_symlink_aops = {
  .readpage =	afs_symlink_filler
};
#endif

struct inode_operations afs_symlink_iops = {
#if defined(AFS_LINUX24_ENV)
  .readlink = 		page_readlink,
  .follow_link =	page_follow_link,
  .setattr =		afs_notify_change,
#else
  .readlink = 		afs_linux_readlink,
  .follow_link =	afs_linux_follow_link,
  .permission =		afs_linux_permission,
  .revalidate =		afs_linux_revalidate,
#endif
};
