/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#include "../afs/param.h"

RCSID("$Header: /cvs/openafs/src/afs/afs_daemons.c,v 1.10 2001/11/01 04:39:08 shadow Exp $");

#include "../afs/sysincludes.h"	/* Standard vendor system headers */
#include "../afs/afsincludes.h"	/* Afs-based standard headers */
#include "../afs/afs_stats.h"   /* statistics gathering code */
#include "../afs/afs_cbqueue.h" 
#ifdef AFS_AIX_ENV
#include <sys/adspace.h>	/* for vm_att(), vm_det() */
#endif


/* background request queue size */
afs_lock_t afs_xbrs;		/* lock for brs */
static int brsInit = 0;
short afs_brsWaiters = 0;	/* number of users waiting for brs buffers */
short afs_brsDaemons = 0;	/* number of daemons waiting for brs requests */
struct brequest	afs_brs[NBRS];	/* request structures */
struct afs_osi_WaitHandle AFS_WaitHandler, AFS_CSWaitHandler;

static int rxepoch_checked=0;
#define afs_CheckRXEpoch() {if (rxepoch_checked == 0 && rxkad_EpochWasSet) { \
	rxepoch_checked = 1; afs_GCUserData(/* force flag */ 1);  } }

extern char afs_rootVolumeName[];
extern struct vcache *afs_globalVp;
extern struct VenusFid afs_rootFid;
extern struct osi_dev cacheDev;
extern char *afs_indexFlags;
extern afs_rwlock_t afs_xvcache;
extern struct afs_exporter *afs_nfsexporter;
extern int cacheDiskType;
extern int afs_BumpBase();
extern void afs_CheckCallbacks();

/* PAG garbage collection */
/* We induce a compile error if param.h does not define AFS_GCPAGS */
afs_int32 afs_gcpags=AFS_GCPAGS;
afs_int32 afs_gcpags_procsize;

afs_int32 afs_CheckServerDaemonStarted = 0;
afs_int32 PROBE_INTERVAL=180;	/* default to 3 min */

#define PROBE_WAIT() (1000 * (PROBE_INTERVAL - ((afs_random() & 0x7fffffff) \
		      % (PROBE_INTERVAL/2))))

afs_CheckServerDaemon()
{
    afs_int32 now, delay, lastCheck, last10MinCheck;

    afs_CheckServerDaemonStarted = 1;

    while (afs_initState < 101) afs_osi_Sleep(&afs_initState);
    afs_osi_Wait(PROBE_WAIT(), &AFS_CSWaitHandler, 0);  
    
    last10MinCheck = lastCheck = osi_Time();
    while ( 1 ) {
	if (afs_termState == AFSOP_STOP_CS) {
	    afs_termState = AFSOP_STOP_BKG;
	    afs_osi_Wakeup(&afs_termState);
	    break;
	}

	now = osi_Time();
	if (PROBE_INTERVAL + lastCheck <= now) {
	    afs_CheckServers(1, (struct cell *) 0); /* check down servers */
	    lastCheck = now = osi_Time();
	}

	if (600 + last10MinCheck <= now) {
	    afs_Trace1(afs_iclSetp, CM_TRACE_PROBEUP, ICL_TYPE_INT32, 600);
	    afs_CheckServers(0, (struct cell *) 0);
	    last10MinCheck = now = osi_Time();
	}
	/* shutdown check. */
	if (afs_termState == AFSOP_STOP_CS) {
	    afs_termState = AFSOP_STOP_BKG;
	    afs_osi_Wakeup(&afs_termState);
	    break;
	}

	/* Compute time to next probe. */
	delay = PROBE_INTERVAL + lastCheck;
	if (delay > 600 + last10MinCheck)
	    delay = 600 + last10MinCheck;
	delay -= now;
	if (delay < 1)
	    delay = 1;
	afs_osi_Wait(delay * 1000,  &AFS_CSWaitHandler, 0);  
    }
    afs_CheckServerDaemonStarted = 0;
}

