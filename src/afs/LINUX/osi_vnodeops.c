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
 * AFS vnodeops. The "NOTUSED" #define is used to indicate routines and
 * calling sequences present in an ops table that we don't actually use.
 * They are present solely for documentation purposes.
 *
 * So far the only truly scary part is that Linux relies on the inode cache
 * to be up to date. Don't you dare break a callback and expect an fstat
 * to give you meaningful information. This appears to be fixed in the 2.1
 * development kernels. As it is we can fix this now by intercepting the 
 * stat calls.
 */

#include <afsconfig.h>
#include "../afs/param.h"

RCSID("$Header: /cvs/openafs/src/afs/LINUX/osi_vnodeops.c,v 1.43 2002/06/30 06:39:12 shadow Exp $");

#include "../afs/sysincludes.h"
#include "../afs/afsincludes.h"
#include "../afs/afs_stats.h"
#include "../afs/afs_osidnlc.h"
#include "../h/mm.h"
#include "../h/pagemap.h"
#if defined(AFS_LINUX24_ENV)
#include "../h/smp_lock.h"
#endif

#ifdef pgoff2loff
#define pageoff(pp) pgoff2loff((pp)->index)
#else
#define pageoff(pp) pp->offset
#endif

extern struct vcache *afs_globalVp;

extern struct dentry_operations *afs_dops;
#if defined(AFS_LINUX24_ENV)
extern struct inode_operations afs_file_iops;
extern struct address_space_operations afs_file_aops;
struct address_space_operations afs_symlink_aops;
#endif
extern struct inode_operations afs_dir_iops;
extern struct inode_operations afs_symlink_iops;


#ifdef NOTUSED
static int afs_linux_lseek(struct inode *ip, struct file *fp, off_t, int) {}
#endif

static ssize_t afs_linux_read(struct file *fp, char *buf, size_t count,
			      loff_t *offp)
{
    ssize_t code;
    struct vcache *vcp = ITOAFS(fp->f_dentry->d_inode);
    cred_t *credp = crref();
    struct vrequest treq;

    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_READOP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_OFFSET, offp,
	       ICL_TYPE_INT32, count,
	       ICL_TYPE_INT32, 99999);

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
		osi_FlushPages(vcp, credp); /* ensure stale pages are gone */
		AFS_GUNLOCK();
		code = generic_file_read(fp, buf, tcount, offp);
		AFS_GLOCK();
		if (code != tcount) {
		    goto done;
		}
		xfered = tcount;
	    } 
            setup_uio(&tuio, &iov, buf + xfered, (afs_offs_t) *offp, count, 
						UIO_READ, AFS_UIOSYS);
            code = afs_read(vcp, &tuio, credp, 0, 0, 0);
	    xfered += count - tuio.uio_resid;
	    if (code != 0) {
		code = xfered;
		*offp += count - tuio.uio_resid;
	    } else {
		code = xfered;
		*offp += count;
	    }
done:
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
	       ICL_TYPE_OFFSET, offp,
	       ICL_TYPE_INT32, count,
	       ICL_TYPE_INT32, code);

    AFS_GUNLOCK();
    crfree(credp);
    return code;
}


/* Now we have integrated VM for writes as well as reads. generic_file_write
 * also takes care of re-positioning the pointer if file is open in append
 * mode. Call fake open/close to ensure we do writes of core dumps.
 */
static ssize_t afs_linux_write(struct file *fp, const char *buf, size_t count,
			   loff_t *offp)
{
    ssize_t code = 0;
    int code2;
    struct vcache *vcp = ITOAFS(fp->f_dentry->d_inode);
    struct vrequest treq;
    cred_t *credp = crref();

    AFS_GLOCK();

    afs_Trace4(afs_iclSetp, CM_TRACE_WRITEOP, ICL_TYPE_POINTER, vcp,
	        ICL_TYPE_OFFSET, offp, 
		ICL_TYPE_INT32, count,
	        ICL_TYPE_INT32, (fp->f_flags & O_APPEND) ? 99998 : 99999);


    /* get a validated vcache entry */
    code = (ssize_t)afs_InitReq(&treq, credp);
    if (!code)
	code = (ssize_t)afs_VerifyVCache(vcp, &treq);

    ObtainWriteLock(&vcp->lock, 529);
    afs_FakeOpen(vcp);
    ReleaseWriteLock(&vcp->lock);
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
		AFS_GUNLOCK();
		code = generic_file_write(fp, buf, tcount, offp);
		AFS_GLOCK();
		if (code != tcount) {
		    goto done;
		}
		xfered = tcount;
	    } 
            setup_uio(&tuio, &iov, buf + xfered, (afs_offs_t) *offp, count, 
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
        	if (AFS_CHUNKBASE(tuio.afsio_offset) != AFS_CHUNKBASE(oldOffset)) {
            	    ObtainWriteLock(&vcp->lock,402);
            	    code = afs_DoPartialWrite(vcp, &treq);
            	    vcp->states |= CDirty;
            	    ReleaseWriteLock(&vcp->lock);
        	}
		code = xfered;
		*offp += count;
		ObtainWriteLock(&vcp->lock,400);
        	vcp->m.Date = osi_Time();       /* Set file date (for ranlib) */
        	/* extend file */
        	if (*offp > vcp->m.Length) {
            	    vcp->m.Length = *offp;
        	}
        	ReleaseWriteLock(&vcp->lock);
	    }
