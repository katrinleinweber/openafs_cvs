/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * afs_vnop_dirops.c - make and remove directories
 *
 * Implements:
 *
 * afs_mkdir
 * afs_rmdir
 *
 */

#include <afsconfig.h>
#include "afs/param.h"

RCSID
    ("$Header: /cvs/openafs/src/afs/VNOPS/afs_vnop_dirops.c,v 1.24 2008/09/22 13:36:23 shadow Exp $");

#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"	/* statistics */
#include "afs/afs_cbqueue.h"
#include "afs/nfsclient.h"
#include "afs/afs_osidnlc.h"

extern afs_rwlock_t afs_xvcache;
extern afs_rwlock_t afs_xcbhash;

/* don't set CDirty in here because RPC is called synchronously */

int
afs_mkdir(OSI_VC_DECL(adp), char *aname, struct vattr *attrs, 
     register struct vcache **avcp, struct AFS_UCRED *acred)
{
    struct vrequest treq;
    register afs_int32 code;
    register struct conn *tc;
    struct VenusFid newFid;
    register struct dcache *tdc;
    struct dcache *new_dc;
    afs_size_t offset, len;
    register struct vcache *tvc;
    struct AFSStoreStatus InStatus;
    struct AFSFetchStatus OutFidStatus, OutDirStatus;
    struct AFSCallBack CallBack;
    struct AFSVolSync tsync;
    afs_int32 now;
    struct afs_fakestat_state fakestate;
    XSTATS_DECLS;
    OSI_VC_CONVERT(adp);

    AFS_STATCNT(afs_mkdir);
    afs_Trace2(afs_iclSetp, CM_TRACE_MKDIR, ICL_TYPE_POINTER, adp,
	       ICL_TYPE_STRING, aname);

    if ((code = afs_InitReq(&treq, acred)))
	goto done2;
    afs_InitFakeStat(&fakestate);

    if (strlen(aname) > AFSNAMEMAX) {
	code = ENAMETOOLONG;
	goto done3;
    }

    if (!afs_ENameOK(aname)) {
	code = EINVAL;
	goto done3;
    }
    
    AFS_DISCON_LOCK();

    code = afs_EvalFakeStat(&adp, &fakestate, &treq);
    if (code)
	goto done;
    code = afs_VerifyVCache(adp, &treq);
    if (code)
	goto done;

    /** If the volume is read-only, return error without making an RPC to the
      * fileserver
      */
    if (adp->states & CRO) {
	code = EROFS;
	goto done;
    }
   
    if (AFS_IS_DISCONNECTED && !AFS_IS_DISCON_RW)
	/*printf("Network is down in afs_mkdir\n");*/
	code = ENETDOWN;
    InStatus.Mask = AFS_SETMODTIME | AFS_SETMODE | AFS_SETGROUP;
    InStatus.ClientModTime = osi_Time();
    InStatus.UnixModeBits = attrs->va_mode & 0xffff;	/* only care about protection bits */
    InStatus.Group = (afs_int32) acred->cr_gid;
    tdc = afs_GetDCache(adp, (afs_size_t) 0, &treq, &offset, &len, 1);
    ObtainWriteLock(&adp->lock, 153);

    if (!AFS_IS_DISCON_RW) {

    	do {
	    tc = afs_Conn(&adp->fid, &treq, SHARED_LOCK);
	    if (tc) {
	    	XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_MAKEDIR);
	    	now = osi_Time();
	    	RX_AFS_GUNLOCK();
	    	code =
		    RXAFS_MakeDir(tc->id,
		    		(struct AFSFid *)&adp->fid.Fid,
				aname,
				&InStatus,
				(struct AFSFid *)&newFid.Fid,
				&OutFidStatus,
				&OutDirStatus,
				&CallBack,
				&tsync);
	    	RX_AFS_GLOCK();
	    	XSTATS_END_TIME;
	    	CallBack.ExpirationTime += now;
	    	/* DON'T forget to Set the callback value... */
	    } else
	    	code = -1;
    	} while (afs_Analyze
		    (tc, code, &adp->fid, &treq, AFS_STATS_FS_RPCIDX_MAKEDIR,
		     SHARED_LOCK, NULL));

    	if (code) {
	    if (code < 0) {
	    	ObtainWriteLock(&afs_xcbhash, 490);
	    	afs_DequeueCallback(adp);
	    	adp->states &= ~CStatd;
	    	ReleaseWriteLock(&afs_xcbhash);
	    	osi_dnlc_purgedp(adp);
	    }
	    ReleaseWriteLock(&adp->lock);
	    if (tdc)
	    	afs_PutDCache(tdc);
	    goto done;
        }

    } else {
#if defined(AFS_DISCON_ENV)
    	/* Disconnected. */

	/* We have the dir entry now, we can use it while disconnected. */
	if (adp->mvid == NULL) {
	    /* If not mount point, generate a new fid. */
	    newFid.Cell = adp->fid.Cell;
    	    newFid.Fid.Volume = adp->fid.Fid.Volume;
	    afs_GenFakeFid(&newFid, VDIR);
	}
    	/* XXX: If mount point???*/

	/* Operations with the actual dir's cache entry are further
	 * down, where the dir entry gets created.
	 */
#endif
    }			/* if (!AFS_IS_DISCON_RW) */

    /* otherwise, we should see if we can make the change to the dir locally */
    if (tdc)
	ObtainWriteLock(&tdc->lock, 632);
    if (AFS_IS_DISCON_RW || afs_LocalHero(adp, tdc, &OutDirStatus, 1)) {
	/* we can do it locally */
	ObtainWriteLock(&afs_xdcache, 294);
	code = afs_dir_Create(tdc, aname, &newFid.Fid);
	ReleaseWriteLock(&afs_xdcache);
	if (code) {
	    ZapDCE(tdc);	/* surprise error -- use invalid value */
	    DZap(tdc);
	}
    }
    if (tdc) {
	ReleaseWriteLock(&tdc->lock);
	afs_PutDCache(tdc);
    }

    if (AFS_IS_DISCON_RW)
	/* We will have to settle with the local link count. */
	adp->m.LinkCount++;
    else
	adp->m.LinkCount = OutDirStatus.LinkCount;
    newFid.Cell = adp->fid.Cell;
    newFid.Fid.Volume = adp->fid.Fid.Volume;
    ReleaseWriteLock(&adp->lock);
    if (AFS_IS_DISCON_RW) {
#if defined(AFS_DISCON_ENV)
    	/* When disconnected, we have to create the full dir here. */

	/* Generate a new vcache and fill it. */
	tvc = afs_NewVCache(&newFid, NULL);
	if (tvc) {
	    code = 0;
	    *avcp = tvc;
	} else {
	    code = ENOENT;
	    goto done;
	}

	ObtainWriteLock(&tvc->lock, 738);
	afs_GenDisconStatus(adp, tvc, &newFid, attrs, &treq, VDIR);
	ReleaseWriteLock(&tvc->lock);

	/* And now make an empty dir, containing . and .. : */
	/* Get a new dcache for it first. */
	new_dc = afs_GetDCache(tvc, (afs_size_t) 0, &treq, &offset, &len, 1);
	if (!new_dc) {
	    printf("afs_mkdir: can't get new dcache for dir.\n");
	    code = ENOENT;
	    goto done;
	}

	ObtainWriteLock(&afs_xdcache, 739);
	code = afs_dir_MakeDir(new_dc,
				(afs_int32 *) &newFid.Fid,
				(afs_int32) &adp->fid.Fid);
	ReleaseWriteLock(&afs_xdcache);
	if (code)
	    printf("afs_mkdir: afs_dirMakeDir code = %u\n", code);

	/* Add to dirty list. */
	if (!tvc->ddirty_flags ||
		(tvc->ddirty_flags == VDisconShadowed)) {

	    /* Put it in the list only if it's fresh. */
	    ObtainWriteLock(&afs_DDirtyVCListLock, 730);
	    AFS_DISCON_ADD_DIRTY(tvc);
	    ReleaseWriteLock(&afs_DDirtyVCListLock);
	}

	ObtainWriteLock(&tvc->lock, 731);
	/* Update length in the vcache. */
	tvc->m.Length = new_dc->f.chunkBytes;
	/* Set create flag. */
	tvc->ddirty_flags |= VDisconCreate;
	ReleaseWriteLock(&tvc->lock);
#endif				/* #ifdef AFS_DISCON_ENV */
    } else {
    	/* now we're done with parent dir, create the real dir's cache entry */
    	tvc = afs_GetVCache(&newFid, &treq, NULL, NULL);
    	if (tvc) {
	    code = 0;
	    *avcp = tvc;
    	} else
	    code = ENOENT;
    }				/* if (AFS_DISCON_RW) */

  done:
    AFS_DISCON_UNLOCK();
  done3:
    afs_PutFakeStat(&fakestate);
    code = afs_CheckCode(code, &treq, 26);
  done2:
    return code;
}