void afs_Daemon() {
    afs_int32 code;
    extern struct afs_exporter *root_exported;
    struct afs_exporter *exporter;
    afs_int32 now;
    afs_int32 last3MinCheck, last10MinCheck, last60MinCheck, lastNMinCheck;
    afs_int32 last1MinCheck;
    afs_uint32 lastCBSlotBump;
    char cs_warned = 0;

    AFS_STATCNT(afs_Daemon);
    last1MinCheck = last3MinCheck = last60MinCheck = last10MinCheck = lastNMinCheck = 0;

    afs_rootFid.Fid.Volume = 0;
    while (afs_initState < 101) afs_osi_Sleep(&afs_initState);

    now = osi_Time();
    lastCBSlotBump = now;

    /* when a lot of clients are booted simultaneously, they develop
     * annoying synchronous VL server bashing behaviors.  So we stagger them.
     */
    last1MinCheck = now + ((afs_random() & 0x7fffffff) % 60); /* an extra 30 */
    last3MinCheck = now - 90 + ((afs_random() & 0x7fffffff) % 180);
    last60MinCheck = now - 1800 + ((afs_random() & 0x7fffffff) % 3600);
    last10MinCheck = now - 300 + ((afs_random() & 0x7fffffff) % 600);
    lastNMinCheck = now - 90 + ((afs_random() & 0x7fffffff) % 180);

    /* start off with afs_initState >= 101 (basic init done) */
    while(1) {
	afs_CheckCallbacks(20);  /* unstat anything which will expire soon */
	
	/* things to do every 20 seconds or less - required by protocol spec */
	if (afs_nfsexporter) 
	    afs_FlushActiveVcaches(0);	/* flush NFS writes */
	afs_FlushVCBs(1);		/* flush queued callbacks */
	afs_MaybeWakeupTruncateDaemon();	/* free cache space if have too */
	rx_CheckPackets();		/* Does RX need more packets? */
#if	defined(AFS_AIX32_ENV) || defined(AFS_HPUX_ENV)
	/* 
	 * Hack: We always want to make sure there are plenty free
	 * entries in the small free pool so that we don't have to
	 * worry about rx (with disabled interrupts) to have to call
	 * malloc). So we do the dummy call below...
	 */
	if (((afs_stats_cmperf.SmallBlocksAlloced - afs_stats_cmperf.SmallBlocksActive) 
	     <= AFS_SALLOC_LOW_WATER))
	    osi_FreeSmallSpace(osi_AllocSmallSpace(AFS_SMALLOCSIZ));
	if (((afs_stats_cmperf.MediumBlocksAlloced - afs_stats_cmperf.MediumBlocksActive) 
	     <= AFS_MALLOC_LOW_WATER+50)) 
	    osi_AllocMoreMSpace(AFS_MALLOC_LOW_WATER * 2); 
#endif
	
	now = osi_Time();
	if (lastCBSlotBump + CBHTSLOTLEN < now) {  /* pretty time-dependant */
	    lastCBSlotBump = now;
	    if (afs_BumpBase()) {
		afs_CheckCallbacks(20);  /* unstat anything which will expire soon */
	    }
	}
	
	if (last1MinCheck + 60 < now) {
	    /* things to do every minute */
	    DFlush();			/* write out dir buffers */
	    afs_WriteThroughDSlots();	/* write through cacheinfo entries */
	    afs_FlushActiveVcaches(1);/* keep flocks held & flush nfs writes */
	    afs_CheckRXEpoch();
	    last1MinCheck = now;
	}
	
	if (last3MinCheck + 180 < now) {
	    afs_CheckTokenCache();	/* check for access cache resets due to expired
					   tickets */
	    last3MinCheck = now;
	}
	if (!afs_CheckServerDaemonStarted) {
	    /* Do the check here if the correct afsd is not installed. */
	    if (!cs_warned) {
		cs_warned = 1;
		printf("Please install afsd with check server daemon.\n");
	    }
	    if (lastNMinCheck + PROBE_INTERVAL < now) {
		/* only check down servers */
		afs_CheckServers(1, (struct cell *) 0);
		lastNMinCheck = now;
	    }
	}
	if (last10MinCheck + 600 < now) {
#ifdef AFS_USERSPACE_IP_ADDR	
	    extern int rxi_GetcbiInfo(void);
#endif
	    afs_Trace1(afs_iclSetp, CM_TRACE_PROBEUP,
		       ICL_TYPE_INT32, 600);
#ifdef AFS_USERSPACE_IP_ADDR	
	    if (rxi_GetcbiInfo()) { /* addresses changed from last time */
		afs_FlushCBs();
	    }
#else  /* AFS_USERSPACE_IP_ADDR */
	    if (rxi_GetIFInfo()) { /* addresses changed from last time */
		afs_FlushCBs();
	    }
#endif /* else AFS_USERSPACE_IP_ADDR */
	    if (!afs_CheckServerDaemonStarted)
		afs_CheckServers(0, (struct cell *) 0);
	    afs_GCUserData(0);	    /* gc old conns */
	    /* This is probably the wrong way of doing GC for the various exporters but it will suffice for a while */
	    for (exporter = root_exported; exporter; exporter = exporter->exp_next) {
		(void) EXP_GC(exporter, 0);	/* Generalize params */
	    }
	    {
		static int cnt=0;
		if (++cnt < 12) {
		    afs_CheckVolumeNames(AFS_VOLCHECK_EXPIRED |
					 AFS_VOLCHECK_BUSY);
		} else {
		    cnt = 0;
		    afs_CheckVolumeNames(AFS_VOLCHECK_EXPIRED |
					 AFS_VOLCHECK_BUSY |
					 AFS_VOLCHECK_MTPTS);
		}
	    }
	    last10MinCheck = now;
	}
	if (last60MinCheck + 3600 < now) {
	    afs_Trace1(afs_iclSetp, CM_TRACE_PROBEVOLUME,
		       ICL_TYPE_INT32, 3600);
	    afs_CheckRootVolume();
#if AFS_GCPAGS
	    if (afs_gcpags == AFS_GCPAGS_OK) {
	        afs_int32 didany;
		afs_GCPAGs(&didany);
	    }
#endif
	    last60MinCheck = now;
	}
	if (afs_initState < 300) {	/* while things ain't rosy */
	    code = afs_CheckRootVolume();
	    if (code ==	0) afs_initState = 300;		    /* succeeded */
	    if (afs_initState <	200) afs_initState = 200;   /* tried once */
	    afs_osi_Wakeup(&afs_initState);
	}
	
	/* 18285 is because we're trying to divide evenly into 128, that is,
	 * CBSlotLen, while staying just under 20 seconds.  If CBSlotLen
	 * changes, should probably change this interval, too. 
	 * Some of the preceding actions may take quite some time, so we
	 * might not want to wait the entire interval */
	now = 18285 - (osi_Time() - now);
	if (now > 0) {
	    afs_osi_Wait(now, &AFS_WaitHandler, 0);  
	}
	
	if (afs_termState == AFSOP_STOP_AFS) {
	    if (afs_CheckServerDaemonStarted)
		afs_termState = AFSOP_STOP_CS;
	    else
		afs_termState = AFSOP_STOP_BKG;
	    afs_osi_Wakeup(&afs_termState);
	    return;
	}
    }
}

afs_CheckRootVolume () {
    char rootVolName[32];
    register struct volume *tvp;
    int usingDynroot = afs_GetDynrootEnable();

    AFS_STATCNT(afs_CheckRootVolume);
    if (*afs_rootVolumeName == 0) {
	strcpy(rootVolName, "root.afs");
    }
    else {
	strcpy(rootVolName, afs_rootVolumeName);
    }
    if (usingDynroot) {
	afs_GetDynrootFid(&afs_rootFid);
	tvp = afs_GetVolume(&afs_rootFid, (struct vrequest *) 0, READ_LOCK);
    } else {
	tvp = afs_GetVolumeByName(rootVolName, LOCALCELL, 1, (struct vrequest *) 0, READ_LOCK);
    }
    if (!tvp) {
	char buf[128];
	int len = strlen(rootVolName);

	if ((len < 9) || strcmp(&rootVolName[len - 9], ".readonly")) {
	    strcpy(buf, rootVolName);
	    afs_strcat(buf, ".readonly");
	    tvp = afs_GetVolumeByName(buf, LOCALCELL, 1, (struct vrequest *) 0, READ_LOCK);
	}
    }
    if (tvp) {
	if (!usingDynroot) {
	    int volid = (tvp->roVol? tvp->roVol : tvp->volume);
	    afs_rootFid.Cell = LOCALCELL;
	    if (afs_rootFid.Fid.Volume && afs_rootFid.Fid.Volume != volid
		&& afs_globalVp) {
		/* If we had a root fid before and it changed location we reset
		 * the afs_globalVp so that it will be reevaluated.
		 * Just decrement the reference count. This only occurs during
		 * initial cell setup and can panic the machine if we set the
		 * count to zero and fs checkv is executed when the current
		 * directory is /afs.
		 */
		AFS_FAST_RELE(afs_globalVp);
		afs_globalVp = 0;
	    }
	    afs_rootFid.Fid.Volume = volid;
	    afs_rootFid.Fid.Vnode = 1;
	    afs_rootFid.Fid.Unique = 1;
	}
	afs_initState = 300;    /* won */
	afs_osi_Wakeup(&afs_initState);
	afs_PutVolume(tvp, READ_LOCK);
    }
#ifdef AFS_DEC_ENV
/* This is to make sure that we update the root gnode */
/* every time root volume gets released */
    {
	extern struct vfs *afs_globalVFS;
	extern int afs_root();
	struct gnode *rootgp;
	struct mount *mp;
	int code;

	/* Only do this if afs_globalVFS is properly set due to race conditions
	   this routine could be called before the gfs_mount is performed!
	   Furthermore, afs_root (called below) *waits* until
	   initState >= 200, so we don't try this until we've gotten
	   at least that far */
	if (afs_globalVFS && afs_initState >= 200) {
	    if (code = afs_root(afs_globalVFS, &rootgp))
		return code;
	    mp = (struct mount *) afs_globalVFS->vfs_data ;
	    mp->m_rootgp = gget(mp, 0, 0, (char *)rootgp);
	    afs_unlock(mp->m_rootgp);	/* unlock basic gnode */
	    afs_vrele((struct vcache *) rootgp);  /* zap afs_root's vnode hold */
	}
    }
#endif
    if (afs_rootFid.Fid.Volume) return 0;
    else return ENOENT;
}