done:
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
    vcp->m.Date = osi_Time(); /* set modification time */
    afs_FakeClose(vcp, credp);
    if (code>=0)
        code2 = afs_DoPartialWrite(vcp, &treq);
    if (code2 && code >=0)
        code = (ssize_t) -code2;
    ReleaseWriteLock(&vcp->lock);
	
    afs_Trace4(afs_iclSetp, CM_TRACE_WRITEOP, ICL_TYPE_POINTER, vcp,
	       	ICL_TYPE_OFFSET, offp, 
		ICL_TYPE_INT32, count,
	       	ICL_TYPE_INT32, code);

    AFS_GUNLOCK();
    crfree(credp);
    return code;
}

/* This is a complete rewrite of afs_readdir, since we can make use of
 * filldir instead of afs_readdir_move. Note that changes to vcache/dcache
 * handling and use of bulkstats will need to be reflected here as well.
 */
static int afs_linux_readdir(struct file *fp,
			     void *dirbuf, filldir_t filldir)
{
    extern struct DirEntry * afs_dir_GetBlob();
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

    AFS_GLOCK();
    AFS_STATCNT(afs_readdir);

    code = afs_InitReq(&treq, credp);
    crfree(credp);
    if (code) {
	AFS_GUNLOCK();
	return -code;
    }

    afs_InitFakeStat(&fakestat);
    code = afs_EvalFakeStat(&avc, &fakestat, &treq);
    if (code) {
	afs_PutFakeStat(&fakestat);
	AFS_GUNLOCK();
	return -code;
    }

    /* update the cache entry */
tagain:
    code = afs_VerifyVCache(avc, &treq);
    if (code) {
	afs_PutFakeStat(&fakestat);
	AFS_GUNLOCK();
	return -code;
    }

    /* get a reference to the entire directory */
    tdc = afs_GetDCache(avc, (afs_size_t) 0, &treq, &origOffset, &tlen, 1);
    len = tlen;
    if (!tdc) {
	afs_PutFakeStat(&fakestat);
	AFS_GUNLOCK();
	return -ENOENT;
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
    offset = (int)fp->f_pos;
    while(1) { 
	dirpos = BlobScan(&tdc->f.inode, offset);
	if (!dirpos)
	    break;

	de = (struct DirEntry*)afs_dir_GetBlob(&tdc->f.inode, dirpos);
	if (!de)
	    break;

	ino = (avc->fid.Fid.Volume << 16) + ntohl(de->fid.vnode);
	ino &= 0x7fffffff; /* Assumes 32 bit ino_t ..... */
	len = strlen(de->name);

	/* filldir returns -EINVAL when the buffer is full. */
#if (defined(AFS_LINUX24_ENV) || defined(pgoff2loff)) && defined(DECLARE_FSTYPE)
        {
             unsigned int type=DT_UNKNOWN;
             struct VenusFid afid;
             struct vcache *tvc;
             int vtype;
             afid.Cell=avc->fid.Cell;
             afid.Fid.Volume=avc->fid.Fid.Volume;
             afid.Fid.Vnode=ntohl(de->fid.vnode);
             afid.Fid.Unique=ntohl(de->fid.vunique);
             if ((avc->states & CForeign) == 0 &&
                 (ntohl(de->fid.vnode) & 1)) {
		 type=DT_DIR;
             } else if ((tvc=afs_FindVCache(&afid,0,0,0,0))) {
                  if (tvc->mvstat) {
                       type=DT_DIR;
                  } else if (((tvc->states) & (CStatd|CTruth))) {
                       /* CTruth will be set if the object has
                        *ever* been statd */
                       vtype=vType(tvc);
                       if (vtype == VDIR)
                            type=DT_DIR;
                       else if (vtype == VREG)
                            type=DT_REG;
                       /* Don't do this until we're sure it can't be a mtpt */
                       /* else if (vtype == VLNK)
                          type=DT_LNK; */
                       /* what other types does AFS support? */
                  }
                  /* clean up from afs_FindVCache */
                  afs_PutVCache(tvc, WRITE_LOCK);
             }
             code = (*filldir)(dirbuf, de->name, len, offset, ino, type);
        }
#else
        code = (*filldir)(dirbuf, de->name, len, offset, ino);
#endif
	DRelease(de, 0);
	if (code)
	    break;
	offset = dirpos + 1 + ((len+16)>>5);
    }
    /* If filldir didn't fill in the last one this is still pointing to that
     * last attempt.
     */
    fp->f_pos = (loff_t)offset;

    ReleaseReadLock(&tdc->lock);
    afs_PutDCache(tdc);
    ReleaseReadLock(&avc->lock);
    afs_PutFakeStat(&fakestat);
    AFS_GUNLOCK();
    return 0;
}

#ifdef NOTUSED
int afs_linux_select(struct inode *ip, struct file *fp, int, select_table *);
#endif

/* in afs_pioctl.c */
extern int afs_xioctl(struct inode *ip, struct file *fp,
			  unsigned int com, unsigned long arg);


/* We need to detect unmap's after close. To do that, we need our own
 * vm_operations_struct's. And we need to set them up for both the
 * private and shared mappings. The fun part is that these are all static
 * so we'll have to initialize on the fly!
 */
static struct vm_operations_struct afs_private_mmap_ops;
static int afs_private_mmap_ops_inited = 0;
static struct vm_operations_struct afs_shared_mmap_ops;
static int afs_shared_mmap_ops_inited = 0;

void afs_linux_vma_close(struct vm_area_struct *vmap)
{
    struct vcache *vcp;
    cred_t *credp;

    if (!vmap->vm_file)
	return;

    vcp = ITOAFS(FILE_INODE(vmap->vm_file));
    if (!vcp)
	return;

    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_VM_CLOSE,
	       ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_INT32, vcp->mapcnt,
	       ICL_TYPE_INT32, vcp->opens,
	       ICL_TYPE_INT32, vcp->execsOrWriters);
    ObtainWriteLock(&vcp->lock, 532);
    if (vcp->mapcnt) {
	vcp->mapcnt--;
	ReleaseWriteLock(&vcp->lock);
	if (!vcp->mapcnt) {
	    credp = crref();
	    (void) afs_close(vcp, vmap->vm_file->f_flags, credp);
	    /* only decrement the execsOrWriters flag if this is not a writable
	     * file. */
	    if (! (vmap->vm_file->f_flags & (FWRITE | FTRUNC)))
		vcp->execsOrWriters--;

	    vcp->states &= ~CMAPPED;
	    crfree(credp);
	}
    }
    else {
	ReleaseWriteLock(&vcp->lock);
    }

 unlock_exit:
    AFS_GUNLOCK();
}

