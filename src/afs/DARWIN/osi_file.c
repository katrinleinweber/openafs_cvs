/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include "../afs/param.h"       /* Should be always first */
#include "../afs/sysincludes.h" /* Standard vendor system headers */
#include "../afs/afsincludes.h" /* Afs-based standard headers */
#include "../afs/afs_stats.h"  /* afs statistics */
#include "../afs/osi_inode.h"


int afs_osicred_initialized=0;
struct  AFS_UCRED afs_osi_cred;
afs_lock_t afs_xosi;            /* lock is for tvattr */
extern struct osi_dev cacheDev;
extern struct mount *afs_cacheVfsp;
int afs_CacheFSType = -1;

/* Initialize the cache operations. Called while initializing cache files. */
void afs_InitDualFSCacheOps(struct vnode *vp)
{
    int code;
    static int inited = 0;

    if (inited)
        return;
    inited = 1;

    if (vp == NULL)
        return;
    if (strncmp("hfs", vp->v_mount->mnt_vfc->vfc_name, 3) == 0)
       afs_CacheFSType = AFS_APPL_HFS_CACHE;
    else 
    if (strncmp("ufs", vp->v_mount->mnt_vfc->vfc_name, 3) == 0)
       afs_CacheFSType = AFS_APPL_UFS_CACHE;
    else
       osi_Panic("Unknown cache vnode type\n");
}

ino_t VnodeToIno(vnode_t *avp)
{
   unsigned long ret;

   if (afs_CacheFSType == AFS_APPL_UFS_CACHE) {
      struct inode *ip = VTOI(avp);
      ret=ip->i_number;
   }
   if (afs_CacheFSType == AFS_APPL_HFS_CACHE) {
#ifndef VTOH
      struct vattr va;
      if (VOP_GETATTR(avp, &va, &afs_osi_cred, current_proc()))
         osi_Panic("VOP_GETATTR failed in VnodeToIno\n"); 
      ret=va.va_fileid;
#else
      struct hfsnode *hp = VTOH(avp);
      ret=H_FILEID(hp);
#endif
   }
   return ret;
}


dev_t VnodeToDev(vnode_t *avp)
{

    
   if (afs_CacheFSType == AFS_APPL_UFS_CACHE) {
      struct inode *ip = VTOI(avp);
      return ip->i_dev;
   }
   if (afs_CacheFSType == AFS_APPL_HFS_CACHE) {
#ifndef VTOH /* slow, but works */
      struct vattr va;
      if (VOP_GETATTR(avp, &va, &afs_osi_cred, current_proc()))
         osi_Panic("VOP_GETATTR failed in VnodeToDev\n"); 
      return va.va_fsid; /* XXX they say it's the dev.... */
#else
      struct hfsnode *hp = VTOH(avp);
      return H_DEV(hp);
#endif
   }
}

void *osi_UFSOpen(ainode)
    afs_int32 ainode;
{
    struct vnode *vp;
    struct vattr va;
    register struct osi_file *afile = NULL;
    extern int cacheDiskType;
    afs_int32 code = 0;
    int dummy;
    AFS_STATCNT(osi_UFSOpen);
    if(cacheDiskType != AFS_FCACHE_TYPE_UFS) {
	osi_Panic("UFSOpen called for non-UFS cache\n");
    }
    if (!afs_osicred_initialized) {
	/* valid for alpha_osf, SunOS, Ultrix */
	bzero((char *)&afs_osi_cred, sizeof(struct AFS_UCRED));
	afs_osi_cred.cr_ref++;
	afs_osi_cred.cr_ngroups=1;
	afs_osicred_initialized = 1;
    }
    afile = (struct osi_file *) osi_AllocSmallSpace(sizeof(struct osi_file));
    AFS_GUNLOCK();
    if (afs_CacheFSType == AFS_APPL_HFS_CACHE) 
       code = igetinode(afs_cacheVfsp, (dev_t) cacheDev.dev, &ainode, &vp, &va, &dummy); /* XXX hfs is broken */
    else
       code = igetinode(afs_cacheVfsp, (dev_t) cacheDev.dev, (ino_t)ainode, &vp, &va, &dummy);
    AFS_GLOCK();
    if (code) {
	osi_FreeSmallSpace(afile);
	osi_Panic("UFSOpen: igetinode failed");
    }
    afile->vnode = vp;
    afile->size = va.va_size;
    afile->offset = 0;
    afile->proc = (int (*)()) 0;
    afile->inum = ainode;        /* for hint validity checking */
    return (void *)afile;
}