/* parm 0 is the pathname, parm 1 to the fetch is the chunk number */
void BPath(ab)
    register struct brequest *ab; {
    register struct dcache *tdc;
    struct vcache *tvc;
    struct vnode *tvn;
#ifdef AFS_LINUX22_ENV
    struct dentry *dp = NULL;
#endif
    afs_size_t offset, len;
    struct vrequest treq;
    afs_int32 code;

    AFS_STATCNT(BPath);
    if (code = afs_InitReq(&treq, ab->cred)) return;
    AFS_GUNLOCK();
#ifdef AFS_LINUX22_ENV
    code = gop_lookupname((char *)ab->parm[0], AFS_UIOSYS, 1,  (struct vnode **) 0, &dp);
    if (dp)
	tvn = (struct vnode*)dp->d_inode;
#else
    code = gop_lookupname((char *)ab->parm[0], AFS_UIOSYS, 1,  (struct vnode **) 0, (struct vnode **)&tvn);
#endif
    AFS_GLOCK();
    osi_FreeLargeSpace((char *)ab->parm[0]); /* free path name buffer here */
    if (code) return;
    /* now path may not have been in afs, so check that before calling our cache manager */
    if (!tvn || !IsAfsVnode((struct vnode *) tvn)) {
	/* release it and give up */
	if (tvn) {
#ifdef AFS_DEC_ENV
	    grele(tvn);
#else
#ifdef AFS_LINUX22_ENV
	    dput(dp);
#else
	    AFS_RELE((struct vnode *) tvn);
#endif
#endif
	}
	return;
    }
#ifdef AFS_DEC_ENV
    tvc = (struct vcache *) afs_gntovn(tvn);
#else
    tvc = (struct vcache *) tvn;
#endif
    /* here we know its an afs vnode, so we can get the data for the chunk */
    tdc = afs_GetDCache(tvc, (afs_size_t) ab->parm[1], &treq, &offset, &len, 1);
    if (tdc) {
	afs_PutDCache(tdc);
    }
#ifdef AFS_DEC_ENV
    grele(tvn);
#else
#ifdef AFS_LINUX22_ENV
    dput(dp);
#else
    AFS_RELE((struct vnode *) tvn);
#endif
#endif
}

/* parm 0 to the fetch is the chunk number; parm 1 is the dcache entry to wakeup,
 * parm 2 is true iff we should release the dcache entry here.
 */
void BPrefetch(ab)
    register struct brequest *ab; {
    register struct dcache *tdc;
    register struct vcache *tvc;
    afs_size_t offset, len;
    struct vrequest treq;

    AFS_STATCNT(BPrefetch);
    if (len = afs_InitReq(&treq, ab->cred)) return;
    tvc = ab->vnode;
    tdc = afs_GetDCache(tvc, (afs_size_t)ab->parm[0], &treq, &offset, &len, 1);
    if (tdc) {
	afs_PutDCache(tdc);
    }
    /* now, dude may be waiting for us to clear DFFetchReq bit; do so.  Can't
     * use tdc from GetDCache since afs_GetDCache may fail, but someone may
     * be waiting for our wakeup anyway.
     */
    tdc = (struct dcache *) (ab->parm[1]);
    tdc->flags &= ~DFFetchReq;
    afs_osi_Wakeup(&tdc->validPos);
    if ((afs_size_t)ab->parm[2]) {
#ifdef	AFS_SUN5_ENVX
	mutex_enter(&tdc->lock);
	tdc->refCount--;
	mutex_exit(&tdc->lock);
#else
	afs_PutDCache(tdc);	/* put this one back, too */
#endif
    }
}


void BStore(ab)
    register struct brequest *ab; {
    register struct vcache *tvc;
    register afs_int32 code;
    struct vrequest treq;
#if defined(AFS_SGI_ENV)
    struct cred *tmpcred;
#endif

    AFS_STATCNT(BStore);
    if (code = afs_InitReq(&treq, ab->cred)) return;
    code = 0;
    tvc = ab->vnode;
#if defined(AFS_SGI_ENV)
    /*
     * Since StoreOnLastReference can end up calling osi_SyncVM which
     * calls into VM code that assumes that u.u_cred has the
     * correct credentials, we set our to theirs for this xaction
     */
    tmpcred = OSI_GET_CURRENT_CRED();
    OSI_SET_CURRENT_CRED(ab->cred);

    /*
     * To avoid recursion since the WriteLock may be released during VM
     * operations, we hold the VOP_RWLOCK across this transaction as
     * do the other callers of StoreOnLastReference
     */
    AFS_RWLOCK((vnode_t *)tvc, 1);
#endif
    ObtainWriteLock(&tvc->lock,209);
    code = afs_StoreOnLastReference(tvc, &treq);
    ReleaseWriteLock(&tvc->lock);
#if defined(AFS_SGI_ENV)
    OSI_SET_CURRENT_CRED(tmpcred);
    AFS_RWUNLOCK((vnode_t *)tvc, 1);
#endif
    /* now set final return code, and wakeup anyone waiting */
    if ((ab->flags & BUVALID) == 0) {
	ab->code = afs_CheckCode(code, &treq, 43);    /* set final code, since treq doesn't go across processes */
	ab->flags |= BUVALID;
	if (ab->flags & BUWAIT) {
	    ab->flags &= ~BUWAIT;
	    afs_osi_Wakeup(ab);
	}
    }
}