static int afs_linux_mmap(struct file *fp, struct vm_area_struct *vmap)
{
    struct vcache *vcp = ITOAFS(FILE_INODE(fp));
    cred_t *credp = crref();
    struct vrequest treq;
    int code;

    AFS_GLOCK();
#if defined(AFS_LINUX24_ENV)
    afs_Trace3(afs_iclSetp, CM_TRACE_GMAP, ICL_TYPE_POINTER, vcp,
               ICL_TYPE_POINTER, vmap->vm_start,
               ICL_TYPE_INT32, vmap->vm_end - vmap->vm_start);
#else
    afs_Trace4(afs_iclSetp, CM_TRACE_GMAP, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, vmap->vm_start,
	       ICL_TYPE_INT32, vmap->vm_end - vmap->vm_start,
	       ICL_TYPE_INT32, vmap->vm_offset);
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
	ObtainWriteLock(&vcp->lock,531);
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
	}
	else {
	    if (!afs_private_mmap_ops_inited) {
		afs_private_mmap_ops_inited = 1;
		afs_private_mmap_ops = *vmap->vm_ops;
		afs_private_mmap_ops.close = afs_linux_vma_close;
	    }
	    vmap->vm_ops = &afs_private_mmap_ops;
	}


	/* Add an open reference on the first mapping. */
	if (vcp->mapcnt == 0) {
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

int afs_linux_open(struct inode *ip, struct file *fp)
{
    int code;
    cred_t *credp = crref();

    AFS_GLOCK();
#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    code = afs_open((struct vcache**)&ip, fp->f_flags, credp);
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif
    AFS_GUNLOCK();

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
static int afs_linux_release(struct inode *ip, struct file *fp)
{
    int code = 0;
    cred_t *credp = crref();
    struct vcache *vcp = ITOAFS(ip);

    AFS_GLOCK();
#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    if (vcp->flushcnt) {
	vcp->flushcnt--; /* protected by AFS global lock. */
    }
    else {
	code = afs_close(vcp, fp->f_flags, credp);
    }
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif
    AFS_GUNLOCK();

    crfree(credp);
    return -code;
}

#if defined(AFS_LINUX24_ENV)
static int afs_linux_fsync(struct file *fp, struct dentry *dp, int datasync)
#else
static int afs_linux_fsync(struct file *fp, struct dentry *dp)
#endif
{
    int code;
    struct inode *ip = FILE_INODE(fp);
    cred_t *credp = crref();

    AFS_GLOCK();
#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif
    code = afs_fsync(ITOAFS(ip), credp);
#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif
    AFS_GUNLOCK();
    crfree(credp);
    return -code;
    
}

#ifdef NOTUSED
/* No support for async i/o */
int afs_linux_fasync(struct inode *ip, struct file *fp, int);

/* I don't think it will, at least not as can be detected here. */
int afs_linux_check_media_change(kdev_t dev);

/* Revalidate media and file system. */
int afs_linux_file_revalidate(kdev_t dev);
#endif /* NOTUSED */

static int afs_linux_lock(struct file *fp, int cmd, struct file_lock *flp)
{
    int code = 0;
    struct vcache *vcp = ITOAFS(FILE_INODE(fp));
    cred_t *credp = crref();
#ifdef AFS_LINUX24_ENV
    struct flock64 flock;
#else
    struct flock flock;
#endif
    
    /* Convert to a lock format afs_lockctl understands. */
    memset((char*)&flock, 0, sizeof(flock));
    flock.l_type = flp->fl_type;
    flock.l_pid = flp->fl_pid;
    flock.l_whence = 0;
    flock.l_start = flp->fl_start;
    flock.l_len = flp->fl_end - flp->fl_start;

    AFS_GLOCK();
    code = afs_lockctl(vcp, &flock, cmd, credp);
    AFS_GUNLOCK();
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
int afs_linux_flush(struct file *fp)
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
    vcp->flushcnt++; /* protected by AFS global lock. */
    AFS_GUNLOCK();

    crfree(credp);
    return -code;
}

/* Not allowed to directly read a directory. */
ssize_t afs_linux_dir_read(struct file *fp, char *buf, size_t count, loff_t *ppos)
{
    return -EISDIR;
}



#if defined(AFS_LINUX24_ENV)
struct file_operations afs_dir_fops = {
    read:      generic_read_dir,
    readdir:   afs_linux_readdir,
    ioctl:     afs_xioctl,
    open:      afs_linux_open,
    release:   afs_linux_release,
};
#else
struct file_operations afs_dir_fops = {
    NULL,		/* afs_linux_lseek */
    afs_linux_dir_read,
    NULL,		/* afs_linux_write */
    afs_linux_readdir,
    NULL,		/* afs_linux_select */
    afs_xioctl,		/* close enough to use the ported AFS one */
    NULL,		/* afs_linux_mmap */
    afs_linux_open,
    NULL,		/* afs_linux_flush */
    afs_linux_release,
    afs_linux_fsync,
    NULL,		/* afs_linux_fasync */
    NULL,		/* afs_linux_check_media_change */
    NULL,		/* afs_linux_file_revalidate */
    afs_linux_lock,
};
#endif

#if defined(AFS_LINUX24_ENV)
struct file_operations afs_file_fops = {
    read:      afs_linux_read,
    write:     afs_linux_write,
    ioctl:     afs_xioctl,
    mmap:      afs_linux_mmap,
    open:      afs_linux_open,
    flush:     afs_linux_flush,
    release:   afs_linux_release,
    fsync:     afs_linux_fsync,
    lock:      afs_linux_lock,
};
#else
struct file_operations afs_file_fops = {
    NULL,		/* afs_linux_lseek */
    afs_linux_read,
    afs_linux_write,
    NULL,		/* afs_linux_readdir */
    NULL,		/* afs_linux_select */
    afs_xioctl,		/* close enough to use the ported AFS one */
    afs_linux_mmap,
    afs_linux_open,
    afs_linux_flush,
    afs_linux_release,
    afs_linux_fsync,
    NULL,		/* afs_linux_fasync */
    NULL,		/* afs_linux_check_media_change */
    NULL,		/* afs_linux_file_revalidate */
    afs_linux_lock,
};
#endif
   

/**********************************************************************
 * AFS Linux dentry operations
 **********************************************************************/

/* afs_linux_revalidate
 * Ensure vcache is stat'd before use. Return 0 if entry is valid.
 */
static int afs_linux_revalidate(struct dentry *dp)
{
    int code;
    cred_t *credp;
    struct vrequest treq;
    struct vcache *vcp = ITOAFS(dp->d_inode);

    AFS_GLOCK();
#ifdef AFS_LINUX24_ENV
    lock_kernel();
#endif

    /* Make this a fast path (no crref), since it's called so often. */
    if (vcp->states & CStatd) {
        if (*dp->d_name.name != '/' && vcp->mvstat == 2) /* root vnode */
	    check_bad_parent(dp); /* check and correct mvid */
	vcache2inode(vcp);
#ifdef AFS_LINUX24_ENV
	unlock_kernel();
#endif
	AFS_GUNLOCK();
	return 0;
    }

    credp = crref();
    code = afs_InitReq(&treq, credp);
    if (!code)
	code = afs_VerifyVCache(vcp, &treq);

#ifdef AFS_LINUX24_ENV
    unlock_kernel();
#endif
    AFS_GUNLOCK();
    crfree(credp);

    return -code ;
}


/* Validate a dentry. Return 1 if unchanged, 0 if VFS layer should re-evaluate.
 * In kernels 2.2.10 and above, we are passed an additional flags var which
 * may have either the LOOKUP_FOLLOW OR LOOKUP_DIRECTORY set in which case
 * we are advised to follow the entry if it is a link or to make sure that 
 * it is a directory. But since the kernel itself checks these possibilities
 * later on, we shouldn't have to do it until later. Perhaps in the future..
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,10)
static int afs_linux_dentry_revalidate(struct dentry *dp, int flags)
#else
static int afs_linux_dentry_revalidate(struct dentry *dp)
#endif
{
    char *name;
    cred_t *credp = crref();
    struct vrequest treq;
    struct vcache *lookupvcp = NULL;
    int code, bad_dentry = 1;
    struct sysname_info sysState;
    struct vcache *vcp = ITOAFS(dp->d_inode);
    struct vcache *parentvcp = ITOAFS(dp->d_parent->d_inode);

    AFS_GLOCK();

    sysState.allocked = 0;

    /* If it's a negative dentry, then there's nothing to do. */
    if (!vcp || !parentvcp)
        goto done;

    /* If it is the AFS root, then there's no chance it needs 
       revalidating */
    if (vcp == afs_globalVp) {
	bad_dentry = 0;
	goto done;
    }

    if (code = afs_InitReq(&treq, credp))
        goto done;

    Check_AtSys(parentvcp, dp->d_name.name, &sysState, &treq);
    name = sysState.name;

    /* First try looking up the DNLC */
    if (lookupvcp = osi_dnlc_lookup(parentvcp, name, WRITE_LOCK)) {
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

    /* A DNLC lookup failure cannot be trusted. Try a real lookup */
    code = afs_lookup(parentvcp, name, &lookupvcp, credp);

    /* Verify that the dentry does not point to an old inode */
    if (vcp != lookupvcp)
        goto done;

    bad_dentry = 0;

done:
    /* Clean up */
    if (lookupvcp)
        afs_PutVCache(lookupvcp, WRITE_LOCK);
    if (sysState.allocked)
        osi_FreeLargeSpace(name);

    AFS_GUNLOCK();
    crfree(credp);

    if (bad_dentry) {
        shrink_dcache_parent(dp);
        d_drop(dp);
    }

    return !bad_dentry;
}

#ifdef notdef
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,10)
static int afs_linux_dentry_revalidate(struct dentry *dp, int flags)
#else
static int afs_linux_dentry_revalidate(struct dentry *dp)
#endif
{
    int code;
    cred_t *credp;
    struct vrequest treq;
    struct inode *ip = AFSTOI(dp->d_inode);

    unsigned long timeout = 3*HZ; /* 3 seconds */

if (!ip)
	printk("negative dentry: %s\n", dp->d_name.name);

    if (!(flags & LOOKUP_CONTINUE)) {
	long diff = CURRENT_TIME - dp->d_parent->d_inode->i_mtime;

	if (diff < 15*60)
	    timeout = 0;
    }

    if (time_after(jiffies, dp->d_time + timeout))
	goto out_bad;

 out_valid:
    return 1;

 out_bad:
    return 0;
}
#endif