int
/* don't set CDirty in here because RPC is called synchronously */
#if	defined(AFS_SUN5_ENV) || defined(AFS_SGI_ENV)
afs_rmdir(OSI_VC_DECL(adp), char *aname, struct vnode *cdirp, 
	  struct AFS_UCRED *acred)
#else
afs_rmdir(OSI_VC_DECL(adp), char *aname, struct AFS_UCRED *acred)
#endif
{
    struct vrequest treq;
    register struct dcache *tdc;
    register struct vcache *tvc = NULL;
    register afs_int32 code;
    register struct conn *tc;
    afs_size_t offset, len;
    struct AFSFetchStatus OutDirStatus;
    struct AFSVolSync tsync;
    struct afs_fakestat_state fakestate;
    XSTATS_DECLS;
    OSI_VC_CONVERT(adp);

    AFS_STATCNT(afs_rmdir);

    afs_Trace2(afs_iclSetp, CM_TRACE_RMDIR, ICL_TYPE_POINTER, adp,
	       ICL_TYPE_STRING, aname);

    if ((code = afs_InitReq(&treq, acred)))
	goto done2;
    afs_InitFakeStat(&fakestate);

    if (strlen(aname) > AFSNAMEMAX) {
	code = ENAMETOOLONG;
	goto done;
    }

    AFS_DISCON_LOCK();

    code = afs_EvalFakeStat(&adp, &fakestate, &treq);
    if (code)
	goto done;

    code = afs_VerifyVCache(adp, &treq);
    if (code)
	goto done;

    /** If the volume is read-only, return error without making an RPC to the
      * fileserver
      */
    if (adp->states & CRO) {
	code = EROFS;
	goto done;
    }

   if (AFS_IS_DISCONNECTED && !AFS_IS_DISCON_RW) {
    	/* Disconnected read only mode. */
        code = ENETDOWN;
        goto done;
    }

    tdc = afs_GetDCache(adp, (afs_size_t) 0, &treq, &offset, &len, 1);	/* test for error below */
    ObtainWriteLock(&adp->lock, 154);
    if (tdc)
	ObtainSharedLock(&tdc->lock, 633);
    if (tdc && (adp->states & CForeign)) {
	struct VenusFid unlinkFid;

	unlinkFid.Fid.Vnode = 0;
	code = afs_dir_Lookup(tdc, aname, &unlinkFid.Fid);
	if (code == 0) {
	    afs_int32 cached = 0;

	    unlinkFid.Cell = adp->fid.Cell;
	    unlinkFid.Fid.Volume = adp->fid.Fid.Volume;
	    if (unlinkFid.Fid.Unique == 0) {
		tvc =
		    afs_LookupVCache(&unlinkFid, &treq, &cached, adp, aname);
	    } else {
		ObtainReadLock(&afs_xvcache);
		tvc = afs_FindVCache(&unlinkFid, 0, 1 /* do xstats */ );
		ReleaseReadLock(&afs_xvcache);
	    }
	}
    }

    if (!AFS_IS_DISCON_RW) {
	/* Not disconnected, can connect to server. */
    	do {
	    tc = afs_Conn(&adp->fid, &treq, SHARED_LOCK);
	    if (tc) {
	    	XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_REMOVEDIR);
	    	RX_AFS_GUNLOCK();
	    	code =
		    RXAFS_RemoveDir(tc->id,
		    		(struct AFSFid *)&adp->fid.Fid,
				aname,
				&OutDirStatus,
				&tsync);
	    	RX_AFS_GLOCK();
	    	XSTATS_END_TIME;
	    } else
	    	code = -1;
    	} while (afs_Analyze
	         (tc, code, &adp->fid, &treq, AFS_STATS_FS_RPCIDX_REMOVEDIR,
	         SHARED_LOCK, NULL));

    	if (code) {
	    if (tdc) {
	    	ReleaseSharedLock(&tdc->lock);
	    	afs_PutDCache(tdc);
	    }

	    if (code < 0) {
	    	ObtainWriteLock(&afs_xcbhash, 491);
	    	afs_DequeueCallback(adp);
	    	adp->states &= ~CStatd;
	    	ReleaseWriteLock(&afs_xcbhash);
	    	osi_dnlc_purgedp(adp);
	    }
	    ReleaseWriteLock(&adp->lock);
	    goto done;
    	}

    	/* here if rpc worked; update the in-core link count */
    	adp->m.LinkCount = OutDirStatus.LinkCount;

    } else {
#if defined(AFS_DISCON_ENV)
    	/* Disconnected. */

	if (!tvc) {
	    /* Find the vcache. */
	    struct VenusFid tfid;

	    tfid.Cell = adp->fid.Cell;
	    tfid.Fid.Volume = adp->fid.Fid.Volume;
	    code = afs_dir_Lookup(tdc, aname, &tfid.Fid);

	    ObtainSharedLock(&afs_xvcache, 764);
	    tvc = afs_FindVCache(&tfid, 0, 1 /* do xstats */ );
	    ReleaseSharedLock(&afs_xvcache);

	    if (!tvc) {
		printf("afs_rmdir: Can't find dir's vcache!\n");
		ReleaseSharedLock(&tdc->lock);
		afs_PutDCache(tdc);	/* drop ref count */
    		ReleaseWriteLock(&adp->lock);
	    	goto done;
	    }
	}

	if (tvc->m.LinkCount > 2) {
	    /* This dir contains more than . and .., thus it can't be
	     * deleted.
	     */
	    ReleaseSharedLock(&tdc->lock);
	    code = ENOTEMPTY;
	    goto done;
	}

    	/* Make a shadow copy of the parent dir (if not done already).
	 * There's no need to make a shadow copy of the deleted directory
	 * because a dir must be empty in order to be rmdir'ed.
	 * If the deleted dir has no shadow, it means that it was empty.
	 */
	if (!(adp->ddirty_flags & VDisconShadowed)) {
	    /* If tdc available, then it is locked.
	     * afs_MakeShadowDir unlocks it.
	     */
	    if (tdc)
	    	ReleaseSharedLock(&tdc->lock);
	    afs_MakeShadowDir(adp);
	    if (tdc)
	    	ObtainSharedLock(&tdc->lock, 732);
	}

	if (!tvc->ddirty_flags ||
		(tvc->ddirty_flags == VDisconShadowed)) {
	    /* Put it in the list only if it's fresh or has only been shadowed. */
	    ObtainWriteLock(&afs_DDirtyVCListLock, 728);
	    AFS_DISCON_ADD_DIRTY(tvc);
	    ReleaseWriteLock(&afs_DDirtyVCListLock);
	}

	/* Now add the vcache to the dirty list. */
	ObtainWriteLock(&tvc->lock, 727);
	tvc->ddirty_flags |= VDisconRemove;
	ReleaseWriteLock(&tvc->lock);

	adp->m.LinkCount--;
#endif				/* #ifdef AFS_DISCON_ENV */
    }				/* if (!AFS_IS_DISCON_RW) */

    if (tdc)
	UpgradeSToWLock(&tdc->lock, 634);
    if (AFS_IS_DISCON_RW || afs_LocalHero(adp, tdc, &OutDirStatus, 1)) {
	/* we can do it locally */
	code = afs_dir_Delete(tdc, aname);
	if (code) {
	    ZapDCE(tdc);	/* surprise error -- invalid value */
	    DZap(tdc);
	}
    }
    if (tdc) {
	ReleaseWriteLock(&tdc->lock);
	afs_PutDCache(tdc);	/* drop ref count */
    }


    if (tvc)
	osi_dnlc_purgedp(tvc);	/* get rid of any entries for this directory */
    else
	osi_dnlc_remove(adp, aname, 0);

    if (tvc) {
	ObtainWriteLock(&tvc->lock, 155);
	tvc->states &= ~CUnique;	/* For the dfs xlator */
	ReleaseWriteLock(&tvc->lock);
	/* If disconnected, keep this vcache around for resync. */
	if (!AFS_IS_DISCON_RW)
	    afs_PutVCache(tvc);
    }
    ReleaseWriteLock(&adp->lock);
    /* don't worry about link count since dirs can not be hardlinked */
    code = 0;

  done:
    AFS_DISCON_UNLOCK();
  done3:
    afs_PutFakeStat(&fakestate);
    code = afs_CheckCode(code, &treq, 27);
  done2:
    return code;
}