/* release a held request buffer */
void afs_BRelease(ab)
    register struct brequest *ab; {

    AFS_STATCNT(afs_BRelease);
    MObtainWriteLock(&afs_xbrs,294);
    if (--ab->refCount <= 0) {
	ab->flags = 0;
    }
    if (afs_brsWaiters) afs_osi_Wakeup(&afs_brsWaiters);
    MReleaseWriteLock(&afs_xbrs);
}

/* return true if bkg fetch daemons are all busy */
int afs_BBusy() {
    AFS_STATCNT(afs_BBusy);
    if (afs_brsDaemons > 0) return 0;
    return 1;
}

struct brequest *afs_BQueue(aopcode, avc, dontwait, ause, acred, aparm0, aparm1, aparm2, aparm3)
    register short aopcode;
    afs_int32 ause, dontwait;
    register struct vcache *avc;
    struct AFS_UCRED *acred;
    /* On 64 bit platforms, "long" does the right thing. */
    afs_size_t aparm0, aparm1, aparm2, aparm3;
{
    register int i;
    register struct brequest *tb;

    AFS_STATCNT(afs_BQueue);
    MObtainWriteLock(&afs_xbrs,296);
    while (1) {
	tb = afs_brs;
	for(i=0;i<NBRS;i++,tb++) {
	    if (tb->refCount == 0) break;
	}
	if (i < NBRS) {
	    /* found a buffer */
	    tb->opcode = aopcode;
	    tb->vnode = avc;
	    tb->cred = acred;
	    crhold(tb->cred);
	    if (avc) {
#ifdef	AFS_DEC_ENV
		avc->vrefCount++;
#else
		VN_HOLD((struct vnode *)avc);
#endif
	    }
	    tb->refCount = ause+1;
	    tb->parm[0] = aparm0;
	    tb->parm[1] = aparm1;
	    tb->parm[2] = aparm2;
	    tb->parm[3] = aparm3;
	    tb->flags = 0;
	    tb->code = 0;
	    /* if daemons are waiting for work, wake them up */
	    if (afs_brsDaemons > 0) {
		afs_osi_Wakeup(&afs_brsDaemons);
	    }
	    MReleaseWriteLock(&afs_xbrs);
	    return tb;
	}
        if (dontwait) {
	    MReleaseWriteLock(&afs_xbrs);
	    return (struct brequest *)0;
	}
	/* no free buffers, sleep a while */
	afs_brsWaiters++;
	MReleaseWriteLock(&afs_xbrs);
	afs_osi_Sleep(&afs_brsWaiters);
	MObtainWriteLock(&afs_xbrs,301);
	afs_brsWaiters--;
    }
}

#ifdef	AFS_AIX32_ENV
#ifdef AFS_AIX41_ENV
/* AIX 4.1 has a much different sleep/wakeup mechanism available for use. 
 * The modifications here will work for either a UP or MP machine.
 */
struct buf *afs_asyncbuf = (struct buf*)0;
afs_int32 afs_asyncbuf_cv = EVENT_NULL;
afs_int32 afs_biodcnt = 0;

/* in implementing this, I assumed that all external linked lists were
 * null-terminated.  
 *
 * Several places in this code traverse a linked list.  The algorithm
 * used here is probably unfamiliar to most people.  Careful examination
 * will show that it eliminates an assignment inside the loop, as compared
 * to the standard algorithm, at the cost of occasionally using an extra
 * variable.
 */

/* get_bioreq()
 *
 * This function obtains, and returns, a pointer to a buffer for
 * processing by a daemon.  It sleeps until such a buffer is available.
 * The source of buffers for it is the list afs_asyncbuf (see also 
 * naix_vm_strategy).  This function may be invoked concurrently by
 * several processes, that is, several instances of the same daemon.
 * naix_vm_strategy, which adds buffers to the list, runs at interrupt
 * level, while get_bioreq runs at process level.
 *
 * Since AIX 4.1 can wake just one process at a time, the separate sleep
 * addresses have been removed.
 * Note that the kernel_lock is held until the e_sleep_thread() occurs. 
 * The afs_asyncbuf_lock is primarily used to serialize access between
 * process and interrupts.
 */
Simple_lock afs_asyncbuf_lock;
/*static*/ struct buf *afs_get_bioreq()
{
    struct buf *bp = (struct buf *) 0;
    struct buf *bestbp;
    struct buf **bestlbpP, **lbpP;
    int bestage, stop;
    struct buf *t1P, *t2P;      /* temp pointers for list manipulation */
    int oldPriority;
    afs_uint32 wait_ret;
    struct afs_bioqueue *s;

    /* ??? Does the forward pointer of the returned buffer need to be NULL?
    */
    
    /* Disable interrupts from the strategy function, and save the 
     * prior priority level and lock access to the afs_asyncbuf.
     */
    AFS_GUNLOCK();
    oldPriority = disable_lock(INTMAX, &afs_asyncbuf_lock) ;

    while(1) {
	if (afs_asyncbuf) {
	    /* look for oldest buffer */
	    bp = bestbp = afs_asyncbuf;
	    bestage = (int) bestbp->av_back;
	    bestlbpP = &afs_asyncbuf;
	    while (1) {
		lbpP = &bp->av_forw;
		bp = *lbpP;
		if (!bp) break;
		if ((int) bp->av_back - bestage < 0) {
		    bestbp = bp;
		    bestlbpP = lbpP;
		    bestage = (int) bp->av_back;
		}
	    }
	    bp = bestbp;
	    *bestlbpP = bp->av_forw;
	    break;
	}
	else {
	    /* If afs_asyncbuf is null, it is necessary to go to sleep.
	     * e_wakeup_one() ensures that only one thread wakes.
	     */
	    int interrupted;
	    /* The LOCK_HANDLER indicates to e_sleep_thread to only drop the
	     * lock on an MP machine.
	     */
	    interrupted = e_sleep_thread(&afs_asyncbuf_cv,
					 &afs_asyncbuf_lock,
					 LOCK_HANDLER|INTERRUPTIBLE);
	    if (interrupted==THREAD_INTERRUPTED) {
		/* re-enable interrupts from strategy */
		unlock_enable(oldPriority, &afs_asyncbuf_lock);
		AFS_GLOCK();
		return(NULL);
	    }
	}  /* end of "else asyncbuf is empty" */
    } /* end of "inner loop" */
    
    /*assert (bp);*/
    
    unlock_enable(oldPriority, &afs_asyncbuf_lock);
    AFS_GLOCK();

    /* For the convenience of other code, replace the gnodes in
     * the b_vp field of bp and the other buffers on the b_work
     * chain with the corresponding vnodes.   
     *
     * ??? what happens to the gnodes?  They're not just cut loose,
     * are they?
     */
    for(t1P=bp;;) {
	t2P = (struct buf *) t1P->b_work;
	t1P->b_vp = ((struct gnode *) t1P->b_vp)->gn_vnode;
	if (!t2P) 
	    break;

	t1P = (struct buf *) t2P->b_work;
	t2P->b_vp = ((struct gnode *) t2P->b_vp)->gn_vnode;
	if (!t1P)
	    break;
    }

    /* If the buffer does not specify I/O, it may immediately
     * be returned to the caller.  This condition is detected
     * by examining the buffer's flags (the b_flags field).  If
     * the B_PFPROT bit is set, the buffer represents a protection
     * violation, rather than a request for I/O.  The remainder
     * of the outer loop handles the case where the B_PFPROT bit is clear.
     */
    if (bp->b_flags & B_PFPROT)  {
	return (bp);
    }
    return (bp);

} /* end of function get_bioreq() */