/* afs_dentry_iput */
static void afs_dentry_iput(struct dentry *dp, struct inode *ip)
{
    osi_iput(ip);
}

#if defined(AFS_LINUX24_ENV)
struct dentry_operations afs_dentry_operations = {
       d_revalidate:   afs_linux_dentry_revalidate,
       d_iput:         afs_dentry_iput,
};
struct dentry_operations *afs_dops = &afs_dentry_operations;
#else
struct dentry_operations afs_dentry_operations = {
	afs_linux_dentry_revalidate,	/* d_validate(struct dentry *) */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	NULL,			/* d_delete(struct dentry *) */
	NULL,			/* d_release(struct dentry *) */
	afs_dentry_iput		/* d_iput(struct dentry *, struct inode *) */
};
struct dentry_operations *afs_dops = &afs_dentry_operations;
#endif

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
int afs_linux_create(struct inode *dip, struct dentry *dp, int mode)
{
    int code;
    cred_t *credp = crref();
    struct vattr vattr;
    enum vcexcl excl;
    const char *name = dp->d_name.name;
    struct inode *ip;

    VATTR_NULL(&vattr);
    vattr.va_mode = mode;

    AFS_GLOCK();
    code = afs_create(ITOAFS(dip), name, &vattr, NONEXCL, mode,
		      (struct vcache**)&ip, credp);

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

	dp->d_op = afs_dops;
	dp->d_time = jiffies;
	d_instantiate(dp, ip);
    }

    AFS_GUNLOCK();
    crfree(credp);
    return -code;
}