afs_osi_Stat(afile, astat)
    register struct osi_file *afile;
    register struct osi_stat *astat; {
    register afs_int32 code;
    struct vattr tvattr;
    AFS_STATCNT(osi_Stat);
    MObtainWriteLock(&afs_xosi,320);
    AFS_GUNLOCK();
    code=VOP_GETATTR(afile->vnode, &tvattr, &afs_osi_cred, current_proc());
    AFS_GLOCK();
    if (code == 0) {
	astat->size = tvattr.va_size;
	astat->blksize = tvattr.va_blocksize;
	astat->mtime = tvattr.va_mtime.tv_sec;
	astat->atime = tvattr.va_atime.tv_sec;
    }
    MReleaseWriteLock(&afs_xosi);
    return code;
}

osi_UFSClose(afile)
     register struct osi_file *afile;
  {
      AFS_STATCNT(osi_Close);
      if(afile->vnode) {
	AFS_RELE(afile->vnode);
      }
      
      osi_FreeSmallSpace(afile);
      return 0;
  }

osi_UFSTruncate(afile, asize)
    register struct osi_file *afile;
    afs_int32 asize; {
    struct AFS_UCRED *oldCred;
    struct vattr tvattr;
    register afs_int32 code;
    struct osi_stat tstat;
    AFS_STATCNT(osi_Truncate);

    /* This routine only shrinks files, and most systems
     * have very slow truncates, even when the file is already
     * small enough.  Check now and save some time.
     */
    code = afs_osi_Stat(afile, &tstat);
    if (code || tstat.size <= asize) return code;
    MObtainWriteLock(&afs_xosi,321);    
    VATTR_NULL(&tvattr);
    tvattr.va_size = asize;
    AFS_GUNLOCK();
    code=VOP_SETATTR(afile->vnode, &tvattr, &afs_osi_cred, current_proc());
    AFS_GLOCK();
    MReleaseWriteLock(&afs_xosi);
    return code;
}

void osi_DisableAtimes(avp)
struct vnode *avp;
{


   if (afs_CacheFSType == AFS_APPL_UFS_CACHE) {
      struct inode *ip = VTOI(avp);
      ip->i_flag &= ~IN_ACCESS;
   }
#ifdef VTOH /* can't do this without internals */
   else if (afs_CacheFSType == AFS_APPL_HFS_CACHE) {
      struct hfsnode *hp = VTOH(avp);
      hp->h_nodeflags &= ~IN_ACCESS;
   }
#endif
}


/* Generic read interface */
afs_osi_Read(afile, offset, aptr, asize)
    register struct osi_file *afile;
    int offset;
    char *aptr;
    afs_int32 asize; {
    struct AFS_UCRED *oldCred;
    unsigned int resid;
    register afs_int32 code;
    AFS_STATCNT(osi_Read);

    /**
      * If the osi_file passed in is NULL, panic only if AFS is not shutting
      * down. No point in crashing when we are already shutting down
      */
    if ( !afile ) {
	if ( !afs_shuttingdown )
	    osi_Panic("osi_Read called with null param");
	else
	    return EIO;
    }

    if (offset != -1) afile->offset = offset;
    AFS_GUNLOCK();
    code = gop_rdwr(UIO_READ, afile->vnode, (caddr_t) aptr, asize, afile->offset,
	          AFS_UIOSYS, IO_UNIT, &afs_osi_cred, &resid);
    AFS_GLOCK();
    if (code == 0) {
	code = asize - resid;
	afile->offset += code;
	osi_DisableAtimes(afile->vnode);
    }
    else {
	afs_Trace2(afs_iclSetp, CM_TRACE_READFAILED, ICL_TYPE_INT32, resid,
	         ICL_TYPE_INT32, code);
	code = -1;
    }
    return code;
}

/* Generic write interface */
afs_osi_Write(afile, offset, aptr, asize)
    register struct osi_file *afile;
    char *aptr;
    afs_int32 offset;
    afs_int32 asize; {
    struct AFS_UCRED *oldCred;
    unsigned int resid;
    register afs_int32 code;
    AFS_STATCNT(osi_Write);
    if ( !afile )
	osi_Panic("afs_osi_Write called with null param");
    if (offset != -1) afile->offset = offset;
    {
	AFS_GUNLOCK();
	code = gop_rdwr(UIO_WRITE, afile->vnode, (caddr_t) aptr, asize, afile->offset,
	          AFS_UIOSYS, IO_UNIT, &afs_osi_cred, &resid);
	AFS_GLOCK();
    }
    if (code == 0) {
	code = asize - resid;
	afile->offset += code;
    }
    else {
	code = -1;
    }
    if (afile->proc) {
	(*afile->proc)(afile, code);
    }
    return code;
}





void
shutdown_osifile()
{
  extern int afs_cold_shutdown;

  AFS_STATCNT(shutdown_osifile);
  if (afs_cold_shutdown) {
    afs_osicred_initialized = 0;
  }
}