/* afs_BioDaemon
 *
 * This function is the daemon.  It is called from the syscall
 * interface.  Ordinarily, a script or an administrator will run a
 * daemon startup utility, specifying the number of I/O daemons to
 * run.  The utility will fork off that number of processes,
 * each making the appropriate syscall, which will cause this
 * function to be invoked.
 */
static int afs_initbiod = 0;		/* this is self-initializing code */
int DOvmlock = 0;
afs_BioDaemon (nbiods)
    afs_int32 nbiods;
{
    afs_int32 code, s, pflg = 0;
    label_t jmpbuf;
    struct buf *bp, *bp1, *tbp1, *tbp2;		/* temp pointers only */
    caddr_t tmpaddr;
    struct vnode *vp;
    struct vcache *vcp;
    char tmperr;
    if (!afs_initbiod) {
	/* XXX ###1 XXX */
	afs_initbiod = 1;
	/* pin lock, since we'll be using it in an interrupt. */
	lock_alloc(&afs_asyncbuf_lock, LOCK_ALLOC_PIN, 2, 1);
	simple_lock_init(&afs_asyncbuf_lock);
	pin (&afs_asyncbuf, sizeof(struct buf*));
	pin (&afs_asyncbuf_cv, sizeof(afs_int32));
    }

    /* Ignore HUP signals... */
#ifdef AFS_AIX41_ENV
    {
 	sigset_t sigbits, osigbits;
 	/*
 	 * add SIGHUP to the set of already masked signals
 	 */
 	SIGFILLSET(sigbits);			/* allow all signals	*/
 	SIGDELSET(sigbits, SIGHUP);		/*   except SIGHUP	*/
 	limit_sigs(&sigbits, &osigbits);	/*   and already masked */
    }
#else
    SIGDELSET(u.u_procp->p_sig, SIGHUP);
    SIGADDSET(u.u_procp->p_sigignore, SIGHUP);
    SIGDELSET(u.u_procp->p_sigcatch, SIGHUP);
#endif
    /* Main body starts here -- this is an intentional infinite loop, and
     * should NEVER exit 
     *
     * Now, the loop will exit if get_bioreq() returns NULL, indicating 
     * that we've been interrupted.
     */
    while (1) {
	bp = afs_get_bioreq();
	if (!bp)
	    break;	/* we were interrupted */
	if (code = setjmpx(&jmpbuf)) {
	    /* This should not have happend, maybe a lack of resources  */
	    AFS_GUNLOCK();
	    s = disable_lock(INTMAX, &afs_asyncbuf_lock);
	    for (bp1 = bp; bp ; bp = bp1) {
		if (bp1)
		    bp1 = (struct buf *) bp1->b_work;
		bp->b_actf = 0;
		bp->b_error = code;
		bp->b_flags |= B_ERROR;
		iodone(bp);
	    }
	    unlock_enable(s, &afs_asyncbuf_lock);
	    AFS_GLOCK();
	    continue;
	}
	vcp = (struct vcache *)bp->b_vp;
	if (bp->b_flags & B_PFSTORE) {	/* XXXX */
	    ObtainWriteLock(&vcp->lock,404);	    
	    if (vcp->v.v_gnode->gn_mwrcnt) {
#ifdef AFS_64BIT_CLIENT
		if (vcp->m.Length < 
				(afs_offs_t)dbtob(bp->b_blkno) + bp->b_bcount)
		    vcp->m.Length = 
				(afs_offs_t)dbtob(bp->b_blkno) + bp->b_bcount;
#else /* AFS_64BIT_CLIENT */
		if (vcp->m.Length < bp->b_bcount + (u_int)dbtob(bp->b_blkno))
		    vcp->m.Length = bp->b_bcount + (u_int)dbtob(bp->b_blkno);
#endif /* AFS_64BIT_CLIENT */
	    }
	    ReleaseWriteLock(&vcp->lock);
	}
	/* If the buffer represents a protection violation, rather than
	 * an actual request for I/O, no special action need be taken.  
	 */
	if ( bp->b_flags & B_PFPROT ) {   
	    iodone (bp);    /* Notify all users of the buffer that we're done */
	    clrjmpx(&jmpbuf);
	    continue;
        }
if (DOvmlock)
	ObtainWriteLock(&vcp->pvmlock,211);     
	/*
	 * First map its data area to a region in the current address space
	 * by calling vm_att with the subspace identifier, and a pointer to
	 * the data area.  vm_att returns  a new data area pointer, but we
	 * also want to hang onto the old one.
	 */
	tmpaddr = bp->b_baddr;
	bp->b_baddr = vm_att (bp->b_xmemd.subspace_id, tmpaddr);
	tmperr = afs_ustrategy(bp);	/* temp variable saves offset calculation */
	if (tmperr) {			/* in non-error case */
	    bp->b_flags |= B_ERROR;		/* should other flags remain set ??? */
	    bp->b_error = tmperr;
	}

	/* Unmap the buffer's data area by calling vm_det.  Reset data area
	 * to the value that we saved above.
	 */
	vm_det(bp->b_un.b_addr);
	bp->b_baddr = tmpaddr;

	/*
	 * buffer may be linked with other buffers via the b_work field.
	 * See also naix_vm_strategy.  For each buffer in the chain (including
	 * bp) notify all users of the buffer that the daemon is finished
	 * using it by calling iodone.  
	 * assumes iodone can modify the b_work field.
	 */
	for(tbp1=bp;;) {
	    tbp2 = (struct buf *) tbp1->b_work;
	    iodone(tbp1);
	    if (!tbp2) 
		break;

	    tbp1 = (struct buf *) tbp2->b_work;
	    iodone(tbp2);
	    if (!tbp1)
		break;
	}
if (DOvmlock)
	ReleaseWriteLock(&vcp->pvmlock);     /* Unlock the vnode.  */
	clrjmpx(&jmpbuf);
    } /* infinite loop (unless we're interrupted) */
} /* end of afs_BioDaemon() */