/* afs_linux_lookup */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,10)
struct dentry *afs_linux_lookup(struct inode *dip, struct dentry *dp)
#else
int afs_linux_lookup(struct inode *dip, struct dentry *dp)
#endif
{
    int code = 0;
    cred_t *credp = crref();
    struct vcache *vcp=NULL;
    const char *comp = dp->d_name.name;
    AFS_GLOCK();
    code = afs_lookup(ITOAFS(dip), comp, &vcp, credp);

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
           printk("afs_linux_lookup: ip->i_mode 0x%x  dp->d_name.name %s  code %d\n", ip->i_mode, dp->d_name.name, code);
#else
	if (S_ISDIR(ip->i_mode))
	    ip->i_op = &afs_dir_iops;
	else if (S_ISLNK(ip->i_mode))
	    ip->i_op = &afs_symlink_iops;
#endif
    } 
    dp->d_time = jiffies;
    dp->d_op = afs_dops;
    d_add(dp, AFSTOI(vcp));

    AFS_GUNLOCK();
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

int afs_linux_link(struct dentry *olddp, struct inode *dip,
		   struct dentry *newdp)
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

int afs_linux_unlink(struct inode *dip, struct dentry *dp)
{
    int code;
    cred_t *credp = crref();
    const char *name = dp->d_name.name;
    int putback = 0;

    AFS_GLOCK();
    code = afs_remove(ITOAFS(dip), name, credp);
    AFS_GUNLOCK();
    if (!code)
	d_drop(dp);
    crfree(credp);
    return -code;
}