#else /* AFS_AIX41_ENV */


#define	squeue afs_q
struct afs_bioqueue {
    struct squeue lruq;
    int sleeper;
    int cnt;
};
struct afs_bioqueue afs_bioqueue;
struct buf *afs_busyq = NULL;
struct buf *afs_asyncbuf;
afs_int32 afs_biodcnt = 0;

/* in implementing this, I assumed that all external linked lists were
 * null-terminated.  
 *
 * Several places in this code traverse a linked list.  The algorithm
 * used here is probably unfamiliar to most people.  Careful examination
 * will show that it eliminates an assignment inside the loop, as compared
 * to the standard algorithm, at the cost of occasionally using an extra
 * variable.
 */

/* get_bioreq()
 *
 * This function obtains, and returns, a pointer to a buffer for
 * processing by a daemon.  It sleeps until such a buffer is available.
 * The source of buffers for it is the list afs_asyncbuf (see also 
 * naix_vm_strategy).  This function may be invoked concurrently by
 * several processes, that is, several instances of the same daemon.
 * naix_vm_strategy, which adds buffers to the list, runs at interrupt
 * level, while get_bioreq runs at process level.
 *
 * The common kernel paradigm of sleeping and waking up, in which all the
 * competing processes sleep waiting for wakeups on one address, is not
 * followed here.  Instead, the following paradigm is used:  when a daemon
 * goes to sleep, it checks for other sleeping daemons.  If there aren't any,
 * it sleeps on the address of variable afs_asyncbuf.  But if there is
 * already a daemon sleeping on that address, it threads its own unique
 * address onto a list, and sleeps on that address.  This way, every 
 * sleeper is sleeping on a different address, and every wakeup wakes up
 * exactly one daemon.  This prevents a whole bunch of daemons from waking
 * up and then immediately having to go back to sleep.  This provides a
 * performance gain and makes the I/O scheduling a bit more deterministic.
 * The list of sleepers is variable afs_bioqueue.  The unique address
 * on which to sleep is passed to get_bioreq as its parameter.
 */
/*static*/ struct buf *afs_get_bioreq(self)
    struct afs_bioqueue *self;      /* address on which to sleep */

{
    struct buf *bp = (struct buf *) 0;
    struct buf *bestbp;
    struct buf **bestlbpP, **lbpP;
    int bestage, stop;
    struct buf *t1P, *t2P;      /* temp pointers for list manipulation */
    int oldPriority;
    afs_uint32 wait_ret;
struct afs_bioqueue *s;

    /* ??? Does the forward pointer of the returned buffer need to be NULL?
    */
    
	/* Disable interrupts from the strategy function, and save the 
	 * prior priority level
	 */
	oldPriority = i_disable ( INTMAX ) ;

	/* Each iteration of following loop either pulls
	 * a buffer off afs_asyncbuf, or sleeps.  
	 */
	while (1) {   /* inner loop */
		if (afs_asyncbuf) {
			/* look for oldest buffer */
			bp = bestbp = afs_asyncbuf;
			bestage = (int) bestbp->av_back;
			bestlbpP = &afs_asyncbuf;
			while (1) {
				lbpP = &bp->av_forw;
				bp = *lbpP;
				if (!bp) break;
				if ((int) bp->av_back - bestage < 0) {
					bestbp = bp;
					bestlbpP = lbpP;
					bestage = (int) bp->av_back;
				}
			}
			bp = bestbp;
			*bestlbpP = bp->av_forw;
			break;  
			}
		else {
		        int interrupted;

		/* If afs_asyncbuf is null, it is necessary to go to sleep.
		 * There are two possibilities:  either there is already a
		 * daemon that is sleeping on the address of afs_asyncbuf,
		 * or there isn't. 
		 */
			if (afs_bioqueue.sleeper) {
				/* enqueue */
				QAdd (&(afs_bioqueue.lruq), &(self->lruq));
				interrupted = sleep ((caddr_t) self, PCATCH|(PZERO + 1));
				if (self->lruq.next != &self->lruq) {	/* XXX ##3 XXX */
				    QRemove (&(self->lruq));		/* dequeue */
				}
self->cnt++;
				afs_bioqueue.sleeper = FALSE;
				if (interrupted) {
				    /* re-enable interrupts from strategy */
				    i_enable (oldPriority);
				    return(NULL);
				}
				continue;
			} else {
				afs_bioqueue.sleeper = TRUE;
				interrupted = sleep ((caddr_t) &afs_asyncbuf, PCATCH|(PZERO + 1));
				afs_bioqueue.sleeper = FALSE;
				if (interrupted)
				{
				    /*
				     * We need to wakeup another daemon if present
				     * since we were waiting on afs_asyncbuf.
				     */
#ifdef	notdef	/* The following doesn't work as advertised */				    
				     if (afs_bioqueue.lruq.next != &afs_bioqueue.lruq)
				     {
					 struct squeue *bq = afs_bioqueue.lruq.next;
					 QRemove (bq);
					 wakeup (bq);
				     }
#endif
				    /* re-enable interrupts from strategy */
				     i_enable (oldPriority);
				     return(NULL);
				 }
				continue;
				}

			}  /* end of "else asyncbuf is empty" */
		} /* end of "inner loop" */

	/*assert (bp);*/

	i_enable (oldPriority);     /* re-enable interrupts from strategy */

	/* For the convenience of other code, replace the gnodes in
	 * the b_vp field of bp and the other buffers on the b_work
	 * chain with the corresponding vnodes.   
	 *
	 * ??? what happens to the gnodes?  They're not just cut loose,
	 * are they?
	 */
	for(t1P=bp;;) {
		t2P = (struct buf *) t1P->b_work;
		t1P->b_vp = ((struct gnode *) t1P->b_vp)->gn_vnode;
		if (!t2P) 
			break;

		t1P = (struct buf *) t2P->b_work;
		t2P->b_vp = ((struct gnode *) t2P->b_vp)->gn_vnode;
		if (!t1P)
			break;
		}

	/* If the buffer does not specify I/O, it may immediately
	 * be returned to the caller.  This condition is detected
	 * by examining the buffer's flags (the b_flags field).  If
	 * the B_PFPROT bit is set, the buffer represents a protection
	 * violation, rather than a request for I/O.  The remainder
	 * of the outer loop handles the case where the B_PFPROT bit is clear.
	 */
	 if (bp->b_flags & B_PFPROT)  {
		return (bp);
	    }

	/* wake up another process to handle the next buffer, and return
	 * bp to the caller.
	 */
	oldPriority = i_disable ( INTMAX ) ;

	/* determine where to find the sleeping process. 
	 * There are two cases: either it is sleeping on
	 * afs_asyncbuf, or it is sleeping on its own unique
	 * address.  These cases are distinguished by examining
	 * the sleeper field of afs_bioqueue.
	 */
	if (afs_bioqueue.sleeper) {
		wakeup (&afs_asyncbuf);
		}
	else {
		if (afs_bioqueue.lruq.next == &afs_bioqueue.lruq) {
			/* queue is empty, what now? ???*/
			/* Should this be impossible, or does    */
			/* it just mean that nobody is sleeping? */;
			}
		else {
			struct squeue *bq = afs_bioqueue.lruq.next;
			QRemove (bq);
			QInit (bq);
			wakeup (bq);
			afs_bioqueue.sleeper = TRUE; 
			}
		}
	i_enable (oldPriority);     /* re-enable interrupts from strategy */
	return (bp);

} /* end of function get_bioreq() */


/* afs_BioDaemon
 *
 * This function is the daemon.  It is called from the syscall
 * interface.  Ordinarily, a script or an administrator will run a
 * daemon startup utility, specifying the number of I/O daemons to
 * run.  The utility will fork off that number of processes,
 * each making the appropriate syscall, which will cause this
 * function to be invoked.
 */
static int afs_initbiod = 0;		/* this is self-initializing code */
int DOvmlock = 0;
afs_BioDaemon (nbiods)
    afs_int32 nbiods;
{
    struct afs_bioqueue *self;
    afs_int32 code, s, pflg = 0;
    label_t jmpbuf;
    struct buf *bp, *bp1, *tbp1, *tbp2;		/* temp pointers only */
    caddr_t tmpaddr;
    struct vnode *vp;
    struct vcache *vcp;
    char tmperr;
    if (!afs_initbiod) {
	/* XXX ###1 XXX */
	afs_initbiod = 1;
	/* Initialize the queue of waiting processes, afs_bioqueue.  */
	QInit (&(afs_bioqueue.lruq));           
    }

    /* establish ourself as a kernel process so shutdown won't kill us */
/*    u.u_procp->p_flag |= SKPROC;*/

    /* Initialize a token (self) to use in the queue of sleeping processes.   */
    self = (struct afs_bioqueue *) afs_osi_Alloc (sizeof (struct afs_bioqueue));
    pin (self, sizeof (struct afs_bioqueue)); /* fix in memory */
    memset(self, 0, sizeof(*self));
    QInit (&(self->lruq));		/* initialize queue entry pointers */


    /* Ignore HUP signals... */
#ifdef AFS_AIX41_ENV
    {
 	sigset_t sigbits, osigbits;
 	/*
 	 * add SIGHUP to the set of already masked signals
 	 */
 	SIGFILLSET(sigbits);			/* allow all signals	*/
 	SIGDELSET(sigbits, SIGHUP);		/*   except SIGHUP	*/
 	limit_sigs(&sigbits, &osigbits);	/*   and already masked */
    }
#else
    SIGDELSET(u.u_procp->p_sig, SIGHUP);
    SIGADDSET(u.u_procp->p_sigignore, SIGHUP);
    SIGDELSET(u.u_procp->p_sigcatch, SIGHUP);
#endif
    /* Main body starts here -- this is an intentional infinite loop, and
     * should NEVER exit 
     *
     * Now, the loop will exit if get_bioreq() returns NULL, indicating 
     * that we've been interrupted.
     */
    while (1) {
	bp = afs_get_bioreq(self);
	if (!bp)
	    break;	/* we were interrupted */
	if (code = setjmpx(&jmpbuf)) {
	    /* This should not have happend, maybe a lack of resources  */
	    s = splimp();
	    for (bp1 = bp; bp ; bp = bp1) {
		if (bp1)
		    bp1 = bp1->b_work;
		bp->b_actf = 0;
		bp->b_error = code;
		bp->b_flags |= B_ERROR;
		iodone(bp);
	    }
	    splx(s);
	    continue;
	}
	vcp = (struct vcache *)bp->b_vp;
	if (bp->b_flags & B_PFSTORE) {
	    ObtainWriteLock(&vcp->lock,210);	    
	    if (vcp->v.v_gnode->gn_mwrcnt) {
		if (vcp->m.Length < bp->b_bcount + (u_int)dbtob(bp->b_blkno))
		    vcp->m.Length = bp->b_bcount + (u_int)dbtob(bp->b_blkno);
	    }
	    ReleaseWriteLock(&vcp->lock);
	}
	/* If the buffer represents a protection violation, rather than
	 * an actual request for I/O, no special action need be taken.  
	 */
	if ( bp->b_flags & B_PFPROT ) {   
	    iodone (bp);    /* Notify all users of the buffer that we're done */
	    continue;
        }
if (DOvmlock)
	ObtainWriteLock(&vcp->pvmlock,558);     
	/*
	 * First map its data area to a region in the current address space
	 * by calling vm_att with the subspace identifier, and a pointer to
	 * the data area.  vm_att returns  a new data area pointer, but we
	 * also want to hang onto the old one.
	 */
	tmpaddr = bp->b_baddr;
	bp->b_baddr = vm_att (bp->b_xmemd.subspace_id, tmpaddr);
	tmperr = afs_ustrategy(bp);	/* temp variable saves offset calculation */
	if (tmperr) {			/* in non-error case */
	    bp->b_flags |= B_ERROR;		/* should other flags remain set ??? */
	    bp->b_error = tmperr;
	}

	/* Unmap the buffer's data area by calling vm_det.  Reset data area
	 * to the value that we saved above.
	 */
	vm_det(bp->b_un.b_addr);
	bp->b_baddr = tmpaddr;

	/*
	 * buffer may be linked with other buffers via the b_work field.
	 * See also naix_vm_strategy.  For each buffer in the chain (including
	 * bp) notify all users of the buffer that the daemon is finished
	 * using it by calling iodone.  
	 * assumes iodone can modify the b_work field.
	 */
	for(tbp1=bp;;) {
	    tbp2 = (struct buf *) tbp1->b_work;
	    iodone(tbp1);
	    if (!tbp2) 
		break;

	    tbp1 = (struct buf *) tbp2->b_work;
	    iodone(tbp2);
	    if (!tbp1)
		break;
	}
if (DOvmlock)
	ReleaseWriteLock(&vcp->pvmlock);     /* Unlock the vnode.  */
	clrjmpx(&jmpbuf);
    } /* infinite loop (unless we're interrupted) */
    unpin (self, sizeof (struct afs_bioqueue));
    afs_osi_Free (self, sizeof (struct afs_bioqueue));
} /* end of afs_BioDaemon() */
#endif /* AFS_AIX41_ENV */ 
#endif /* AFS_AIX32_ENV */