int afs_linux_symlink(struct inode *dip, struct dentry *dp,
		      const char *target)
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

int afs_linux_mkdir(struct inode *dip, struct dentry *dp, int mode)
{
    int code;
    cred_t *credp = crref();
    struct vcache *tvcp = NULL;
    struct vattr vattr;
    const char *name = dp->d_name.name;

    AFS_GLOCK();
    VATTR_NULL(&vattr);
    vattr.va_mask = ATTR_MODE;
    vattr.va_mode = mode;
    code = afs_mkdir(ITOAFS(dip), name, &vattr, &tvcp, credp);

    if (tvcp) {
	tvcp->v.v_op = &afs_dir_iops;
#if defined(AFS_LINUX24_ENV)
	tvcp->v.v_fop = &afs_dir_fops;
#endif
	dp->d_op = afs_dops;
	dp->d_time = jiffies;
	d_instantiate(dp, AFSTOI(tvcp));
    }

    AFS_GUNLOCK();
    crfree(credp);
    return -code;
}

int afs_linux_rmdir(struct inode *dip, struct dentry *dp)
{
    int code;
    cred_t *credp = crref();
    const char *name = dp->d_name.name;

    AFS_GLOCK();
    code = afs_rmdir(ITOAFS(dip), name, credp);

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

    AFS_GUNLOCK();
    crfree(credp);
    return -code;
}



int afs_linux_rename(struct inode *oldip, struct dentry *olddp,
		     struct inode *newip, struct dentry *newdp)
{
    int code;
    cred_t *credp = crref();
    const char *oldname = olddp->d_name.name;
    const char *newname = newdp->d_name.name;

    /* Remove old and new entries from name hash. New one will change below.
     * While it's optimal to catch failures and re-insert newdp into hash,
     * it's also error prone and in that case we're already dealing with error
     * cases. Let another lookup put things right, if need be.
     */
    if (!list_empty(&olddp->d_hash)) {
	d_drop(olddp);
    }
    if (!list_empty(&newdp->d_hash)) {
	d_drop(newdp);
    }
    AFS_GLOCK();
    code = afs_rename(ITOAFS(oldip), oldname, ITOAFS(newip),
		      newname, credp);
    AFS_GUNLOCK();

    if (!code) {
	/* update time so it doesn't expire immediately */
	newdp->d_time = jiffies;
	d_move(olddp, newdp);
    }

    crfree(credp);
    return -code;
}


/* afs_linux_ireadlink 
 * Internal readlink which can return link contents to user or kernel space.
 * Note that the buffer is NOT supposed to be null-terminated.
 */
static int afs_linux_ireadlink(struct inode *ip, char *target, int maxlen,
			uio_seg_t seg)
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
int afs_linux_readlink(struct dentry *dp, char *target, int maxlen)
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
struct dentry * afs_linux_follow_link(struct dentry *dp,
				      struct dentry *basep,
				      unsigned int follow)
{
    int code = 0;
    char *name;
    struct dentry *res;


    AFS_GLOCK();
    name = osi_Alloc(PATH_MAX+1);
    if (!name) {
	AFS_GUNLOCK();
	dput(basep);
	return ERR_PTR(-EIO);
    }

    code = afs_linux_ireadlink(dp->d_inode, name, PATH_MAX, AFS_UIOSYS);
    AFS_GUNLOCK();

    if (code<0) {
	dput(basep);
	res = ERR_PTR(code);
    }
    else {
	name[code] = '\0';
	res = lookup_dentry(name, basep, follow);
    }

    AFS_GLOCK();
    osi_Free(name, PATH_MAX+1);
    AFS_GUNLOCK();
    return res;
}
#endif

/* afs_linux_readpage
 * all reads come through here. A strategy-like read call.
 */
int afs_linux_readpage(struct file *fp, struct page *pp)
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
    int cnt = atomic_read(&pp->count);

    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_READPAGE,
	       ICL_TYPE_POINTER, ip,
	       ICL_TYPE_POINTER, pp,
	       ICL_TYPE_INT32, cnt,
	       ICL_TYPE_INT32, 99999); /* not a possible code value */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    address = kmap(pp);
    ClearPageError(pp);

    lock_kernel();
#else
    atomic_add(1, &pp->count);
    set_bit(PG_locked, &pp->flags); /* other bits? See mm.h */
    clear_bit(PG_error, &pp->flags);
#endif

    setup_uio(&tuio, &iovec, (char*)address, offset, PAGESIZE,
	      UIO_READ, AFS_UIOSYS);
    code = afs_rdwr(ITOAFS(ip), &tuio, UIO_READ, 0, credp);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
    unlock_kernel();
#endif

    if (!code) {
	if (tuio.uio_resid) /* zero remainder of page */
	    memset((void*)(address+(PAGESIZE-tuio.uio_resid)), 0,
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

    crfree(credp);
    afs_Trace4(afs_iclSetp, CM_TRACE_READPAGE,
	       ICL_TYPE_POINTER, ip,
	       ICL_TYPE_POINTER, pp,
	       ICL_TYPE_INT32, cnt,
	       ICL_TYPE_INT32, code);
    AFS_GUNLOCK();
    return -code;
}

#if defined(AFS_LINUX24_ENV)
int afs_linux_writepage(struct page *pp)
{
    struct address_space *mapping = pp->mapping;
    struct inode *inode;
    unsigned long end_index;
    unsigned offset = PAGE_CACHE_SIZE;
    long status;

    inode = (struct inode *) mapping->host;
    end_index = inode->i_size >> PAGE_CACHE_SHIFT;

    /* easy case */
    if (pp->index < end_index)
	goto do_it;
    /* things got complicated... */
    offset = inode->i_size & (PAGE_CACHE_SIZE-1);
    /* OK, are we completely out? */
    if (pp->index >= end_index+1 || !offset)
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

#ifdef NOTUSED
/* afs_linux_bmap - supports generic_readpage, but we roll our own. */
int afs_linux_bmap(struct inode *ip, int) { return -EINVAL; }

/* afs_linux_truncate
 * Handles discarding disk blocks if this were a device. ext2 indicates we
 * may need to zero partial last pages of memory mapped files.
 */
void afs_linux_truncate(struct inode *ip)
{
}
#endif

/* afs_linux_permission
 * Check access rights - returns error if can't check or permission denied.
 */
int afs_linux_permission(struct inode *ip, int mode)
{
    int code;
    cred_t *credp = crref();
    int tmp = 0;

    AFS_GLOCK();
    if (mode & MAY_EXEC) tmp |= VEXEC;
    if (mode & MAY_READ) tmp |= VREAD;
    if (mode & MAY_WRITE) tmp |= VWRITE;
    code = afs_access(ITOAFS(ip), tmp, credp);

    AFS_GUNLOCK();
    crfree(credp);
    return -code;
}


#ifdef NOTUSED
/* msdos sector mapping hack for memory mapping. */
int afs_linux_smap(struct inode *ip, int) { return -EINVAL; }
#endif

#if defined(AFS_LINUX24_ENV)
int afs_linux_writepage_sync(struct inode *ip, struct page *pp,
                        unsigned long offset,
                        unsigned int count)
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
              ICL_TYPE_POINTER, pp,
              ICL_TYPE_INT32, atomic_read(&pp->count),
              ICL_TYPE_INT32, 99999);
    setup_uio(&tuio, &iovec, buffer, base, count, UIO_WRITE, AFS_UIOSYS);

    code = afs_write(vcp, &tuio, f_flags, credp, 0);

    vcache2inode(vcp);

    code = code ? -code : count - tuio.uio_resid;
    afs_Trace4(afs_iclSetp, CM_TRACE_UPDATEPAGE, ICL_TYPE_POINTER, vcp,
              ICL_TYPE_POINTER, pp,
              ICL_TYPE_INT32, atomic_read(&pp->count),
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
int afs_linux_updatepage(struct file *fp, struct page *pp,
			 unsigned long offset,
			 unsigned int count, int sync)
{
    struct vcache *vcp = ITOAFS(FILE_INODE(fp));
    u8 *page_addr = (u8*) afs_linux_page_address(pp);
    int code = 0;
    cred_t *credp;
    uio_t tuio;
    struct iovec iovec;
    
    set_bit(PG_locked, &pp->flags);

    credp = crref();
    AFS_GLOCK();
    afs_Trace4(afs_iclSetp, CM_TRACE_UPDATEPAGE, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, pp,
	       ICL_TYPE_INT32, atomic_read(&pp->count),
	       ICL_TYPE_INT32, 99999);
    setup_uio(&tuio, &iovec, page_addr + offset, (afs_offs_t)(pageoff(pp) + offset),
		count, UIO_WRITE, AFS_UIOSYS);

    code = afs_write(vcp, &tuio, fp->f_flags, credp, 0);

    vcache2inode(vcp);

    code = code ? -code : count - tuio.uio_resid;
    afs_Trace4(afs_iclSetp, CM_TRACE_UPDATEPAGE, ICL_TYPE_POINTER, vcp,
	       ICL_TYPE_POINTER, pp,
	       ICL_TYPE_INT32, atomic_read(&pp->count),
	       ICL_TYPE_INT32, code);

    AFS_GUNLOCK();
    crfree(credp);

    clear_bit(PG_locked, &pp->flags);
    return code;
}
#endif

#if defined(AFS_LINUX24_ENV)
static int afs_linux_commit_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
    int code;

    AFS_GLOCK();
    lock_kernel();
    code = afs_linux_updatepage(file, page, offset, to-offset);
    unlock_kernel();
    AFS_GUNLOCK();
    kunmap(page);

    return code;
}

static int afs_linux_prepare_write(struct file *file, struct page *page,
				   unsigned from, unsigned to)
{
    kmap(page);
    return 0;
}

extern int afs_notify_change(struct dentry *dp, struct iattr* iattrp);
#endif

#if defined(AFS_LINUX24_ENV)
struct inode_operations afs_file_iops = {
    revalidate:                afs_linux_revalidate,
    setattr:           afs_notify_change,
    permission:                afs_linux_permission,
};
struct address_space_operations afs_file_aops = {
        readpage: afs_linux_readpage,
        writepage: afs_linux_writepage,
        commit_write: afs_linux_commit_write,
        prepare_write: afs_linux_prepare_write,
};

struct inode_operations *afs_ops = &afs_file_iops;
#else
struct inode_operations afs_iops = {
    &afs_file_fops,	/* file operations */
    NULL,      		/* afs_linux_create */
    NULL,		/* afs_linux_lookup */
    NULL,		/* afs_linux_link */
    NULL, 		/* afs_linux_unlink */
    NULL,		/* afs_linux_symlink */
    NULL,		/* afs_linux_mkdir */
    NULL,		/* afs_linux_rmdir */
    NULL,		/* afs_linux_mknod */
    NULL,		/* afs_linux_rename */
    NULL,		/* afs_linux_readlink */
    NULL,		/* afs_linux_follow_link */
    afs_linux_readpage,
    NULL,		/* afs_linux_writepage */
    NULL,		/* afs_linux_bmap */
    NULL,		/* afs_linux_truncate */
    afs_linux_permission,
    NULL,		/* afs_linux_smap */
    afs_linux_updatepage,
    afs_linux_revalidate,
};

struct inode_operations *afs_ops = &afs_iops;
#endif

/* Separate ops vector for directories. Linux 2.2 tests type of inode
 * by what sort of operation is allowed.....
 */
#if defined(AFS_LINUX24_ENV)
struct inode_operations afs_dir_iops = {
    create:    afs_linux_create,
    lookup:    afs_linux_lookup,
    link:      afs_linux_link,
    unlink:    afs_linux_unlink,
    symlink:   afs_linux_symlink,
    mkdir:     afs_linux_mkdir,
    rmdir:     afs_linux_rmdir,
    rename:    afs_linux_rename,
    revalidate:        afs_linux_revalidate,
    setattr:   afs_notify_change,
    permission:        afs_linux_permission,
};
#else
struct inode_operations afs_dir_iops = {
    &afs_dir_fops,	/* file operations for directories */
    afs_linux_create,
    afs_linux_lookup,
    afs_linux_link,
    afs_linux_unlink,
    afs_linux_symlink,
    afs_linux_mkdir,
    afs_linux_rmdir,
    NULL,		/* afs_linux_mknod */
    afs_linux_rename,
    NULL,		/* afs_linux_readlink */
    NULL,		/* afs_linux_follow_link */
    NULL,		/* afs_linux_readpage */
    NULL,		/* afs_linux_writepage */
    NULL,		/* afs_linux_bmap */
    NULL,		/* afs_linux_truncate */
    afs_linux_permission,
    NULL,		/* afs_linux_smap */
    NULL,		/* afs_linux_updatepage */
    afs_linux_revalidate,
};
#endif

/* We really need a separate symlink set of ops, since do_follow_link()
 * determines if it _is_ a link by checking if the follow_link op is set.
 */
#if defined(AFS_LINUX24_ENV)
static int afs_symlink_filler(struct file *file, struct page *page)
{
    struct inode *ip = (struct inode *) page->mapping->host;
    char *p = (char *)kmap(page);
    int code;

    AFS_GLOCK();
    lock_kernel();
    code = afs_linux_ireadlink(ip, p, PAGE_SIZE, AFS_UIOSYS);

    if (code<0)
           goto fail;
    p[code] = '\0';            /* null terminate? */
    unlock_kernel();
    AFS_GUNLOCK();

    SetPageUptodate(page);
    kunmap(page);
    UnlockPage(page);
    return 0;

fail:
    unlock_kernel();
    AFS_GUNLOCK();

    SetPageError(page);
    kunmap(page);
    UnlockPage(page);
    return code;
}

struct address_space_operations afs_symlink_aops = {
       readpage:       afs_symlink_filler
};

struct inode_operations afs_symlink_iops = {
    readlink:          page_readlink,
    follow_link:       page_follow_link,
    setattr:           afs_notify_change,
};
#else
struct inode_operations afs_symlink_iops = {
    NULL,		/* file operations */
    NULL,		/* create */
    NULL,		/* lookup */
    NULL,		/* link */
    NULL,		/* unlink */
    NULL,		/* symlink */
    NULL,		/* mkdir */
    NULL,		/* rmdir */
    NULL,		/* afs_linux_mknod */
    NULL,		/* rename */
    afs_linux_readlink,
    afs_linux_follow_link,
    NULL,		/* readpage */
    NULL,		/* afs_linux_writepage */
    NULL,		/* afs_linux_bmap */
    NULL,		/* afs_linux_truncate */
    afs_linux_permission, /* tho the code appears to indicate not used? */
    NULL,		/* afs_linux_smap */
    NULL,		/* updatepage */
    afs_linux_revalidate, /* tho the code appears to indicate not used? */
};
#endif