int afs_nbrs = 0;
void afs_BackgroundDaemon() {
    struct brequest *tb;
    int i, foundAny;
    afs_int32 pid;

    AFS_STATCNT(afs_BackgroundDaemon);
    /* initialize subsystem */
    if (brsInit == 0) {
	LOCK_INIT(&afs_xbrs, "afs_xbrs");
	memset((char *)afs_brs, 0, sizeof(afs_brs));
	brsInit = 1;
#if defined (AFS_SGI_ENV) && defined(AFS_SGI_SHORTSTACK)
        /*
         * steal the first daemon for doing delayed DSlot flushing
         * (see afs_GetDownDSlot)
         */
	AFS_GUNLOCK();
        afs_sgidaemon();
        return;
#endif
    }
    afs_nbrs++;

    MObtainWriteLock(&afs_xbrs,302);
    while (1) {
	if (afs_termState == AFSOP_STOP_BKG) {
	    if (--afs_nbrs <= 0)
		afs_termState = AFSOP_STOP_TRUNCDAEMON;
	    MReleaseWriteLock(&afs_xbrs);
	    afs_osi_Wakeup(&afs_termState);
	    return;
	}
	
	/* find a request */
	tb = afs_brs;
	foundAny = 0;
	for(i=0;i<NBRS;i++,tb++) {
	    /* look for request */
	    if ((tb->refCount > 0) && !(tb->flags & BSTARTED)) {
		/* new request, not yet picked up */
		tb->flags |= BSTARTED;
		MReleaseWriteLock(&afs_xbrs);
		foundAny = 1;
		afs_Trace1(afs_iclSetp, CM_TRACE_BKG1,
			   ICL_TYPE_INT32, tb->opcode);
		if (tb->opcode == BOP_FETCH)
		    BPrefetch(tb);
		else if (tb->opcode == BOP_STORE)
		    BStore(tb);
		else if (tb->opcode == BOP_PATH)
		    BPath(tb);
		else panic("background bop");
		if (tb->vnode) {
#ifdef	AFS_DEC_ENV
		    tb->vnode->vrefCount--;	    /* fix up reference count */
#else
		    AFS_RELE((struct vnode *)(tb->vnode));	/* MUST call vnode layer or could lose vnodes */
#endif
		    tb->vnode = (struct vcache *) 0;
		}
		if (tb->cred) {
		    crfree(tb->cred);
		    tb->cred = (struct AFS_UCRED *) 0;
		}
		afs_BRelease(tb);   /* this grabs and releases afs_xbrs lock */
		MObtainWriteLock(&afs_xbrs,305);
	    }
	}
	if (!foundAny) {
	    /* wait for new request */
	    afs_brsDaemons++;
	    MReleaseWriteLock(&afs_xbrs);
	    afs_osi_Sleep(&afs_brsDaemons);
	    MObtainWriteLock(&afs_xbrs,307);
	    afs_brsDaemons--;
	}
    }
}


void shutdown_daemons()
{
  extern int afs_cold_shutdown;
  register int i;
  register struct brequest *tb;

  AFS_STATCNT(shutdown_daemons);
  if (afs_cold_shutdown) {
      afs_brsDaemons = brsInit = 0;
      rxepoch_checked = afs_nbrs = 0;
      memset((char *)afs_brs, 0, sizeof(afs_brs));
      memset((char *)&afs_xbrs, 0, sizeof(afs_lock_t));
      afs_brsWaiters = 0;
#ifdef AFS_AIX32_ENV
#ifdef AFS_AIX41_ENV
      lock_free(&afs_asyncbuf_lock);
      unpin(&afs_asyncbuf, sizeof(struct buf*));
      pin (&afs_asyncbuf_cv, sizeof(afs_int32));
#else /* AFS_AIX41_ENV */
      afs_busyq = NULL;
      afs_biodcnt = 0;
      memset((char *)&afs_bioqueue, 0, sizeof(struct afs_bioqueue));
#endif
      afs_initbiod = 0;
#endif
  }
}

#if defined(AFS_SGI_ENV) && defined(AFS_SGI_SHORTSTACK)
/*
 * sgi - daemon - handles certain operations that otherwise
 * would use up too much kernel stack space
 *
 * This all assumes that since the caller must have the xdcache lock
 * exclusively that the list will never be more than one long
 * and noone else can attempt to add anything until we're done.
 */
SV_TYPE afs_sgibksync;
SV_TYPE afs_sgibkwait;
lock_t afs_sgibklock;
struct dcache *afs_sgibklist;

int
afs_sgidaemon(void)
{
	int s;
	struct dcache *tdc;

	if (afs_sgibklock == NULL) {
		SV_INIT(&afs_sgibksync, "bksync", 0, 0);
		SV_INIT(&afs_sgibkwait, "bkwait", 0, 0);
		SPINLOCK_INIT(&afs_sgibklock, "bklock");
	}
	s = SPLOCK(afs_sgibklock);
	for (;;) {
		/* wait for something to do */
		SP_WAIT(afs_sgibklock, s, &afs_sgibksync, PINOD);
		osi_Assert(afs_sgibklist);

		/* XX will probably need to generalize to real list someday */
		s = SPLOCK(afs_sgibklock);
		while (afs_sgibklist) {
			tdc = afs_sgibklist;
			afs_sgibklist = NULL;
			SPUNLOCK(afs_sgibklock, s);
			AFS_GLOCK();
			tdc->flags &= ~DFEntryMod;
			afs_WriteDCache(tdc, 1);
			AFS_GUNLOCK();
			s = SPLOCK(afs_sgibklock);
		}

		/* done all the work - wake everyone up */
		while (SV_SIGNAL(&afs_sgibkwait))
			;
	}
}
#endif
