/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 *
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * Implements:
 * afs_FlushVCache
 * afs_AllocCBR
 * afs_FreeCBR
 * afs_FlushVCBs
 * afs_QueueVCB
 * afs_RemoveVCB
 * afs_NewVCache
 * afs_FlushActiveVcaches
 * afs_VerifyVCache2
 * afs_WriteVCache
 * afs_SimpleVStat
 * afs_ProcessFS
 * TellALittleWhiteLie
 * afs_RemoteLookup
 * afs_GetVCache
 * afs_LookupVCache
 * afs_GetRootVCache
 * afs_FetchStatus
 * afs_StuffVcache
 * afs_PutVCache
 * afs_FindVCache
 * afs_NFSFindVCache
 * afs_vcacheInit
 * shutdown_vcache
 *
 */
#include <afsconfig.h>
#include "afs/param.h"

RCSID
    ("$Header: /cvs/openafs/src/afs/afs_vcache.c,v 1.65.2.7 2005/01/31 04:18:56 shadow Exp $");

#include "afs/sysincludes.h"	/*Standard vendor system headers */
#include "afsincludes.h"	/*AFS-based standard headers */
#include "afs/afs_stats.h"
#include "afs/afs_cbqueue.h"
#include "afs/afs_osidnlc.h"

#ifdef AFS_OSF_ENV
afs_int32 afs_maxvcount = 0;	/* max number of vcache entries */
afs_int32 afs_vcount = 0;	/* number of vcache in use now */
#endif /* AFS_OSF_ENV */

#ifdef AFS_SGI_ENV
int afsvnumbers = 0;
#endif

#ifdef AFS_SGI64_ENV
char *makesname();
#endif /* AFS_SGI64_ENV */

/* Exported variables */
afs_rwlock_t afs_xvcache;	/*Lock: alloc new stat cache entries */
afs_lock_t afs_xvcb;		/*Lock: fids on which there are callbacks */
struct vcache *freeVCList;	/*Free list for stat cache entries */
struct vcache *Initial_freeVCList;	/*Initial list for above */
struct afs_q VLRU;		/*vcache LRU */
afs_int32 vcachegen = 0;
unsigned int afs_paniconwarn = 0;
struct vcache *afs_vhashT[VCSIZE];
static struct afs_cbr *afs_cbrHashT[CBRSIZE];
afs_int32 afs_bulkStatsLost;
int afs_norefpanic = 0;

/* Forward declarations */
static afs_int32 afs_QueueVCB(struct vcache *avc);

/*
 * afs_HashCBRFid
 *
 * Generate an index into the hash table for a given Fid.
 */
static int
afs_HashCBRFid(struct AFSFid *fid)
{
    return (fid->Volume + fid->Vnode + fid->Unique) % CBRSIZE;
}

/*
 * afs_InsertHashCBR
 *
 * Insert a CBR entry into the hash table.
 * Must be called with afs_xvcb held.
 */
static void
afs_InsertHashCBR(struct afs_cbr *cbr)
{
    int slot = afs_HashCBRFid(&cbr->fid);

    cbr->hash_next = afs_cbrHashT[slot];
    if (afs_cbrHashT[slot])
	afs_cbrHashT[slot]->hash_pprev = &cbr->hash_next;

    cbr->hash_pprev = &afs_cbrHashT[slot];
    afs_cbrHashT[slot] = cbr;
}

/*
 * afs_FlushVCache
 *
 * Description:
 *	Flush the given vcache entry.
 *
 * Parameters:
 *	avc : Pointer to vcache entry to flush.
 *	slept : Pointer to int to set 1 if we sleep/drop locks, 0 if we don't.
 *
 * Environment:
 *	afs_xvcache lock must be held for writing upon entry to
 *	prevent people from changing the vrefCount field, and to
 *      protect the lruq and hnext fields.
 * LOCK: afs_FlushVCache afs_xvcache W
 * REFCNT: vcache ref count must be zero on entry except for osf1
 * RACE: lock is dropped and reobtained, permitting race in caller
 */

int
afs_FlushVCache(struct vcache *avc, int *slept)
{				/*afs_FlushVCache */

    register afs_int32 i, code;
    register struct vcache **uvc, *wvc;

    *slept = 0;
    AFS_STATCNT(afs_FlushVCache);
    afs_Trace2(afs_iclSetp, CM_TRACE_FLUSHV, ICL_TYPE_POINTER, avc,
	       ICL_TYPE_INT32, avc->states);
#ifdef  AFS_OSF_ENV
    AFS_GUNLOCK();
    VN_LOCK(AFSTOV(avc));
    AFS_GLOCK();
#endif

    code = osi_VM_FlushVCache(avc, slept);
    if (code)
	goto bad;

    if (avc->states & CVFlushed) {
	code = EBUSY;
	goto bad;
    }
    if (avc->nextfree || !avc->vlruq.prev || !avc->vlruq.next) {	/* qv afs.h */
	refpanic("LRU vs. Free inconsistency");
    }
    avc->states |= CVFlushed;
    /* pull the entry out of the lruq and put it on the free list */
    QRemove(&avc->vlruq);
    avc->vlruq.prev = avc->vlruq.next = (struct afs_q *)0;

    /* keep track of # of files that we bulk stat'd, but never used
     * before they got recycled.
     */
    if (avc->states & CBulkStat)
	afs_bulkStatsLost++;
    vcachegen++;
    /* remove entry from the hash chain */
    i = VCHash(&avc->fid);
    uvc = &afs_vhashT[i];
    for (wvc = *uvc; wvc; uvc = &wvc->hnext, wvc = *uvc) {
	if (avc == wvc) {
	    *uvc = avc->hnext;
	    avc->hnext = (struct vcache *)NULL;
	    break;
	}
    }
    if (!wvc)
	osi_Panic("flushvcache");	/* not in correct hash bucket */
    if (avc->mvid)
	osi_FreeSmallSpace(avc->mvid);
    avc->mvid = (struct VenusFid *)0;
    if (avc->linkData) {
	afs_osi_Free(avc->linkData, strlen(avc->linkData) + 1);
	avc->linkData = NULL;
    }
#if defined(AFS_XBSD_ENV)
    /* OK, there are no internal vrefCounts, so there shouldn't
     * be any more refs here. */
    if (avc->v) {
	avc->v->v_data = NULL;	/* remove from vnode */
	avc->v = NULL;		/* also drop the ptr to vnode */
    }
#endif
    afs_FreeAllAxs(&(avc->Access));

    /* we can't really give back callbacks on RO files, since the
     * server only tracks them on a per-volume basis, and we don't
     * know whether we still have some other files from the same
     * volume. */
    if ((avc->states & CRO) == 0 && avc->callback) {
	afs_QueueVCB(avc);
    }
    ObtainWriteLock(&afs_xcbhash, 460);
    afs_DequeueCallback(avc);	/* remove it from queued callbacks list */
    avc->states &= ~(CStatd | CUnique);
    ReleaseWriteLock(&afs_xcbhash);
    afs_symhint_inval(avc);
    if ((avc->states & CForeign) || (avc->fid.Fid.Vnode & 1))
	osi_dnlc_purgedp(avc);	/* if it (could be) a directory */
    else
	osi_dnlc_purgevp(avc);

    /*
     * Next, keep track of which vnodes we've deleted for create's
     * optimistic synchronization algorithm
     */
    afs_allZaps++;
    if (avc->fid.Fid.Vnode & 1)
	afs_oddZaps++;
    else
	afs_evenZaps++;

#if	!defined(AFS_OSF_ENV)
    /* put the entry in the free list */
    avc->nextfree = freeVCList;
    freeVCList = avc;
    if (avc->vlruq.prev || avc->vlruq.next) {
	refpanic("LRU vs. Free inconsistency");
    }
#else
    /* This should put it back on the vnode free list since usecount is 1 */
    afs_vcount--;
    vSetType(avc, VREG);
    if (VREFCOUNT(avc) > 0) {
	VN_UNLOCK(AFSTOV(avc));
	AFS_RELE(AFSTOV(avc));
    } else {
	if (afs_norefpanic) {
	    printf("flush vc refcnt < 1");
	    afs_norefpanic++;
	    (void)vgone(avc, VX_NOSLEEP, NULL);
	    AFS_GLOCK();
	    VN_UNLOCK(AFSTOV(avc));
	} else
	    osi_Panic("flush vc refcnt < 1");
    }
#endif /* AFS_OSF_ENV */
    avc->states |= CVFlushed;
    return 0;

  bad:
#ifdef	AFS_OSF_ENV
    VN_UNLOCK(AFSTOV(avc));
#endif
    return code;

}				/*afs_FlushVCache */

#ifndef AFS_SGI_ENV
/*
 * afs_InactiveVCache
 *
 * The core of the inactive vnode op for all but IRIX.
 */
void
afs_InactiveVCache(struct vcache *avc, struct AFS_UCRED *acred)
{
    AFS_STATCNT(afs_inactive);
    if (avc->states & CDirty) {
	/* we can't keep trying to push back dirty data forever.  Give up. */
	afs_InvalidateAllSegments(avc);	/* turns off dirty bit */
    }
    avc->states &= ~CMAPPED;	/* mainly used by SunOS 4.0.x */
    avc->states &= ~CDirty;	/* Turn it off */
    if (avc->states & CUnlinked) {
	if (CheckLock(&afs_xvcache) || CheckLock(&afs_xdcache)) {
	    avc->states |= CUnlinkedDel;
	    return;
	}
	afs_remunlink(avc, 1);	/* ignore any return code */
    }

}
#endif

/*
 * afs_AllocCBR
 *
 * Description: allocate a callback return structure from the
 * free list and return it.
 *
 * Env: The alloc and free routines are both called with the afs_xvcb lock
 * held, so we don't have to worry about blocking in osi_Alloc.
 */
static struct afs_cbr *afs_cbrSpace = 0;
struct afs_cbr *
afs_AllocCBR(void)
{
    register struct afs_cbr *tsp;
    int i;

    while (!afs_cbrSpace) {
	if (afs_stats_cmperf.CallBackAlloced >= 2) {
	    /* don't allocate more than 2 * AFS_NCBRS for now */
	    afs_FlushVCBs(0);
	    afs_stats_cmperf.CallBackFlushes++;
	} else {
	    /* try allocating */
	    tsp =
		(struct afs_cbr *)afs_osi_Alloc(AFS_NCBRS *
						sizeof(struct afs_cbr));
	    for (i = 0; i < AFS_NCBRS - 1; i++) {
		tsp[i].next = &tsp[i + 1];
	    }
	    tsp[AFS_NCBRS - 1].next = 0;
	    afs_cbrSpace = tsp;
	    afs_stats_cmperf.CallBackAlloced++;
	}
    }
    tsp = afs_cbrSpace;
    afs_cbrSpace = tsp->next;
    return tsp;
}

/*
 * afs_FreeCBR
 *
 * Description: free a callback return structure, removing it from all lists.
 *
 * Parameters:
 *	asp -- the address of the structure to free.
 *
 * Environment: the xvcb lock is held over these calls.
 */
int
afs_FreeCBR(register struct afs_cbr *asp)
{
    *(asp->pprev) = asp->next;
    if (asp->next)
	asp->next->pprev = asp->pprev;

    *(asp->hash_pprev) = asp->hash_next;
    if (asp->hash_next)
	asp->hash_next->hash_pprev = asp->hash_pprev;

    asp->next = afs_cbrSpace;
    afs_cbrSpace = asp;
    return 0;
}

/*
 * afs_FlushVCBs
 *
 * Description: flush all queued callbacks to all servers.
 *
 * Parameters: none.
 *
 * Environment: holds xvcb lock over RPC to guard against race conditions
 *	when a new callback is granted for the same file later on.
 */
afs_int32
afs_FlushVCBs(afs_int32 lockit)
{
    struct AFSFid *tfids;
    struct AFSCallBack callBacks[1];
    struct AFSCBFids fidArray;
    struct AFSCBs cbArray;
    afs_int32 code;
    struct afs_cbr *tcbrp;
    int tcount;
    struct server *tsp;
    int i;
    struct vrequest treq;
    struct conn *tc;
    int safety1, safety2, safety3;
    XSTATS_DECLS;
    if ((code = afs_InitReq(&treq, afs_osi_credp)))
	return code;
    treq.flags |= O_NONBLOCK;
    tfids = afs_osi_Alloc(sizeof(struct AFSFid) * AFS_MAXCBRSCALL);

    if (lockit)
	MObtainWriteLock(&afs_xvcb, 273);
    ObtainReadLock(&afs_xserver);
    for (i = 0; i < NSERVERS; i++) {
	for (safety1 = 0, tsp = afs_servers[i];
	     tsp && safety1 < afs_totalServers + 10;
	     tsp = tsp->next, safety1++) {
	    /* don't have any */
	    if (tsp->cbrs == (struct afs_cbr *)0)
		continue;

	    /* otherwise, grab a block of AFS_MAXCBRSCALL from the list
	     * and make an RPC, over and over again.
	     */
	    tcount = 0;		/* number found so far */
	    for (safety2 = 0; safety2 < afs_cacheStats; safety2++) {
		if (tcount >= AFS_MAXCBRSCALL || !tsp->cbrs) {
		    /* if buffer is full, or we've queued all we're going
		     * to from this server, we should flush out the
		     * callbacks.
		     */
		    fidArray.AFSCBFids_len = tcount;
		    fidArray.AFSCBFids_val = (struct AFSFid *)tfids;
		    cbArray.AFSCBs_len = 1;
		    cbArray.AFSCBs_val = callBacks;
		    memset(&callBacks[0], 0, sizeof(callBacks[0]));
		    callBacks[0].CallBackType = CB_EXCLUSIVE;
		    for (safety3 = 0; safety3 < MAXHOSTS * 2; safety3++) {
			tc = afs_ConnByHost(tsp, tsp->cell->fsport,
					    tsp->cell->cellNum, &treq, 0,
					    SHARED_LOCK);
			if (tc) {
			    XSTATS_START_TIME
				(AFS_STATS_FS_RPCIDX_GIVEUPCALLBACKS);
			    RX_AFS_GUNLOCK();
			    code =
				RXAFS_GiveUpCallBacks(tc->id, &fidArray,
						      &cbArray);
			    RX_AFS_GLOCK();
			    XSTATS_END_TIME;
			} else
			    code = -1;
			if (!afs_Analyze
			    (tc, code, 0, &treq,
			     AFS_STATS_FS_RPCIDX_GIVEUPCALLBACKS, SHARED_LOCK,
			     tsp->cell)) {
			    break;
			}
		    }
		    /* ignore return code, since callbacks may have
		     * been returned anyway, we shouldn't leave them
		     * around to be returned again.
		     *
		     * Next, see if we are done with this server, and if so,
		     * break to deal with the next one.
		     */
		    if (!tsp->cbrs)
			break;
		    tcount = 0;
		}
		/* if to flush full buffer */
		/* if we make it here, we have an entry at the head of cbrs,
		 * which we should copy to the file ID array and then free.
		 */
		tcbrp = tsp->cbrs;
		tfids[tcount++] = tcbrp->fid;

		/* Freeing the CBR will unlink it from the server's CBR list */
		afs_FreeCBR(tcbrp);
	    }			/* while loop for this one server */
	    if (safety2 > afs_cacheStats) {
		afs_warn("possible internal error afs_flushVCBs (%d)\n",
			 safety2);
	    }
	}			/* for loop for this hash chain */
    }				/* loop through all hash chains */
    if (safety1 > afs_totalServers + 2) {
	afs_warn
	    ("AFS internal error (afs_flushVCBs) (%d > %d), continuing...\n",
	     safety1, afs_totalServers + 2);
	if (afs_paniconwarn)
	    osi_Panic("afs_flushVCBS safety1");
    }

    ReleaseReadLock(&afs_xserver);
    if (lockit)
	MReleaseWriteLock(&afs_xvcb);
    afs_osi_Free(tfids, sizeof(struct AFSFid) * AFS_MAXCBRSCALL);
    return 0;
}

/*
 * afs_QueueVCB
 *
 * Description:
 *	Queue a callback on the given fid.
 *
 * Parameters:
 *	avc: vcache entry
 *
 * Environment:
 *	Locks the xvcb lock.
 *	Called when the xvcache lock is already held.
 */

static afs_int32
afs_QueueVCB(struct vcache *avc)
{
    struct server *tsp;
    struct afs_cbr *tcbp;

    AFS_STATCNT(afs_QueueVCB);
    /* The callback is really just a struct server ptr. */
    tsp = (struct server *)(avc->callback);

    /* we now have a pointer to the server, so we just allocate
     * a queue entry and queue it.
     */
    MObtainWriteLock(&afs_xvcb, 274);
    tcbp = afs_AllocCBR();
    tcbp->fid = avc->fid.Fid;

    tcbp->next = tsp->cbrs;
    if (tsp->cbrs)
	tsp->cbrs->pprev = &tcbp->next;

    tsp->cbrs = tcbp;
    tcbp->pprev = &tsp->cbrs;

    afs_InsertHashCBR(tcbp);

    /* now release locks and return */
    MReleaseWriteLock(&afs_xvcb);
    return 0;
}


/*
 * afs_RemoveVCB
 *
 * Description:
 *	Remove a queued callback for a given Fid.
 *
 * Parameters:
 *	afid: The fid we want cleansed of queued callbacks.
 *
 * Environment:
 *	Locks xvcb and xserver locks.
 *	Typically called with xdcache, xvcache and/or individual vcache
 *	entries locked.
 */

void
afs_RemoveVCB(struct VenusFid *afid)
{
    int slot;
    struct afs_cbr *cbr, *ncbr;

    AFS_STATCNT(afs_RemoveVCB);
    MObtainWriteLock(&afs_xvcb, 275);

    slot = afs_HashCBRFid(&afid->Fid);
    ncbr = afs_cbrHashT[slot];

    while (ncbr) {
	cbr = ncbr;
	ncbr = cbr->hash_next;

	if (afid->Fid.Volume == cbr->fid.Volume &&
	    afid->Fid.Vnode == cbr->fid.Vnode &&
	    afid->Fid.Unique == cbr->fid.Unique) {
	    afs_FreeCBR(cbr);
	}
    }

    MReleaseWriteLock(&afs_xvcb);
}

#if defined(AFS_LINUX22_ENV) && !defined(AFS_LINUX26_ENV)

static void
__shrink_dcache_parent(struct dentry *parent)
{
    struct dentry *this_parent = parent;
    struct list_head *next;
    int found = 0;
    LIST_HEAD(afs_dentry_unused);

  repeat:
    next = this_parent->d_subdirs.next;
  resume:
    while (next != &this_parent->d_subdirs) {
	struct list_head *tmp = next;
	struct dentry *dentry = list_entry(tmp, struct dentry, d_child);
	next = tmp->next;
	if (!DCOUNT(dentry)) {
	    list_del(&dentry->d_lru);
	    list_add(&dentry->d_lru, afs_dentry_unused.prev);
	    found++;
	}
	/*
	 * Descend a level if the d_subdirs list is non-empty.
	 */
	if (!list_empty(&dentry->d_subdirs)) {
	    this_parent = dentry;
	    goto repeat;
	}
    }
    /*
     * All done at this level ... ascend and resume the search.
     */
    if (this_parent != parent) {
	next = this_parent->d_child.next;
	this_parent = this_parent->d_parent;
	goto resume;
    }

    for (;;) {
	struct dentry *dentry;
	struct list_head *tmp;

	tmp = afs_dentry_unused.prev;

	if (tmp == &afs_dentry_unused)
	    break;
#ifdef AFS_LINUX24_ENV
	list_del_init(tmp);
#else
	list_del(tmp);
	INIT_LIST_HEAD(tmp);
#endif /* AFS_LINUX24_ENV */
	dentry = list_entry(tmp, struct dentry, d_lru);

#ifdef AFS_LINUX24_ENV
	/* Unused dentry with a count? */
	if (DCOUNT(dentry))
	    BUG();
#endif
	DGET(dentry);
#ifdef AFS_LINUX24_ENV
	list_del_init(&dentry->d_hash);	/* d_drop */
#else
	list_del(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_hash);
#endif /* AFS_LINUX24_ENV */
	DUNLOCK();
	dput(dentry);
	DLOCK();
	if (!--found)
	    break;
    }
}

/* afs_TryFlushDcacheChildren -- Shakes loose vcache references held by
 *                               children of the dentry
 *
 * LOCKS -- Called with afs_xvcache write locked. Drops and reaquires
 *          AFS_GLOCK, so it can call dput, which may call iput, but
 *          keeps afs_xvcache exclusively.
 *
 * Tree traversal algorithm from fs/dcache.c: select_parent()
 */
static void
afs_TryFlushDcacheChildren(struct vcache *tvc)
{
    struct inode *ip = AFSTOI(tvc);
    struct dentry *this_parent;
    struct list_head *next;
    struct list_head *cur;
    struct list_head *head = &ip->i_dentry;
    struct dentry *dentry;

    AFS_GUNLOCK();
  restart:
#ifndef old_vcache_scheme
    DLOCK();
    cur = head;
    while ((cur = cur->next) != head) {
	dentry = list_entry(cur, struct dentry, d_alias);

	if (!list_empty(&dentry->d_hash) && !list_empty(&dentry->d_subdirs))
	    __shrink_dcache_parent(dentry);

	if (!DCOUNT(dentry)) {
	    DGET(dentry);
#ifdef AFS_LINUX24_ENV
	    list_del_init(&dentry->d_hash);	/* d_drop */
#else
	    list_del(&dentry->d_hash);
	    INIT_LIST_HEAD(&dentry->d_hash);
#endif /* AFS_LINUX24_ENV */
	    DUNLOCK();
	    dput(dentry);
	    goto restart;
	}
    }
    DUNLOCK();
    AFS_GLOCK();
#else
  restart:
    DLOCK();
    cur = head;
    while ((cur = cur->next) != head) {
	dentry = list_entry(cur, struct dentry, d_alias);

	afs_Trace3(afs_iclSetp, CM_TRACE_TRYFLUSHDCACHECHILDREN,
		   ICL_TYPE_POINTER, ip, ICL_TYPE_STRING,
		   dentry->d_parent->d_name.name, ICL_TYPE_STRING,
		   dentry->d_name.name);

	if (!DCOUNT(dentry)) {
	    AFS_GUNLOCK();
	    DGET(dentry);
	    DUNLOCK();
	    d_drop(dentry);
	    dput(dentry);
	    AFS_GLOCK();
	    goto restart;
	}
    }
    DUNLOCK();
#endif
}
#endif /* AFS_LINUX22_ENV && !AFS_LINUX26_ENV */

/*
 * afs_NewVCache
 *
 * Description:
 *	This routine is responsible for allocating a new cache entry
 *	from the free list.  It formats the cache entry and inserts it
 *	into the appropriate hash tables.  It must be called with
 *	afs_xvcache write-locked so as to prevent several processes from
 *	trying to create a new cache entry simultaneously.
 *
 * Parameters:
 *	afid  : The file id of the file whose cache entry is being
 *		created.
 */
/* LOCK: afs_NewVCache  afs_xvcache W */
struct vcache *
afs_NewVCache(struct VenusFid *afid, struct server *serverp)
{
    struct vcache *tvc;
    afs_int32 i;
    afs_int32 anumber = VCACHE_FREE;
#ifdef	AFS_AIX_ENV
    struct gnode *gnodepnt;
#endif
#ifdef	AFS_MACH_ENV
    struct vm_info *vm_info_ptr;
#endif /* AFS_MACH_ENV */
#ifdef	AFS_OSF_ENV
    struct vcache *nvc;
#endif /* AFS_OSF_ENV */
    struct afs_q *tq, *uq;
    int code, fv_slept;

    AFS_STATCNT(afs_NewVCache);
#ifdef	AFS_OSF_ENV
#ifdef	AFS_OSF30_ENV
    if (afs_vcount >= afs_maxvcount) {
#else
    /*
     * If we are using > 33 % of the total system vnodes for AFS vcache
     * entries or we are using the maximum number of vcache entries,
     * then free some.  (if our usage is > 33% we should free some, if
     * our usage is > afs_maxvcount, set elsewhere to 0.5*nvnode,
     * we _must_ free some -- no choice).
     */
    if (((3 * afs_vcount) > nvnode) || (afs_vcount >= afs_maxvcount)) {
#endif
	struct afs_q *tq, *uq;
	int i;
	char *panicstr;

	i = 0;
	for (tq = VLRU.prev; tq != &VLRU && anumber > 0; tq = uq) {
	    tvc = QTOV(tq);
	    uq = QPrev(tq);
	    if (tvc->states & CVFlushed)
		refpanic("CVFlushed on VLRU");
	    else if (i++ > afs_maxvcount)
		refpanic("Exceeded pool of AFS vnodes(VLRU cycle?)");
	    else if (QNext(uq) != tq)
		refpanic("VLRU inconsistent");
	    else if (VREFCOUNT(tvc) < 1)
		refpanic("refcnt 0 on VLRU");

	    if (VREFCOUNT(tvc) == 1 && tvc->opens == 0
		&& (tvc->states & CUnlinkedDel) == 0) {
		code = afs_FlushVCache(tvc, &fv_slept);
		if (code == 0) {
		    anumber--;
		}
		if (fv_slept) {
		    uq = VLRU.prev;
		    i = 0;
		    continue;	/* start over - may have raced. */
		}
	    }
	    if (tq == uq)
		break;
	}
	if (anumber == VCACHE_FREE) {
	    printf("NewVCache: warning none freed, using %d of %d\n",
		   afs_vcount, afs_maxvcount);
	    if (afs_vcount >= afs_maxvcount) {
		osi_Panic("NewVCache - none freed");
		/* XXX instead of panicing, should do afs_maxvcount++
		 * and magic up another one */
	    }
	}
    }

    AFS_GUNLOCK();
    if (getnewvnode(MOUNT_AFS, &Afs_vnodeops, &nvc)) {
	/* What should we do ???? */
	osi_Panic("afs_NewVCache: no more vnodes");
    }
    AFS_GLOCK();

    tvc = nvc;
    tvc->nextfree = NULL;
    afs_vcount++;
#else /* AFS_OSF_ENV */
    /* pull out a free cache entry */
    if (!freeVCList) {
	i = 0;
	for (tq = VLRU.prev; (anumber > 0) && (tq != &VLRU); tq = uq) {
	    tvc = QTOV(tq);
	    uq = QPrev(tq);

	    if (tvc->states & CVFlushed) {
		refpanic("CVFlushed on VLRU");
	    } else if (i++ > 2 * afs_cacheStats) {	/* even allowing for a few xallocs... */
		refpanic("Increase -stat parameter of afsd(VLRU cycle?)");
	    } else if (QNext(uq) != tq) {
		refpanic("VLRU inconsistent");
	    }
#ifdef AFS_DARWIN_ENV
	    if ((VREFCOUNT(tvc) < DARWIN_REFBASE) ||
		(VREFCOUNT(tvc) < 1 + DARWIN_REFBASE &&
		 UBCINFOEXISTS(&tvc->v))) {
		VREFCOUNT_SET(tvc,
			      DARWIN_REFBASE +
			      (UBCINFOEXISTS(&tvc->v) ? 1 : 0));
	    }
	    if (tvc->opens == 0 && ((tvc->states & CUnlinkedDel) == 0)
		&& VREFCOUNT(tvc) == DARWIN_REFBASE + 1
		&& UBCINFOEXISTS(&tvc->v)) {
		osi_VM_TryReclaim(tvc, &fv_slept);
		if (fv_slept) {
		    uq = VLRU.prev;
		    i = 0;
		    continue;	/* start over - may have raced. */
		}
	    }
#elif defined(AFS_LINUX22_ENV)
	    if (tvc != afs_globalVp && VREFCOUNT(tvc) && tvc->opens == 0) {
#if defined(AFS_LINUX26_ENV)
                struct dentry *dentry;
                struct list_head *cur, *head = &(AFSTOI(tvc))->i_dentry;
                AFS_FAST_HOLD(tvc);
                AFS_GUNLOCK();

restart:
                spin_lock(&dcache_lock);
                cur = head;
                while ((cur = cur->next) != head) {
                    dentry = list_entry(cur, struct dentry, d_alias);

		    if (d_unhashed(dentry))
			continue;

		    dget_locked(dentry);

		    if (!list_empty(&dentry->d_subdirs)) {
                        DUNLOCK();
			shrink_dcache_parent(dentry);
                        DLOCK();
                    }

		    spin_lock(&dentry->d_lock);
		    if (atomic_read(&dentry->d_count) > 1) {
			if (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)) {
			    spin_unlock(&dentry->d_lock);
			    spin_unlock(&dcache_lock);
			    dput(dentry);
			    goto inuse;
			}
		    }

		    __d_drop(dentry);
		    spin_unlock(&dentry->d_lock);
		    spin_unlock(&dcache_lock);
		    dput(dentry);
		    goto restart;

                }
                DUNLOCK();
inuse:

                AFS_GLOCK();
                AFS_FAST_RELE(tvc);
#else
		afs_TryFlushDcacheChildren(tvc);
#endif
	    }
#endif

	    if (VREFCOUNT(tvc) ==
#ifdef AFS_DARWIN_ENV
		DARWIN_REFBASE
#else
		0
#endif
		&& tvc->opens == 0 && (tvc->states & CUnlinkedDel) == 0) {
#if defined(AFS_XBSD_ENV)
		/*
		 * vgone() reclaims the vnode, which calls afs_FlushVCache(),
		 * then it puts the vnode on the free list.
		 * If we don't do this we end up with a cleaned vnode that's
		 * not on the free list.
		 * XXX assume FreeBSD is the same for now.
		 */
		vgone(AFSTOV(tvc));
		code = fv_slept = 0;
#else
		code = afs_FlushVCache(tvc, &fv_slept);
#endif
		if (code == 0) {
		    anumber--;
		}
		if (fv_slept) {
		    uq = VLRU.prev;
		    i = 0;
		    continue;	/* start over - may have raced. */
		}
	    }
	    if (tq == uq)
		break;
	}
    }
    if (!freeVCList) {
	/* none free, making one is better than a panic */
	afs_stats_cmperf.vcacheXAllocs++;	/* count in case we have a leak */
	tvc = (struct vcache *)afs_osi_Alloc(sizeof(struct vcache));
#ifdef	KERNEL_HAVE_PIN
	pin((char *)tvc, sizeof(struct vcache));	/* XXX */
#endif
#ifdef	AFS_MACH_ENV
	/* In case it still comes here we need to fill this */
	tvc->v.v_vm_info = VM_INFO_NULL;
	vm_info_init(tvc->v.v_vm_info);
	/* perhaps we should also do close_flush on non-NeXT mach systems;
	 * who knows; we don't currently have the sources.
	 */
#endif /* AFS_MACH_ENV */
#if defined(AFS_SGI_ENV)
	{
	    char name[METER_NAMSZ];
	    memset(tvc, 0, sizeof(struct vcache));
	    tvc->v.v_number = ++afsvnumbers;
	    tvc->vc_rwlockid = OSI_NO_LOCKID;
	    initnsema(&tvc->vc_rwlock, 1,
		      makesname(name, "vrw", tvc->v.v_number));
#ifndef	AFS_SGI53_ENV
	    initnsema(&tvc->v.v_sync, 0,
		      makesname(name, "vsy", tvc->v.v_number));
#endif
#ifndef AFS_SGI62_ENV
	    initnlock(&tvc->v.v_lock,
		      makesname(name, "vlk", tvc->v.v_number));
#endif
	}
#endif /* AFS_SGI_ENV */
    } else {
	tvc = freeVCList;	/* take from free list */
	freeVCList = tvc->nextfree;
	tvc->nextfree = NULL;
    }
#endif /* AFS_OSF_ENV */

#ifdef	AFS_MACH_ENV
    vm_info_ptr = tvc->v.v_vm_info;
#endif /* AFS_MACH_ENV */

#if defined(AFS_XBSD_ENV)
    if (tvc->v)
	panic("afs_NewVCache(): free vcache with vnode attached");
#endif

#if !defined(AFS_SGI_ENV) && !defined(AFS_OSF_ENV)
    memset((char *)tvc, 0, sizeof(struct vcache));
#else
    tvc->uncred = 0;
#endif

    RWLOCK_INIT(&tvc->lock, "vcache lock");
#if	defined(AFS_SUN5_ENV)
    RWLOCK_INIT(&tvc->vlock, "vcache vlock");
#endif /* defined(AFS_SUN5_ENV) */

#ifdef	AFS_MACH_ENV
    tvc->v.v_vm_info = vm_info_ptr;
    tvc->v.v_vm_info->pager = MEMORY_OBJECT_NULL;
#endif /* AFS_MACH_ENV */
#ifdef AFS_OBSD_ENV
    AFS_GUNLOCK();
    afs_nbsd_getnewvnode(tvc);	/* includes one refcount */
    AFS_GLOCK();
    lockinit(&tvc->rwlock, PINOD, "vcache", 0, 0);
#endif
#ifdef AFS_FBSD_ENV
    {
	struct vnode *vp;

	AFS_GUNLOCK();
#ifdef AFS_FBSD50_ENV
	if (getnewvnode(MOUNT_AFS, afs_globalVFS, afs_vnodeop_p, &vp))
#else
	if (getnewvnode(VT_AFS, afs_globalVFS, afs_vnodeop_p, &vp))
#endif
	    panic("afs getnewvnode");	/* can't happen */
	AFS_GLOCK();
	if (tvc->v != NULL) {
	    /* I'd like to know if this ever happens...
	     * We don't drop global for the rest of this function,
	     * so if we do lose the race, the other thread should
	     * have found the same vnode and finished initializing
	     * the vcache entry.  Is it conceivable that this vcache
	     * entry could be recycled during this interval?  If so,
	     * then there probably needs to be some sort of additional
	     * mutual exclusion (an Embryonic flag would suffice).
	     * -GAW */
	    printf("afs_NewVCache: lost the race\n");
	    return (tvc);
	}
	tvc->v = vp;
	tvc->v->v_data = tvc;
	lockinit(&tvc->rwlock, PINOD, "vcache", 0, 0);
    }
#endif
    tvc->parentVnode = 0;
    tvc->mvid = NULL;
    tvc->linkData = NULL;
    tvc->cbExpires = 0;
    tvc->opens = 0;
    tvc->execsOrWriters = 0;
    tvc->flockCount = 0;
    tvc->anyAccess = 0;
    tvc->states = 0;
    tvc->last_looker = 0;
    tvc->fid = *afid;
    tvc->asynchrony = -1;
    tvc->vc_error = 0;
    afs_symhint_inval(tvc);
#ifdef AFS_TEXT_ENV
    tvc->flushDV.low = tvc->flushDV.high = AFS_MAXDV;
#endif
    hzero(tvc->mapDV);
    tvc->truncPos = AFS_NOTRUNC;	/* don't truncate until we need to */
    hzero(tvc->m.DataVersion);	/* in case we copy it into flushDV */
#if defined(AFS_LINUX22_ENV)
    {
	struct inode *ip = AFSTOI(tvc);
	struct address_space *mapping = &ip->i_data;

#if defined(AFS_LINUX26_ENV)
	inode_init_once(ip);
#else
	sema_init(&ip->i_sem, 1);
	INIT_LIST_HEAD(&ip->i_hash);
	INIT_LIST_HEAD(&ip->i_dentry);
#if defined(AFS_LINUX24_ENV)
	sema_init(&ip->i_zombie, 1);
	init_waitqueue_head(&ip->i_wait);
	spin_lock_init(&ip->i_data.i_shared_lock);
#ifdef STRUCT_ADDRESS_SPACE_HAS_PAGE_LOCK
	spin_lock_init(&ip->i_data.page_lock);
#endif
	INIT_LIST_HEAD(&ip->i_data.clean_pages);
	INIT_LIST_HEAD(&ip->i_data.dirty_pages);
	INIT_LIST_HEAD(&ip->i_data.locked_pages);
	INIT_LIST_HEAD(&ip->i_dirty_buffers);
#ifdef STRUCT_INODE_HAS_I_DIRTY_DATA_BUFFERS
	INIT_LIST_HEAD(&ip->i_dirty_data_buffers);
#endif
#ifdef STRUCT_INODE_HAS_I_DEVICES
	INIT_LIST_HEAD(&ip->i_devices);
#endif
#ifdef STRUCT_INODE_HAS_I_TRUNCATE_SEM
	init_rwsem(&ip->i_truncate_sem);
#endif
#ifdef STRUCT_INODE_HAS_I_ALLOC_SEM
	init_rwsem(&ip->i_alloc_sem);
#endif

#else /* AFS_LINUX22_ENV */
	sema_init(&ip->i_atomic_write, 1);
	init_waitqueue(&ip->i_wait);
#endif
#endif

#if defined(AFS_LINUX24_ENV)
	mapping->host = ip;
	ip->i_mapping = mapping;
#ifdef STRUCT_ADDRESS_SPACE_HAS_GFP_MASK
	ip->i_data.gfp_mask = GFP_HIGHUSER;
#endif
#if defined(AFS_LINUX26_ENV)
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER);
	{
	    extern struct backing_dev_info afs_backing_dev_info;

	    mapping->backing_dev_info = &afs_backing_dev_info;
	}
#endif
#endif

#if !defined(AFS_LINUX26_ENV)
	if (afs_globalVFS)
	    ip->i_dev = afs_globalVFS->s_dev;
#else
#ifdef STRUCT_INODE_HAS_I_SECURITY
	ip->i_security = NULL;
	if (security_inode_alloc(ip))
	    panic("Cannot allocate inode security");
#endif
#endif
	ip->i_sb = afs_globalVFS;
	put_inode_on_dummy_list(ip);
    }
#endif

#ifdef	AFS_OSF_ENV
    /* Hold it for the LRU (should make count 2) */
    VN_HOLD(AFSTOV(tvc));
#else /* AFS_OSF_ENV */
#if !defined(AFS_XBSD_ENV)
    VREFCOUNT_SET(tvc, 1);	/* us */
#endif /* AFS_XBSD_ENV */
#endif /* AFS_OSF_ENV */
#ifdef	AFS_AIX32_ENV
    LOCK_INIT(&tvc->pvmlock, "vcache pvmlock");
    tvc->vmh = tvc->segid = NULL;
    tvc->credp = NULL;
#endif
#if defined(AFS_SUN_ENV) || defined(AFS_ALPHA_ENV) || defined(AFS_SUN5_ENV)
#if	defined(AFS_SUN5_ENV)
    rw_init(&tvc->rwlock, "vcache rwlock", RW_DEFAULT, NULL);

#if	defined(AFS_SUN55_ENV)
    /* This is required if the kaio (kernel aynchronous io)
     ** module is installed. Inside the kernel, the function
     ** check_vp( common/os/aio.c) checks to see if the kernel has
     ** to provide asynchronous io for this vnode. This
     ** function extracts the device number by following the
     ** v_data field of the vnode. If we do not set this field
     ** then the system panics. The  value of the v_data field
     ** is not really important for AFS vnodes because the kernel
     ** does not do asynchronous io for regular files. Hence,
     ** for the time being, we fill up the v_data field with the
     ** vnode pointer itself. */
    tvc->v.v_data = (char *)tvc;
#endif /* AFS_SUN55_ENV */
#endif
    afs_BozonInit(&tvc->pvnLock, tvc);
#endif

    tvc->Access = NULL;
    tvc->callback = serverp;	/* to minimize chance that clear
				 * request is lost */
    /* initialize vnode data, note vrefCount is v.v_count */
#ifdef	AFS_AIX_ENV
    /* Don't forget to free the gnode space */
    tvc->v.v_gnode = gnodepnt =
	(struct gnode *)osi_AllocSmallSpace(sizeof(struct gnode));
    memset((char *)gnodepnt, 0, sizeof(struct gnode));
#endif
#ifdef AFS_SGI64_ENV
    memset((void *)&(tvc->vc_bhv_desc), 0, sizeof(tvc->vc_bhv_desc));
    bhv_desc_init(&(tvc->vc_bhv_desc), tvc, tvc, &Afs_vnodeops);
#ifdef AFS_SGI65_ENV
    vn_bhv_head_init(&(tvc->v.v_bh), "afsvp");
    vn_bhv_insert_initial(&(tvc->v.v_bh), &(tvc->vc_bhv_desc));
#else
    bhv_head_init(&(tvc->v.v_bh));
    bhv_insert_initial(&(tvc->v.v_bh), &(tvc->vc_bhv_desc));
#endif
#ifdef AFS_SGI65_ENV
    tvc->v.v_mreg = tvc->v.v_mregb = (struct pregion *)tvc;
#ifdef VNODE_TRACING
    tvc->v.v_trace = ktrace_alloc(VNODE_TRACE_SIZE, 0);
#endif
    init_bitlock(&tvc->v.v_pcacheflag, VNODE_PCACHE_LOCKBIT, "afs_pcache",
		 tvc->v.v_number);
    init_mutex(&tvc->v.v_filocksem, MUTEX_DEFAULT, "afsvfl", (long)tvc);
    init_mutex(&tvc->v.v_buf_lock, MUTEX_DEFAULT, "afsvnbuf", (long)tvc);
#endif
    vnode_pcache_init(&tvc->v);
#if defined(DEBUG) && defined(VNODE_INIT_BITLOCK)
    /* Above define is never true execpt in SGI test kernels. */
    init_bitlock(&(tvc->v.v_flag, VLOCK, "vnode", tvc->v.v_number);
#endif
#ifdef INTR_KTHREADS
		 AFS_VN_INIT_BUF_LOCK(&(tvc->v));
#endif
#else
    SetAfsVnode(AFSTOV(tvc));
#endif /* AFS_SGI64_ENV */
#ifdef AFS_DARWIN_ENV
    tvc->v.v_ubcinfo = UBC_INFO_NULL;
    lockinit(&tvc->rwlock, PINOD, "vcache rwlock", 0, 0);
    cache_purge(AFSTOV(tvc));
    tvc->v.v_data = tvc;
    tvc->v.v_tag = VT_AFS;
    /* VLISTNONE(&tvc->v); */
    tvc->v.v_freelist.tqe_next = 0;
    tvc->v.v_freelist.tqe_prev = (struct vnode **)0xdeadb;
    tvc->vrefCount += DARWIN_REFBASE;
#endif
    /*
     * The proper value for mvstat (for root fids) is setup by the caller.
     */
    tvc->mvstat = 0;
    if (afid->Fid.Vnode == 1 && afid->Fid.Unique == 1)
	tvc->mvstat = 2;
    if (afs_globalVFS == 0)
	osi_Panic("afs globalvfs");
    vSetVfsp(tvc, afs_globalVFS);
    vSetType(tvc, VREG);
#ifdef	AFS_AIX_ENV
    tvc->v.v_vfsnext = afs_globalVFS->vfs_vnodes;	/* link off vfs */
    tvc->v.v_vfsprev = NULL;
    afs_globalVFS->vfs_vnodes = &tvc->v;
    if (tvc->v.v_vfsnext != NULL)
	tvc->v.v_vfsnext->v_vfsprev = &tvc->v;
    tvc->v.v_next = gnodepnt->gn_vnode;	/*Single vnode per gnode for us! */
    gnodepnt->gn_vnode = &tvc->v;
#endif
#ifdef	AFS_DEC_ENV
    tvc->v.g_dev = ((struct mount *)afs_globalVFS->vfs_data)->m_dev;
#endif
#if	defined(AFS_DUX40_ENV)
    insmntque(tvc, afs_globalVFS, &afs_ubcops);
#else
#ifdef  AFS_OSF_ENV
    /* Is this needed??? */
    insmntque(tvc, afs_globalVFS);
#endif /* AFS_OSF_ENV */
#endif /* AFS_DUX40_ENV */
#if defined(AFS_SGI_ENV)
    VN_SET_DPAGES(&(tvc->v), (struct pfdat *)NULL);
    osi_Assert((tvc->v.v_flag & VINACT) == 0);
    tvc->v.v_flag = 0;
    osi_Assert(VN_GET_PGCNT(&(tvc->v)) == 0);
    osi_Assert(tvc->mapcnt == 0 && tvc->vc_locktrips == 0);
    osi_Assert(tvc->vc_rwlockid == OSI_NO_LOCKID);
    osi_Assert(tvc->v.v_filocks == NULL);
#if !defined(AFS_SGI65_ENV)
    osi_Assert(tvc->v.v_filocksem == NULL);
#endif
    osi_Assert(tvc->cred == NULL);
#ifdef AFS_SGI64_ENV
    vnode_pcache_reinit(&tvc->v);
    tvc->v.v_rdev = NODEV;
#endif
    vn_initlist((struct vnlist *)&tvc->v);
    tvc->lastr = 0;
#endif /* AFS_SGI_ENV */
    tvc->h1.dchint = 0;
    osi_dnlc_purgedp(tvc);	/* this may be overkill */
    memset((char *)&(tvc->quick), 0, sizeof(struct vtodc));
    memset((char *)&(tvc->callsort), 0, sizeof(struct afs_q));
    tvc->slocks = NULL;
    i = VCHash(afid);

    tvc->hnext = afs_vhashT[i];
    afs_vhashT[i] = tvc;
    if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
	refpanic("NewVCache VLRU inconsistent");
    }
    QAdd(&VLRU, &tvc->vlruq);	/* put in lruq */
    if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
	refpanic("NewVCache VLRU inconsistent2");
    }
    if (tvc->vlruq.next->prev != &(tvc->vlruq)) {
	refpanic("NewVCache VLRU inconsistent3");
    }
    if (tvc->vlruq.prev->next != &(tvc->vlruq)) {
	refpanic("NewVCache VLRU inconsistent4");
    }
    vcachegen++;

    return tvc;

}				/*afs_NewVCache */


/*
 * afs_FlushActiveVcaches
 *
 * Description:
 *	???
 *
 * Parameters:
 *	doflocks : Do we handle flocks?
 */
/* LOCK: afs_FlushActiveVcaches afs_xvcache N */
void
afs_FlushActiveVcaches(register afs_int32 doflocks)
{
    register struct vcache *tvc;
    register int i;
    register struct conn *tc;
    register afs_int32 code;
    register struct AFS_UCRED *cred = NULL;
    struct vrequest treq, ureq;
    struct AFSVolSync tsync;
    int didCore;
    XSTATS_DECLS;
    AFS_STATCNT(afs_FlushActiveVcaches);
    ObtainReadLock(&afs_xvcache);
    for (i = 0; i < VCSIZE; i++) {
	for (tvc = afs_vhashT[i]; tvc; tvc = tvc->hnext) {
	    if (doflocks && tvc->flockCount != 0) {
		/* if this entry has an flock, send a keep-alive call out */
		osi_vnhold(tvc, 0);
		ReleaseReadLock(&afs_xvcache);
		ObtainWriteLock(&tvc->lock, 51);
		do {
		    afs_InitReq(&treq, afs_osi_credp);
		    treq.flags |= O_NONBLOCK;

		    tc = afs_Conn(&tvc->fid, &treq, SHARED_LOCK);
		    if (tc) {
			XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_EXTENDLOCK);
			RX_AFS_GUNLOCK();
			code =
			    RXAFS_ExtendLock(tc->id,
					     (struct AFSFid *)&tvc->fid.Fid,
					     &tsync);
			RX_AFS_GLOCK();
			XSTATS_END_TIME;
		    } else
			code = -1;
		} while (afs_Analyze
			 (tc, code, &tvc->fid, &treq,
			  AFS_STATS_FS_RPCIDX_EXTENDLOCK, SHARED_LOCK, NULL));

		ReleaseWriteLock(&tvc->lock);
		ObtainReadLock(&afs_xvcache);
		AFS_FAST_RELE(tvc);
	    }
	    didCore = 0;
	    if ((tvc->states & CCore) || (tvc->states & CUnlinkedDel)) {
		/*
		 * Don't let it evaporate in case someone else is in
		 * this code.  Also, drop the afs_xvcache lock while
		 * getting vcache locks.
		 */
		osi_vnhold(tvc, 0);
		ReleaseReadLock(&afs_xvcache);
#if defined(AFS_SUN_ENV) || defined(AFS_ALPHA_ENV)
		afs_BozonLock(&tvc->pvnLock, tvc);
#endif
#if defined(AFS_SGI_ENV)
		/*
		 * That's because if we come in via the CUnlinkedDel bit state path we'll be have 0 refcnt
		 */
		osi_Assert(VREFCOUNT(tvc) > 0);
		AFS_RWLOCK((vnode_t *) tvc, VRWLOCK_WRITE);
#endif
		ObtainWriteLock(&tvc->lock, 52);
		if (tvc->states & CCore) {
		    tvc->states &= ~CCore;
		    /* XXXX Find better place-holder for cred XXXX */
		    cred = (struct AFS_UCRED *)tvc->linkData;
		    tvc->linkData = NULL;	/* XXX */
		    afs_InitReq(&ureq, cred);
		    afs_Trace2(afs_iclSetp, CM_TRACE_ACTCCORE,
			       ICL_TYPE_POINTER, tvc, ICL_TYPE_INT32,
			       tvc->execsOrWriters);
		    code = afs_StoreOnLastReference(tvc, &ureq);
		    ReleaseWriteLock(&tvc->lock);
#if defined(AFS_SUN_ENV) || defined(AFS_ALPHA_ENV)
		    afs_BozonUnlock(&tvc->pvnLock, tvc);
#endif
		    hzero(tvc->flushDV);
		    osi_FlushText(tvc);
		    didCore = 1;
		    if (code && code != VNOVNODE) {
			afs_StoreWarn(code, tvc->fid.Fid.Volume,
				      /* /dev/console */ 1);
		    }
		} else if (tvc->states & CUnlinkedDel) {
		    /*
		     * Ignore errors
		     */
		    ReleaseWriteLock(&tvc->lock);
#if defined(AFS_SUN_ENV) || defined(AFS_ALPHA_ENV)
		    afs_BozonUnlock(&tvc->pvnLock, tvc);
#endif
#if defined(AFS_SGI_ENV)
		    AFS_RWUNLOCK((vnode_t *) tvc, VRWLOCK_WRITE);
#endif
		    afs_remunlink(tvc, 0);
#if defined(AFS_SGI_ENV)
		    AFS_RWLOCK((vnode_t *) tvc, VRWLOCK_WRITE);
#endif
		} else {
		    /* lost (or won, perhaps) the race condition */
		    ReleaseWriteLock(&tvc->lock);
#if defined(AFS_SUN_ENV) || defined(AFS_ALPHA_ENV)
		    afs_BozonUnlock(&tvc->pvnLock, tvc);
#endif
		}
#if defined(AFS_SGI_ENV)
		AFS_RWUNLOCK((vnode_t *) tvc, VRWLOCK_WRITE);
#endif
		ObtainReadLock(&afs_xvcache);
		AFS_FAST_RELE(tvc);
		if (didCore) {
#ifdef	AFS_GFS_ENV
		    VREFCOUNT_DEC(tvc);
#else
		    AFS_RELE(AFSTOV(tvc));
#endif
		    /* Matches write code setting CCore flag */
		    crfree(cred);
		}
	    }
#ifdef AFS_DARWIN_ENV
	    if (VREFCOUNT(tvc) == 1 + DARWIN_REFBASE
		&& UBCINFOEXISTS(&tvc->v)) {
		if (tvc->opens)
		    panic("flushactive open, hasubc, but refcnt 1");
		osi_VM_TryReclaim(tvc, 0);
	    }
#endif
	}
    }
    ReleaseReadLock(&afs_xvcache);
}


/*
 * afs_VerifyVCache
 *
 * Description:
 *	Make sure a cache entry is up-to-date status-wise.
 *
 * NOTE: everywhere that calls this can potentially be sped up
 *       by checking CStatd first, and avoiding doing the InitReq
 *       if this is up-to-date.
 *
 *  Anymore, the only places that call this KNOW already that the
 *  vcache is not up-to-date, so we don't screw around.
 *
 * Parameters:
 *	avc  : Ptr to vcache entry to verify.
 *	areq : ???
 */

int
afs_VerifyVCache2(struct vcache *avc, struct vrequest *areq)
{
    register struct vcache *tvc;

    AFS_STATCNT(afs_VerifyVCache);

#if defined(AFS_OSF_ENV)
    ObtainReadLock(&avc->lock);
    if (afs_IsWired(avc)) {
	ReleaseReadLock(&avc->lock);
	return 0;
    }
    ReleaseReadLock(&avc->lock);
#endif /* AFS_OSF_ENV */
    /* otherwise we must fetch the status info */

    ObtainWriteLock(&avc->lock, 53);
    if (avc->states & CStatd) {
	ReleaseWriteLock(&avc->lock);
	return 0;
    }
    ObtainWriteLock(&afs_xcbhash, 461);
    avc->states &= ~(CStatd | CUnique);
    avc->callback = NULL;
    afs_DequeueCallback(avc);
    ReleaseWriteLock(&afs_xcbhash);
    ReleaseWriteLock(&avc->lock);

    /* since we've been called back, or the callback has expired,
     * it's possible that the contents of this directory, or this
     * file's name have changed, thus invalidating the dnlc contents.
     */
    if ((avc->states & CForeign) || (avc->fid.Fid.Vnode & 1))
	osi_dnlc_purgedp(avc);
    else
	osi_dnlc_purgevp(avc);

    /* fetch the status info */
    tvc = afs_GetVCache(&avc->fid, areq, NULL, avc);
    if (!tvc)
	return ENOENT;
    /* Put it back; caller has already incremented vrefCount */
    afs_PutVCache(tvc);
    return 0;

}				/*afs_VerifyVCache */


/*
 * afs_SimpleVStat
 *
 * Description:
 *	Simple copy of stat info into cache.
 *
 * Parameters:
 *	avc   : Ptr to vcache entry involved.
 *	astat : Ptr to stat info to copy.
 *
 * Environment:
 *	Nothing interesting.
 *
 * Callers:  as of 1992-04-29, only called by WriteVCache
 */
static void
afs_SimpleVStat(register struct vcache *avc,
		register struct AFSFetchStatus *astat, struct vrequest *areq)
{
    afs_size_t length;
    AFS_STATCNT(afs_SimpleVStat);

#ifdef AFS_SGI_ENV
    if ((avc->execsOrWriters <= 0) && !afs_DirtyPages(avc)
	&& !AFS_VN_MAPPED((vnode_t *) avc)) {
#else
    if ((avc->execsOrWriters <= 0) && !afs_DirtyPages(avc)) {
#endif
#ifdef AFS_64BIT_CLIENT
	FillInt64(length, astat->Length_hi, astat->Length);
#else /* AFS_64BIT_CLIENT */
	length = astat->Length;
#endif /* AFS_64BIT_CLIENT */
#if defined(AFS_SGI_ENV)
	osi_Assert((valusema(&avc->vc_rwlock) <= 0)
		   && (OSI_GET_LOCKID() == avc->vc_rwlockid));
	if (length < avc->m.Length) {
	    vnode_t *vp = (vnode_t *) avc;

	    osi_Assert(WriteLocked(&avc->lock));
	    ReleaseWriteLock(&avc->lock);
	    AFS_GUNLOCK();
	    PTOSSVP(vp, (off_t) length, (off_t) MAXLONG);
	    AFS_GLOCK();
	    ObtainWriteLock(&avc->lock, 67);
	}
#endif
	/* if writing the file, don't fetch over this value */
	afs_Trace3(afs_iclSetp, CM_TRACE_SIMPLEVSTAT, ICL_TYPE_POINTER, avc,
		   ICL_TYPE_OFFSET, ICL_HANDLE_OFFSET(avc->m.Length),
		   ICL_TYPE_OFFSET, ICL_HANDLE_OFFSET(length));
	avc->m.Length = length;
	avc->m.Date = astat->ClientModTime;
    }
    avc->m.Owner = astat->Owner;
    avc->m.Group = astat->Group;
    avc->m.Mode = astat->UnixModeBits;
    if (vType(avc) == VREG) {
	avc->m.Mode |= S_IFREG;
    } else if (vType(avc) == VDIR) {
	avc->m.Mode |= S_IFDIR;
    } else if (vType(avc) == VLNK) {
	avc->m.Mode |= S_IFLNK;
	if ((avc->m.Mode & 0111) == 0)
	    avc->mvstat = 1;
    }
    if (avc->states & CForeign) {
	struct axscache *ac;
	avc->anyAccess = astat->AnonymousAccess;
#ifdef badidea
	if ((astat->CallerAccess & ~astat->AnonymousAccess))
	    /*   USED TO SAY :
	     * Caller has at least one bit not covered by anonymous, and
	     * thus may have interesting rights.
	     *
	     * HOWEVER, this is a really bad idea, because any access query
	     * for bits which aren't covered by anonymous, on behalf of a user
	     * who doesn't have any special rights, will result in an answer of
	     * the form "I don't know, lets make a FetchStatus RPC and find out!"
	     * It's an especially bad idea under Ultrix, since (due to the lack of
	     * a proper access() call) it must perform several afs_access() calls
	     * in order to create magic mode bits that vary according to who makes
	     * the call.  In other words, _every_ stat() generates a test for
	     * writeability...
	     */
#endif /* badidea */
	    if (avc->Access && (ac = afs_FindAxs(avc->Access, areq->uid)))
		ac->axess = astat->CallerAccess;
	    else		/* not found, add a new one if possible */
		afs_AddAxs(avc->Access, areq->uid, astat->CallerAccess);
    }


}				/*afs_SimpleVStat */


/*
 * afs_WriteVCache
 *
 * Description:
 *	Store the status info *only* back to the server for a
 *	fid/vrequest.
 *
 * Parameters:
 *	avc	: Ptr to the vcache entry.
 *	astatus	: Ptr to the status info to store.
 *	areq	: Ptr to the associated vrequest.
 *
 * Environment:
 *	Must be called with a shared lock held on the vnode.
 */

int
afs_WriteVCache(register struct vcache *avc,
		register struct AFSStoreStatus *astatus,
		struct vrequest *areq)
{
    afs_int32 code;
    struct conn *tc;
    struct AFSFetchStatus OutStatus;
    struct AFSVolSync tsync;
    XSTATS_DECLS;
    AFS_STATCNT(afs_WriteVCache);
    afs_Trace2(afs_iclSetp, CM_TRACE_WVCACHE, ICL_TYPE_POINTER, avc,
	       ICL_TYPE_OFFSET, ICL_HANDLE_OFFSET(avc->m.Length));

    do {
	tc = afs_Conn(&avc->fid, areq, SHARED_LOCK);
	if (tc) {
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_STORESTATUS);
	    RX_AFS_GUNLOCK();
	    code =
		RXAFS_StoreStatus(tc->id, (struct AFSFid *)&avc->fid.Fid,
				  astatus, &OutStatus, &tsync);
	    RX_AFS_GLOCK();
	    XSTATS_END_TIME;
	} else
	    code = -1;
    } while (afs_Analyze
	     (tc, code, &avc->fid, areq, AFS_STATS_FS_RPCIDX_STORESTATUS,
	      SHARED_LOCK, NULL));

    UpgradeSToWLock(&avc->lock, 20);
    if (code == 0) {
	/* success, do the changes locally */
	afs_SimpleVStat(avc, &OutStatus, areq);
	/*
	 * Update the date, too.  SimpleVStat didn't do this, since
	 * it thought we were doing this after fetching new status
	 * over a file being written.
	 */
	avc->m.Date = OutStatus.ClientModTime;
    } else {
	/* failure, set up to check with server next time */
	ObtainWriteLock(&afs_xcbhash, 462);
	afs_DequeueCallback(avc);
	avc->states &= ~(CStatd | CUnique);	/* turn off stat valid flag */
	ReleaseWriteLock(&afs_xcbhash);
	if ((avc->states & CForeign) || (avc->fid.Fid.Vnode & 1))
	    osi_dnlc_purgedp(avc);	/* if it (could be) a directory */
    }
    ConvertWToSLock(&avc->lock);
    return code;

}				/*afs_WriteVCache */

/*
 * afs_ProcessFS
 *
 * Description:
 *	Copy astat block into vcache info
 *
 * Parameters:
 *	avc   : Ptr to vcache entry.
 *	astat : Ptr to stat block to copy in.
 *	areq  : Ptr to associated request.
 *
 * Environment:
 *	Must be called under a write lock
 *
 * Note: this code may get dataversion and length out of sync if the file has
 *       been modified.  This is less than ideal.  I haven't thought about
 *       it sufficiently to be certain that it is adequate.
 */
void
afs_ProcessFS(register struct vcache *avc,
	      register struct AFSFetchStatus *astat, struct vrequest *areq)
{
    afs_size_t length;
    AFS_STATCNT(afs_ProcessFS);

#ifdef AFS_64BIT_CLIENT
    FillInt64(length, astat->Length_hi, astat->Length);
#else /* AFS_64BIT_CLIENT */
    length = astat->Length;
#endif /* AFS_64BIT_CLIENT */
    /* WARNING: afs_DoBulkStat uses the Length field to store a sequence
     * number for each bulk status request. Under no circumstances
     * should afs_DoBulkStat store a sequence number if the new
     * length will be ignored when afs_ProcessFS is called with
     * new stats. If you change the following conditional then you
     * also need to change the conditional in afs_DoBulkStat.  */
#ifdef AFS_SGI_ENV
    if ((avc->execsOrWriters <= 0) && !afs_DirtyPages(avc)
	&& !AFS_VN_MAPPED((vnode_t *) avc)) {
#else
    if ((avc->execsOrWriters <= 0) && !afs_DirtyPages(avc)) {
#endif
	/* if we're writing or mapping this file, don't fetch over these
	 *  values.
	 */
	afs_Trace3(afs_iclSetp, CM_TRACE_PROCESSFS, ICL_TYPE_POINTER, avc,
		   ICL_TYPE_OFFSET, ICL_HANDLE_OFFSET(avc->m.Length),
		   ICL_TYPE_OFFSET, ICL_HANDLE_OFFSET(length));
	avc->m.Length = length;
	avc->m.Date = astat->ClientModTime;
    }
    hset64(avc->m.DataVersion, astat->dataVersionHigh, astat->DataVersion);
    avc->m.Owner = astat->Owner;
    avc->m.Mode = astat->UnixModeBits;
    avc->m.Group = astat->Group;
    avc->m.LinkCount = astat->LinkCount;
    if (astat->FileType == File) {
	vSetType(avc, VREG);
	avc->m.Mode |= S_IFREG;
    } else if (astat->FileType == Directory) {
	vSetType(avc, VDIR);
	avc->m.Mode |= S_IFDIR;
    } else if (astat->FileType == SymbolicLink) {
	if (afs_fakestat_enable && (avc->m.Mode & 0111) == 0) {
	    vSetType(avc, VDIR);
	    avc->m.Mode |= S_IFDIR;
	} else {
	    vSetType(avc, VLNK);
	    avc->m.Mode |= S_IFLNK;
	}
	if ((avc->m.Mode & 0111) == 0) {
	    avc->mvstat = 1;
	}
    }
    avc->anyAccess = astat->AnonymousAccess;
#ifdef badidea
    if ((astat->CallerAccess & ~astat->AnonymousAccess))
	/*   USED TO SAY :
	 * Caller has at least one bit not covered by anonymous, and
	 * thus may have interesting rights.
	 *
	 * HOWEVER, this is a really bad idea, because any access query
	 * for bits which aren't covered by anonymous, on behalf of a user
	 * who doesn't have any special rights, will result in an answer of
	 * the form "I don't know, lets make a FetchStatus RPC and find out!"
	 * It's an especially bad idea under Ultrix, since (due to the lack of
	 * a proper access() call) it must perform several afs_access() calls
	 * in order to create magic mode bits that vary according to who makes
	 * the call.  In other words, _every_ stat() generates a test for
	 * writeability...
	 */
#endif /* badidea */
    {
	struct axscache *ac;
	if (avc->Access && (ac = afs_FindAxs(avc->Access, areq->uid)))
	    ac->axess = astat->CallerAccess;
	else			/* not found, add a new one if possible */
	    afs_AddAxs(avc->Access, areq->uid, astat->CallerAccess);
    }
#ifdef AFS_LINUX22_ENV
    vcache2inode(avc);		/* Set the inode attr cache */
#endif
#ifdef AFS_DARWIN_ENV
    osi_VM_Setup(avc, 1);
#endif

}				/*afs_ProcessFS */


int
afs_RemoteLookup(register struct VenusFid *afid, struct vrequest *areq,
		 char *name, struct VenusFid *nfid,
		 struct AFSFetchStatus *OutStatusp,
		 struct AFSCallBack *CallBackp, struct server **serverp,
		 struct AFSVolSync *tsyncp)
{
    afs_int32 code;
    afs_uint32 start;
    register struct conn *tc;
    struct AFSFetchStatus OutDirStatus;
    XSTATS_DECLS;
    if (!name)
	name = "";		/* XXX */
    do {
	tc = afs_Conn(afid, areq, SHARED_LOCK);
	if (tc) {
	    if (serverp)
		*serverp = tc->srvr->server;
	    start = osi_Time();
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_XLOOKUP);
	    RX_AFS_GUNLOCK();
	    code =
		RXAFS_Lookup(tc->id, (struct AFSFid *)&afid->Fid, name,
			     (struct AFSFid *)&nfid->Fid, OutStatusp,
			     &OutDirStatus, CallBackp, tsyncp);
	    RX_AFS_GLOCK();
	    XSTATS_END_TIME;
	} else
	    code = -1;
    } while (afs_Analyze
	     (tc, code, afid, areq, AFS_STATS_FS_RPCIDX_XLOOKUP, SHARED_LOCK,
	      NULL));

    return code;
}


/*
 * afs_GetVCache
 *
 * Description:
 *	Given a file id and a vrequest structure, fetch the status
 *	information associated with the file.
 *
 * Parameters:
 *	afid : File ID.
 *	areq : Ptr to associated vrequest structure, specifying the
 *		user whose authentication tokens will be used.
 *      avc  : caller may already have a vcache for this file, which is
 *             already held.
 *
 * Environment:
 *	The cache entry is returned with an increased vrefCount field.
 *	The entry must be discarded by calling afs_PutVCache when you
 *	are through using the pointer to the cache entry.
 *
 *	You should not hold any locks when calling this function, except
 *	locks on other vcache entries.  If you lock more than one vcache
 *	entry simultaneously, you should lock them in this order:
 *
 *	    1. Lock all files first, then directories.
 *	    2.  Within a particular type, lock entries in Fid.Vnode order.
 *
 *	This locking hierarchy is convenient because it allows locking
 *	of a parent dir cache entry, given a file (to check its access
 *	control list).  It also allows renames to be handled easily by
 *	locking directories in a constant order.
 * NB.  NewVCache -> FlushVCache presently (4/10/95) drops the xvcache lock.
 */
   /* might have a vcache structure already, which must
    * already be held by the caller */

struct vcache *
afs_GetVCache(register struct VenusFid *afid, struct vrequest *areq,
	      afs_int32 * cached, struct vcache *avc)
{

    afs_int32 code, newvcache = 0;
    register struct vcache *tvc;
    struct volume *tvp;
    afs_int32 retry;

    AFS_STATCNT(afs_GetVCache);

    if (cached)
	*cached = 0;		/* Init just in case */

#if	defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
  loop:
#endif

    ObtainSharedLock(&afs_xvcache, 5);

    tvc = afs_FindVCache(afid, &retry, DO_STATS | DO_VLRU);
    if (tvc && retry) {
#if	defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
	ReleaseSharedLock(&afs_xvcache);
	spunlock_psema(tvc->v.v_lock, retry, &tvc->v.v_sync, PINOD);
	goto loop;
#endif
    }

    if (tvc) {
	if (cached)
	    *cached = 1;
	if (tvc->states & CStatd) {
	    ReleaseSharedLock(&afs_xvcache);
	    return tvc;
	}
    } else {
	UpgradeSToWLock(&afs_xvcache, 21);

	/* no cache entry, better grab one */
	tvc = afs_NewVCache(afid, NULL);
	newvcache = 1;

	ConvertWToSLock(&afs_xvcache);
	afs_stats_cmperf.vcacheMisses++;
    }

    ReleaseSharedLock(&afs_xvcache);

    ObtainWriteLock(&tvc->lock, 54);

    if (tvc->states & CStatd) {
#ifdef AFS_LINUX22_ENV
	vcache2inode(tvc);
#endif
	ReleaseWriteLock(&tvc->lock);
#ifdef AFS_DARWIN_ENV
	osi_VM_Setup(tvc, 0);
#endif
	return tvc;
    }
#if defined(AFS_OSF_ENV)
    if (afs_IsWired(tvc)) {
	ReleaseWriteLock(&tvc->lock);
	return tvc;
    }
#endif /* AFS_OSF_ENV */
#ifdef AFS_OBSD_ENV
    VOP_LOCK(AFSTOV(tvc), LK_EXCLUSIVE | LK_RETRY, curproc);
    uvm_vnp_uncache(AFSTOV(tvc));
    VOP_UNLOCK(AFSTOV(tvc), 0, curproc);
#endif
#ifdef AFS_FBSD_ENV
    /*
     * XXX - I really don't like this.  Should try to understand better.
     * It seems that sometimes, when we get called, we already hold the
     * lock on the vnode (e.g., from afs_getattr via afs_VerifyVCache).
     * We can't drop the vnode lock, because that could result in a race.
     * Sometimes, though, we get here and don't hold the vnode lock.
     * I hate code paths that sometimes hold locks and sometimes don't.
     * In any event, the dodge we use here is to check whether the vnode
     * is locked, and if it isn't, then we gain and drop it around the call
     * to vinvalbuf; otherwise, we leave it alone.
     */
    {
	struct vnode *vp;
	int iheldthelock;

	vp = AFSTOV(tvc);
#ifdef AFS_FBSD50_ENV
	iheldthelock = VOP_ISLOCKED(vp, curthread);
	if (!iheldthelock)
	    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, curthread);
	vinvalbuf(vp, V_SAVE, osi_curcred(), curthread, PINOD, 0);
	if (!iheldthelock)
	    VOP_UNLOCK(vp, LK_EXCLUSIVE, curthread);
#else
	iheldthelock = VOP_ISLOCKED(vp, curproc);
	if (!iheldthelock)
	    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, curproc);
	vinvalbuf(vp, V_SAVE, osi_curcred(), curproc, PINOD, 0);
	if (!iheldthelock)
	    VOP_UNLOCK(vp, LK_EXCLUSIVE, curproc);
#endif
    }
#endif

    ObtainWriteLock(&afs_xcbhash, 464);
    tvc->states &= ~CUnique;
    tvc->callback = 0;
    afs_DequeueCallback(tvc);
    ReleaseWriteLock(&afs_xcbhash);

    /* It is always appropriate to throw away all the access rights? */
    afs_FreeAllAxs(&(tvc->Access));
    tvp = afs_GetVolume(afid, areq, READ_LOCK);	/* copy useful per-volume info */
    if (tvp) {
	if ((tvp->states & VForeign)) {
	    if (newvcache)
		tvc->states |= CForeign;
	    if (newvcache && (tvp->rootVnode == afid->Fid.Vnode)
		&& (tvp->rootUnique == afid->Fid.Unique)) {
		tvc->mvstat = 2;
	    }
	}
	if (tvp->states & VRO)
	    tvc->states |= CRO;
	if (tvp->states & VBackup)
	    tvc->states |= CBackup;
	/* now copy ".." entry back out of volume structure, if necessary */
	if (tvc->mvstat == 2 && tvp->dotdot.Fid.Volume != 0) {
	    if (!tvc->mvid)
		tvc->mvid = (struct VenusFid *)
		    osi_AllocSmallSpace(sizeof(struct VenusFid));
	    *tvc->mvid = tvp->dotdot;
	}
	afs_PutVolume(tvp, READ_LOCK);
    }

    /* stat the file */
    afs_RemoveVCB(afid);
    {
	struct AFSFetchStatus OutStatus;

	if (afs_DynrootNewVnode(tvc, &OutStatus)) {
	    afs_ProcessFS(tvc, &OutStatus, areq);
	    tvc->states |= CStatd | CUnique;
	    code = 0;
	} else {
	    code = afs_FetchStatus(tvc, afid, areq, &OutStatus);
	}
    }

    if (code) {
	ReleaseWriteLock(&tvc->lock);

	ObtainReadLock(&afs_xvcache);
	AFS_FAST_RELE(tvc);
	ReleaseReadLock(&afs_xvcache);
	return NULL;
    }

    ReleaseWriteLock(&tvc->lock);
    return tvc;

}				/*afs_GetVCache */



struct vcache *
afs_LookupVCache(struct VenusFid *afid, struct vrequest *areq,
		 afs_int32 * cached, struct vcache *adp, char *aname)
{
    afs_int32 code, now, newvcache = 0;
    struct VenusFid nfid;
    register struct vcache *tvc;
    struct volume *tvp;
    struct AFSFetchStatus OutStatus;
    struct AFSCallBack CallBack;
    struct AFSVolSync tsync;
    struct server *serverp = 0;
    afs_int32 origCBs;
    afs_int32 retry;

    AFS_STATCNT(afs_GetVCache);
    if (cached)
	*cached = 0;		/* Init just in case */

#if	defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
  loop1:
#endif

    ObtainReadLock(&afs_xvcache);
    tvc = afs_FindVCache(afid, &retry, DO_STATS /* no vlru */ );

    if (tvc) {
	ReleaseReadLock(&afs_xvcache);
	if (retry) {
#if	defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
	    spunlock_psema(tvc->v.v_lock, retry, &tvc->v.v_sync, PINOD);
	    goto loop1;
#endif
	}
	ObtainReadLock(&tvc->lock);

	if (tvc->states & CStatd) {
	    if (cached) {
		*cached = 1;
	    }
	    ReleaseReadLock(&tvc->lock);
	    return tvc;
	}
	tvc->states &= ~CUnique;

	ReleaseReadLock(&tvc->lock);
	ObtainReadLock(&afs_xvcache);
	AFS_FAST_RELE(tvc);
    }
    /* if (tvc) */
    ReleaseReadLock(&afs_xvcache);

    /* lookup the file */
    nfid = *afid;
    now = osi_Time();
    origCBs = afs_allCBs;	/* if anything changes, we don't have a cb */
    code =
	afs_RemoteLookup(&adp->fid, areq, aname, &nfid, &OutStatus, &CallBack,
			 &serverp, &tsync);

#if	defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
  loop2:
#endif

    ObtainSharedLock(&afs_xvcache, 6);
    tvc = afs_FindVCache(&nfid, &retry, DO_VLRU /* no xstats now */ );
    if (tvc && retry) {
#if	defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
	ReleaseSharedLock(&afs_xvcache);
	spunlock_psema(tvc->v.v_lock, retry, &tvc->v.v_sync, PINOD);
	goto loop2;
#endif
    }

    if (!tvc) {
	/* no cache entry, better grab one */
	UpgradeSToWLock(&afs_xvcache, 22);
	tvc = afs_NewVCache(&nfid, serverp);
	newvcache = 1;
	ConvertWToSLock(&afs_xvcache);
    }

    ReleaseSharedLock(&afs_xvcache);
    ObtainWriteLock(&tvc->lock, 55);

    /* It is always appropriate to throw away all the access rights? */
    afs_FreeAllAxs(&(tvc->Access));
    tvp = afs_GetVolume(afid, areq, READ_LOCK);	/* copy useful per-vol info */
    if (tvp) {
	if ((tvp->states & VForeign)) {
	    if (newvcache)
		tvc->states |= CForeign;
	    if (newvcache && (tvp->rootVnode == afid->Fid.Vnode)
		&& (tvp->rootUnique == afid->Fid.Unique))
		tvc->mvstat = 2;
	}
	if (tvp->states & VRO)
	    tvc->states |= CRO;
	if (tvp->states & VBackup)
	    tvc->states |= CBackup;
	/* now copy ".." entry back out of volume structure, if necessary */
	if (tvc->mvstat == 2 && tvp->dotdot.Fid.Volume != 0) {
	    if (!tvc->mvid)
		tvc->mvid = (struct VenusFid *)
		    osi_AllocSmallSpace(sizeof(struct VenusFid));
	    *tvc->mvid = tvp->dotdot;
	}
    }

    if (code) {
	ObtainWriteLock(&afs_xcbhash, 465);
	afs_DequeueCallback(tvc);
	tvc->states &= ~(CStatd | CUnique);
	ReleaseWriteLock(&afs_xcbhash);
	if ((tvc->states & CForeign) || (tvc->fid.Fid.Vnode & 1))
	    osi_dnlc_purgedp(tvc);	/* if it (could be) a directory */
	if (tvp)
	    afs_PutVolume(tvp, READ_LOCK);
	ReleaseWriteLock(&tvc->lock);
	ObtainReadLock(&afs_xvcache);
	AFS_FAST_RELE(tvc);
	ReleaseReadLock(&afs_xvcache);
	return NULL;
    }

    ObtainWriteLock(&afs_xcbhash, 466);
    if (origCBs == afs_allCBs) {
	if (CallBack.ExpirationTime) {
	    tvc->callback = serverp;
	    tvc->cbExpires = CallBack.ExpirationTime + now;
	    tvc->states |= CStatd | CUnique;
	    tvc->states &= ~CBulkFetching;
	    afs_QueueCallback(tvc, CBHash(CallBack.ExpirationTime), tvp);
	} else if (tvc->states & CRO) {
	    /* adapt gives us an hour. */
	    tvc->cbExpires = 3600 + osi_Time();
	     /*XXX*/ tvc->states |= CStatd | CUnique;
	    tvc->states &= ~CBulkFetching;
	    afs_QueueCallback(tvc, CBHash(3600), tvp);
	} else {
	    tvc->callback = NULL;
	    afs_DequeueCallback(tvc);
	    tvc->states &= ~(CStatd | CUnique);
	    if ((tvc->states & CForeign) || (tvc->fid.Fid.Vnode & 1))
		osi_dnlc_purgedp(tvc);	/* if it (could be) a directory */
	}
    } else {
	afs_DequeueCallback(tvc);
	tvc->states &= ~CStatd;
	tvc->states &= ~CUnique;
	tvc->callback = NULL;
	if ((tvc->states & CForeign) || (tvc->fid.Fid.Vnode & 1))
	    osi_dnlc_purgedp(tvc);	/* if it (could be) a directory */
    }
    ReleaseWriteLock(&afs_xcbhash);
    if (tvp)
	afs_PutVolume(tvp, READ_LOCK);
    afs_ProcessFS(tvc, &OutStatus, areq);

    ReleaseWriteLock(&tvc->lock);
    return tvc;

}

struct vcache *
afs_GetRootVCache(struct VenusFid *afid, struct vrequest *areq,
		  afs_int32 * cached, struct volume *tvolp)
{
    afs_int32 code = 0, i, newvcache = 0, haveStatus = 0;
    afs_int32 getNewFid = 0;
    afs_uint32 start;
    struct VenusFid nfid;
    register struct vcache *tvc;
    struct server *serverp = 0;
    struct AFSFetchStatus OutStatus;
    struct AFSCallBack CallBack;
    struct AFSVolSync tsync;
    int origCBs = 0;

    start = osi_Time();

  newmtpt:
    if (!tvolp->rootVnode || getNewFid) {
	struct VenusFid tfid;

	tfid = *afid;
	tfid.Fid.Vnode = 0;	/* Means get rootfid of volume */
	origCBs = afs_allCBs;	/* ignore InitCallBackState */
	code =
	    afs_RemoteLookup(&tfid, areq, NULL, &nfid, &OutStatus, &CallBack,
			     &serverp, &tsync);
	if (code) {
	    return NULL;
	}
/*	ReleaseReadLock(&tvolp->lock);		 */
	ObtainWriteLock(&tvolp->lock, 56);
	tvolp->rootVnode = afid->Fid.Vnode = nfid.Fid.Vnode;
	tvolp->rootUnique = afid->Fid.Unique = nfid.Fid.Unique;
	ReleaseWriteLock(&tvolp->lock);
/*	ObtainReadLock(&tvolp->lock);*/
	haveStatus = 1;
    } else {
	afid->Fid.Vnode = tvolp->rootVnode;
	afid->Fid.Unique = tvolp->rootUnique;
    }

    ObtainSharedLock(&afs_xvcache, 7);
    i = VCHash(afid);
    for (tvc = afs_vhashT[i]; tvc; tvc = tvc->hnext) {
	if (!FidCmp(&(tvc->fid), afid)) {
#ifdef	AFS_OSF_ENV
	    /* Grab this vnode, possibly reactivating from the free list */
	    /* for the present (95.05.25) everything on the hash table is
	     * definitively NOT in the free list -- at least until afs_reclaim
	     * can be safely implemented */
	    int vg;
	    AFS_GUNLOCK();
	    vg = vget(AFSTOV(tvc));	/* this bumps ref count */
	    AFS_GLOCK();
	    if (vg)
		continue;
#endif /* AFS_OSF_ENV */
#ifdef AFS_DARWIN14_ENV
	    /* It'd really suck if we allowed duplicate vcaches for the 
	     * same fid to happen. Wonder if this will work? */
	    struct vnode *vp = AFSTOV(tvc);
	    if (vp->v_flag & (VXLOCK | VORECLAIM | VTERMINATE)) {
		printf("precluded FindVCache on %x (%d:%d:%d)\n",
		       vp, tvc->fid.Fid.Volume, tvc->fid.Fid.Vnode,
		       tvc->fid.Fid.Unique);
		simple_lock(&vp->v_interlock);
		SET(vp->v_flag, VTERMWANT);
		simple_unlock(&vp->v_interlock);
		(void)tsleep((caddr_t) & vp->v_ubcinfo, PINOD, "vget1", 0);
		printf("VTERMWANT ended on %x\n", vp);
		continue;
	    }
#endif
	    break;
	}
    }

    if (!haveStatus && (!tvc || !(tvc->states & CStatd))) {
	/* Mount point no longer stat'd or unknown. FID may have changed. */
#ifdef AFS_OSF_ENV
	if (tvc)
	    AFS_RELE(AFSTOV(tvc));
#endif
	tvc = NULL;
	getNewFid = 1;
	ReleaseSharedLock(&afs_xvcache);
	goto newmtpt;
    }

    if (!tvc) {
	UpgradeSToWLock(&afs_xvcache, 23);
	/* no cache entry, better grab one */
	tvc = afs_NewVCache(afid, NULL);
	newvcache = 1;
	afs_stats_cmperf.vcacheMisses++;
    } else {
	if (cached)
	    *cached = 1;
	afs_stats_cmperf.vcacheHits++;
#ifdef	AFS_OSF_ENV
	/* we already bumped the ref count in the for loop above */
#else /* AFS_OSF_ENV */
	osi_vnhold(tvc, 0);
#endif
	UpgradeSToWLock(&afs_xvcache, 24);
	if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
	    refpanic("GRVC VLRU inconsistent0");
	}
	if (tvc->vlruq.next->prev != &(tvc->vlruq)) {
	    refpanic("GRVC VLRU inconsistent1");
	}
	if (tvc->vlruq.prev->next != &(tvc->vlruq)) {
	    refpanic("GRVC VLRU inconsistent2");
	}
	QRemove(&tvc->vlruq);	/* move to lruq head */
	QAdd(&VLRU, &tvc->vlruq);
	if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
	    refpanic("GRVC VLRU inconsistent3");
	}
	if (tvc->vlruq.next->prev != &(tvc->vlruq)) {
	    refpanic("GRVC VLRU inconsistent4");
	}
	if (tvc->vlruq.prev->next != &(tvc->vlruq)) {
	    refpanic("GRVC VLRU inconsistent5");
	}
	vcachegen++;
    }

    ReleaseWriteLock(&afs_xvcache);

    if (tvc->states & CStatd) {
	return tvc;
    } else {

	ObtainReadLock(&tvc->lock);
	tvc->states &= ~CUnique;
	tvc->callback = NULL;	/* redundant, perhaps */
	ReleaseReadLock(&tvc->lock);
    }

    ObtainWriteLock(&tvc->lock, 57);

    /* It is always appropriate to throw away all the access rights? */
    afs_FreeAllAxs(&(tvc->Access));

    if (newvcache)
	tvc->states |= CForeign;
    if (tvolp->states & VRO)
	tvc->states |= CRO;
    if (tvolp->states & VBackup)
	tvc->states |= CBackup;
    /* now copy ".." entry back out of volume structure, if necessary */
    if (newvcache && (tvolp->rootVnode == afid->Fid.Vnode)
	&& (tvolp->rootUnique == afid->Fid.Unique)) {
	tvc->mvstat = 2;
    }
    if (tvc->mvstat == 2 && tvolp->dotdot.Fid.Volume != 0) {
	if (!tvc->mvid)
	    tvc->mvid = (struct VenusFid *)
		osi_AllocSmallSpace(sizeof(struct VenusFid));
	*tvc->mvid = tvolp->dotdot;
    }

    /* stat the file */
    afs_RemoveVCB(afid);

    if (!haveStatus) {
	struct VenusFid tfid;

	tfid = *afid;
	tfid.Fid.Vnode = 0;	/* Means get rootfid of volume */
	origCBs = afs_allCBs;	/* ignore InitCallBackState */
	code =
	    afs_RemoteLookup(&tfid, areq, NULL, &nfid, &OutStatus, &CallBack,
			     &serverp, &tsync);
    }

    if (code) {
	ObtainWriteLock(&afs_xcbhash, 467);
	afs_DequeueCallback(tvc);
	tvc->callback = NULL;
	tvc->states &= ~(CStatd | CUnique);
	ReleaseWriteLock(&afs_xcbhash);
	if ((tvc->states & CForeign) || (tvc->fid.Fid.Vnode & 1))
	    osi_dnlc_purgedp(tvc);	/* if it (could be) a directory */
	ReleaseWriteLock(&tvc->lock);
	ObtainReadLock(&afs_xvcache);
	AFS_FAST_RELE(tvc);
	ReleaseReadLock(&afs_xvcache);
	return NULL;
    }

    ObtainWriteLock(&afs_xcbhash, 468);
    if (origCBs == afs_allCBs) {
	tvc->states |= CTruth;
	tvc->callback = serverp;
	if (CallBack.ExpirationTime != 0) {
	    tvc->cbExpires = CallBack.ExpirationTime + start;
	    tvc->states |= CStatd;
	    tvc->states &= ~CBulkFetching;
	    afs_QueueCallback(tvc, CBHash(CallBack.ExpirationTime), tvolp);
	} else if (tvc->states & CRO) {
	    /* adapt gives us an hour. */
	    tvc->cbExpires = 3600 + osi_Time();
	     /*XXX*/ tvc->states |= CStatd;
	    tvc->states &= ~CBulkFetching;
	    afs_QueueCallback(tvc, CBHash(3600), tvolp);
	}
    } else {
	afs_DequeueCallback(tvc);
	tvc->callback = NULL;
	tvc->states &= ~(CStatd | CUnique);
	if ((tvc->states & CForeign) || (tvc->fid.Fid.Vnode & 1))
	    osi_dnlc_purgedp(tvc);	/* if it (could be) a directory */
    }
    ReleaseWriteLock(&afs_xcbhash);
    afs_ProcessFS(tvc, &OutStatus, areq);

    ReleaseWriteLock(&tvc->lock);
    return tvc;
}



/*
 * must be called with avc write-locked
 * don't absolutely have to invalidate the hint unless the dv has
 * changed, but be sure to get it right else there will be consistency bugs.
 */
afs_int32
afs_FetchStatus(struct vcache * avc, struct VenusFid * afid,
		struct vrequest * areq, struct AFSFetchStatus * Outsp)
{
    int code;
    afs_uint32 start = 0;
    register struct conn *tc;
    struct AFSCallBack CallBack;
    struct AFSVolSync tsync;
    struct volume *volp;
    XSTATS_DECLS;
    do {
	tc = afs_Conn(afid, areq, SHARED_LOCK);
	avc->quick.stamp = 0;
	avc->h1.dchint = NULL;	/* invalidate hints */
	if (tc) {
	    avc->callback = tc->srvr->server;
	    start = osi_Time();
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_FETCHSTATUS);
	    RX_AFS_GUNLOCK();
	    code =
		RXAFS_FetchStatus(tc->id, (struct AFSFid *)&afid->Fid, Outsp,
				  &CallBack, &tsync);
	    RX_AFS_GLOCK();

	    XSTATS_END_TIME;

	} else
	    code = -1;
    } while (afs_Analyze
	     (tc, code, afid, areq, AFS_STATS_FS_RPCIDX_FETCHSTATUS,
	      SHARED_LOCK, NULL));

    if (!code) {
	afs_ProcessFS(avc, Outsp, areq);
	volp = afs_GetVolume(afid, areq, READ_LOCK);
	ObtainWriteLock(&afs_xcbhash, 469);
	avc->states |= CTruth;
	if (avc->callback /* check for race */ ) {
	    if (CallBack.ExpirationTime != 0) {
		avc->cbExpires = CallBack.ExpirationTime + start;
		avc->states |= CStatd;
		avc->states &= ~CBulkFetching;
		afs_QueueCallback(avc, CBHash(CallBack.ExpirationTime), volp);
	    } else if (avc->states & CRO) {	/* ordinary callback on a read-only volume -- AFS 3.2 style */
		avc->cbExpires = 3600 + start;
		avc->states |= CStatd;
		avc->states &= ~CBulkFetching;
		afs_QueueCallback(avc, CBHash(3600), volp);
	    } else {
		afs_DequeueCallback(avc);
		avc->callback = NULL;
		avc->states &= ~(CStatd | CUnique);
		if ((avc->states & CForeign) || (avc->fid.Fid.Vnode & 1))
		    osi_dnlc_purgedp(avc);	/* if it (could be) a directory */
	    }
	} else {
	    afs_DequeueCallback(avc);
	    avc->callback = NULL;
	    avc->states &= ~(CStatd | CUnique);
	    if ((avc->states & CForeign) || (avc->fid.Fid.Vnode & 1))
		osi_dnlc_purgedp(avc);	/* if it (could be) a directory */
	}
	ReleaseWriteLock(&afs_xcbhash);
	if (volp)
	    afs_PutVolume(volp, READ_LOCK);
    } else {
	/* used to undo the local callback, but that's too extreme.
	 * There are plenty of good reasons that fetchstatus might return
	 * an error, such as EPERM.  If we have the vnode cached, statd,
	 * with callback, might as well keep track of the fact that we
	 * don't have access...
	 */
	if (code == EPERM || code == EACCES) {
	    struct axscache *ac;
	    if (avc->Access && (ac = afs_FindAxs(avc->Access, areq->uid)))
		ac->axess = 0;
	    else		/* not found, add a new one if possible */
		afs_AddAxs(avc->Access, areq->uid, 0);
	}
    }
    return code;
}

#if 0
/*
 * afs_StuffVcache
 *
 * Description:
 *	Stuff some information into the vcache for the given file.
 *
 * Parameters:
 *	afid	  : File in question.
 *	OutStatus : Fetch status on the file.
 *	CallBack  : Callback info.
 *	tc	  : RPC connection involved.
 *	areq	  : vrequest involved.
 *
 * Environment:
 *	Nothing interesting.
 */
void
afs_StuffVcache(register struct VenusFid *afid,
		struct AFSFetchStatus *OutStatus,
		struct AFSCallBack *CallBack, register struct conn *tc,
		struct vrequest *areq)
{
    register afs_int32 code, i, newvcache = 0;
    register struct vcache *tvc;
    struct AFSVolSync tsync;
    struct volume *tvp;
    struct axscache *ac;
    afs_int32 retry;

    AFS_STATCNT(afs_StuffVcache);
#ifdef IFS_VCACHECOUNT
    ifs_gvcachecall++;
#endif

  loop:
    ObtainSharedLock(&afs_xvcache, 8);

    tvc = afs_FindVCache(afid, &retry, DO_VLRU /* no stats */ );
    if (tvc && retry) {
#if	defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
	ReleaseSharedLock(&afs_xvcache);
	spunlock_psema(tvc->v.v_lock, retry, &tvc->v.v_sync, PINOD);
	goto loop;
#endif
    }

    if (!tvc) {
	/* no cache entry, better grab one */
	UpgradeSToWLock(&afs_xvcache, 25);
	tvc = afs_NewVCache(afid, NULL);
	newvcache = 1;
	ConvertWToSLock(&afs_xvcache);
    }

    ReleaseSharedLock(&afs_xvcache);
    ObtainWriteLock(&tvc->lock, 58);

    tvc->states &= ~CStatd;
    if ((tvc->states & CForeign) || (tvc->fid.Fid.Vnode & 1))
	osi_dnlc_purgedp(tvc);	/* if it (could be) a directory */

    /* Is it always appropriate to throw away all the access rights? */
    afs_FreeAllAxs(&(tvc->Access));

    /*Copy useful per-volume info */
    tvp = afs_GetVolume(afid, areq, READ_LOCK);
    if (tvp) {
	if (newvcache && (tvp->states & VForeign))
	    tvc->states |= CForeign;
	if (tvp->states & VRO)
	    tvc->states |= CRO;
	if (tvp->states & VBackup)
	    tvc->states |= CBackup;
	/*
	 * Now, copy ".." entry back out of volume structure, if
	 * necessary
	 */
	if (tvc->mvstat == 2 && tvp->dotdot.Fid.Volume != 0) {
	    if (!tvc->mvid)
		tvc->mvid = (struct VenusFid *)
		    osi_AllocSmallSpace(sizeof(struct VenusFid));
	    *tvc->mvid = tvp->dotdot;
	}
    }
    /* store the stat on the file */
    afs_RemoveVCB(afid);
    afs_ProcessFS(tvc, OutStatus, areq);
    tvc->callback = tc->srvr->server;

    /* we use osi_Time twice below.  Ideally, we would use the time at which
     * the FetchStatus call began, instead, but we don't have it here.  So we
     * make do with "now".  In the CRO case, it doesn't really matter. In
     * the other case, we hope that the difference between "now" and when the
     * call actually began execution on the server won't be larger than the
     * padding which the server keeps.  Subtract 1 second anyway, to be on
     * the safe side.  Can't subtract more because we don't know how big
     * ExpirationTime is.  Possible consistency problems may arise if the call
     * timeout period becomes longer than the server's expiration padding.  */
    ObtainWriteLock(&afs_xcbhash, 470);
    if (CallBack->ExpirationTime != 0) {
	tvc->cbExpires = CallBack->ExpirationTime + osi_Time() - 1;
	tvc->states |= CStatd;
	tvc->states &= ~CBulkFetching;
	afs_QueueCallback(tvc, CBHash(CallBack->ExpirationTime), tvp);
    } else if (tvc->states & CRO) {
	/* old-fashioned AFS 3.2 style */
	tvc->cbExpires = 3600 + osi_Time();
	 /*XXX*/ tvc->states |= CStatd;
	tvc->states &= ~CBulkFetching;
	afs_QueueCallback(tvc, CBHash(3600), tvp);
    } else {
	afs_DequeueCallback(tvc);
	tvc->callback = NULL;
	tvc->states &= ~(CStatd | CUnique);
	if ((tvc->states & CForeign) || (tvc->fid.Fid.Vnode & 1))
	    osi_dnlc_purgedp(tvc);	/* if it (could be) a directory */
    }
    ReleaseWriteLock(&afs_xcbhash);
    if (tvp)
	afs_PutVolume(tvp, READ_LOCK);

    /* look in per-pag cache */
    if (tvc->Access && (ac = afs_FindAxs(tvc->Access, areq->uid)))
	ac->axess = OutStatus->CallerAccess;	/* substitute pags */
    else			/* not found, add a new one if possible */
	afs_AddAxs(tvc->Access, areq->uid, OutStatus->CallerAccess);

    ReleaseWriteLock(&tvc->lock);
    afs_Trace4(afs_iclSetp, CM_TRACE_STUFFVCACHE, ICL_TYPE_POINTER, tvc,
	       ICL_TYPE_POINTER, tvc->callback, ICL_TYPE_INT32,
	       tvc->cbExpires, ICL_TYPE_INT32, tvc->cbExpires - osi_Time());
    /*
     * Release ref count... hope this guy stays around...
     */
    afs_PutVCache(tvc);
}				/*afs_StuffVcache */
#endif

/*
 * afs_PutVCache
 *
 * Description:
 *	Decrements the reference count on a cache entry.
 *
 * Parameters:
 *	avc : Pointer to the cache entry to decrement.
 *
 * Environment:
 *	Nothing interesting.
 */
void
afs_PutVCache(register struct vcache *avc)
{
    AFS_STATCNT(afs_PutVCache);
    /*
     * Can we use a read lock here?
     */
    ObtainReadLock(&afs_xvcache);
    AFS_FAST_RELE(avc);
    ReleaseReadLock(&afs_xvcache);
}				/*afs_PutVCache */

/*
 * afs_FindVCache
 *
 * Description:
 *	Find a vcache entry given a fid.
 *
 * Parameters:
 *	afid : Pointer to the fid whose cache entry we desire.
 *      retry: (SGI-specific) tell the caller to drop the lock on xvcache,
 *             unlock the vnode, and try again.
 *      flags: bit 1 to specify whether to compute hit statistics.  Not
 *             set if FindVCache is called as part of internal bookkeeping.
 *
 * Environment:
 *	Must be called with the afs_xvcache lock at least held at
 *	the read level.  In order to do the VLRU adjustment, the xvcache lock
 *      must be shared-- we upgrade it here.
 */

struct vcache *
afs_FindVCache(struct VenusFid *afid, afs_int32 * retry, afs_int32 flag)
{

    register struct vcache *tvc;
    afs_int32 i;

    AFS_STATCNT(afs_FindVCache);

    i = VCHash(afid);
    for (tvc = afs_vhashT[i]; tvc; tvc = tvc->hnext) {
	if (FidMatches(afid, tvc)) {
#ifdef  AFS_OSF_ENV
	    /* Grab this vnode, possibly reactivating from the free list */
	    int vg;
	    AFS_GUNLOCK();
	    vg = vget(AFSTOV(tvc));
	    AFS_GLOCK();
	    if (vg)
		continue;
#endif /* AFS_OSF_ENV */
	    break;
	}
    }

    /* should I have a read lock on the vnode here? */
    if (tvc) {
	if (retry)
	    *retry = 0;
#if !defined(AFS_OSF_ENV)
	osi_vnhold(tvc, retry);	/* already held, above */
	if (retry && *retry)
	    return 0;
#endif
	/*
	 * only move to front of vlru if we have proper vcache locking)
	 */
	if (flag & DO_VLRU) {
	    if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
		refpanic("FindVC VLRU inconsistent1");
	    }
	    if (tvc->vlruq.next->prev != &(tvc->vlruq)) {
		refpanic("FindVC VLRU inconsistent1");
	    }
	    if (tvc->vlruq.prev->next != &(tvc->vlruq)) {
		refpanic("FindVC VLRU inconsistent2");
	    }
	    UpgradeSToWLock(&afs_xvcache, 26);
	    QRemove(&tvc->vlruq);
	    QAdd(&VLRU, &tvc->vlruq);
	    ConvertWToSLock(&afs_xvcache);
	    if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
		refpanic("FindVC VLRU inconsistent1");
	    }
	    if (tvc->vlruq.next->prev != &(tvc->vlruq)) {
		refpanic("FindVC VLRU inconsistent2");
	    }
	    if (tvc->vlruq.prev->next != &(tvc->vlruq)) {
		refpanic("FindVC VLRU inconsistent3");
	    }
	}
	vcachegen++;
    }

    if (flag & DO_STATS) {
	if (tvc)
	    afs_stats_cmperf.vcacheHits++;
	else
	    afs_stats_cmperf.vcacheMisses++;
	if (afs_IsPrimaryCellNum(afid->Cell))
	    afs_stats_cmperf.vlocalAccesses++;
	else
	    afs_stats_cmperf.vremoteAccesses++;
    }
#ifdef AFS_LINUX22_ENV
    if (tvc && (tvc->states & CStatd))
	vcache2inode(tvc);	/* mainly to reset i_nlink */
#endif
#ifdef AFS_DARWIN_ENV
    if (tvc)
	osi_VM_Setup(tvc, 0);
#endif
    return tvc;
}				/*afs_FindVCache */

/*
 * afs_NFSFindVCache
 *
 * Description:
 *	Find a vcache entry given a fid. Does a wildcard match on what we
 *	have for the fid. If more than one entry, don't return anything.
 *
 * Parameters:
 *	avcp : Fill in pointer if we found one and only one.
 *	afid : Pointer to the fid whose cache entry we desire.
 *      retry: (SGI-specific) tell the caller to drop the lock on xvcache,
 *             unlock the vnode, and try again.
 *      flags: bit 1 to specify whether to compute hit statistics.  Not
 *             set if FindVCache is called as part of internal bookkeeping.
 *
 * Environment:
 *	Must be called with the afs_xvcache lock at least held at
 *	the read level.  In order to do the VLRU adjustment, the xvcache lock
 *      must be shared-- we upgrade it here.
 *
 * Return value:
 *	number of matches found.
 */

int afs_duplicate_nfs_fids = 0;

afs_int32
afs_NFSFindVCache(struct vcache **avcp, struct VenusFid *afid)
{
    register struct vcache *tvc;
    afs_int32 i;
    afs_int32 count = 0;
    struct vcache *found_tvc = NULL;

    AFS_STATCNT(afs_FindVCache);

#if defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
  loop:
#endif

    ObtainSharedLock(&afs_xvcache, 331);

    i = VCHash(afid);
    for (tvc = afs_vhashT[i]; tvc; tvc = tvc->hnext) {
	/* Match only on what we have.... */
	if (((tvc->fid.Fid.Vnode & 0xffff) == afid->Fid.Vnode)
	    && (tvc->fid.Fid.Volume == afid->Fid.Volume)
	    && ((tvc->fid.Fid.Unique & 0xffffff) == afid->Fid.Unique)
	    && (tvc->fid.Cell == afid->Cell)) {
#ifdef  AFS_OSF_ENV
	    /* Grab this vnode, possibly reactivating from the free list */
	    int vg;
	    AFS_GUNLOCK();
	    vg = vget(AFSTOV(tvc));
	    AFS_GLOCK();
	    if (vg) {
		/* This vnode no longer exists. */
		continue;
	    }
#endif /* AFS_OSF_ENV */
	    count++;
	    if (found_tvc) {
		/* Duplicates */
#ifdef AFS_OSF_ENV
		/* Drop our reference counts. */
		vrele(AFSTOV(tvc));
		vrele(AFSTOV(found_tvc));
#endif
		afs_duplicate_nfs_fids++;
		ReleaseSharedLock(&afs_xvcache);
		return count;
	    }
	    found_tvc = tvc;
	}
    }

    tvc = found_tvc;
    /* should I have a read lock on the vnode here? */
    if (tvc) {
#if defined(AFS_SGI_ENV) && !defined(AFS_SGI53_ENV)
	afs_int32 retry = 0;
	osi_vnhold(tvc, &retry);
	if (retry) {
	    count = 0;
	    found_tvc = (struct vcache *)0;
	    ReleaseSharedLock(&afs_xvcache);
	    spunlock_psema(tvc->v.v_lock, retry, &tvc->v.v_sync, PINOD);
	    goto loop;
	}
#else
#if !defined(AFS_OSF_ENV)
	osi_vnhold(tvc, (int *)0);	/* already held, above */
#endif
#endif
	/*
	 * We obtained the xvcache lock above.
	 */
	if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
	    refpanic("FindVC VLRU inconsistent1");
	}
	if (tvc->vlruq.next->prev != &(tvc->vlruq)) {
	    refpanic("FindVC VLRU inconsistent1");
	}
	if (tvc->vlruq.prev->next != &(tvc->vlruq)) {
	    refpanic("FindVC VLRU inconsistent2");
	}
	UpgradeSToWLock(&afs_xvcache, 568);
	QRemove(&tvc->vlruq);
	QAdd(&VLRU, &tvc->vlruq);
	ConvertWToSLock(&afs_xvcache);
	if ((VLRU.next->prev != &VLRU) || (VLRU.prev->next != &VLRU)) {
	    refpanic("FindVC VLRU inconsistent1");
	}
	if (tvc->vlruq.next->prev != &(tvc->vlruq)) {
	    refpanic("FindVC VLRU inconsistent2");
	}
	if (tvc->vlruq.prev->next != &(tvc->vlruq)) {
	    refpanic("FindVC VLRU inconsistent3");
	}
    }
    vcachegen++;

    if (tvc)
	afs_stats_cmperf.vcacheHits++;
    else
	afs_stats_cmperf.vcacheMisses++;
    if (afs_IsPrimaryCellNum(afid->Cell))
	afs_stats_cmperf.vlocalAccesses++;
    else
	afs_stats_cmperf.vremoteAccesses++;

    *avcp = tvc;		/* May be null */

    ReleaseSharedLock(&afs_xvcache);
    return (tvc ? 1 : 0);

}				/*afs_NFSFindVCache */




/*
 * afs_vcacheInit
 *
 * Initialize vcache related variables
 */
void
afs_vcacheInit(int astatSize)
{
    register struct vcache *tvp;
    int i;
#if	defined(AFS_OSF_ENV)
    if (!afs_maxvcount) {
#if	defined(AFS_OSF30_ENV)
	afs_maxvcount = max_vnodes / 2;	/* limit ourselves to half the total */
#else
	afs_maxvcount = nvnode / 2;	/* limit ourselves to half the total */
#endif
	if (astatSize < afs_maxvcount) {
	    afs_maxvcount = astatSize;
	}
    }
#else /* AFS_OSF_ENV */
    freeVCList = NULL;
#endif

    RWLOCK_INIT(&afs_xvcache, "afs_xvcache");
    LOCK_INIT(&afs_xvcb, "afs_xvcb");

#if	!defined(AFS_OSF_ENV)
#ifdef AFS_LINUX26_ENV
    printf("old style would have needed %d contiguous bytes\n", astatSize *
	   sizeof(struct vcache));
    Initial_freeVCList = freeVCList = tvp = (struct vcache *)
	afs_osi_Alloc(sizeof(struct vcache));
    for (i = 0; i < astatSize; i++) {
	tvp->nextfree = (struct vcache *) afs_osi_Alloc(sizeof(struct vcache));
	tvp = tvp->nextfree;
    }
    tvp->nextfree = NULL;
#else
    /* Allocate and thread the struct vcache entries */
    tvp = (struct vcache *)afs_osi_Alloc(astatSize * sizeof(struct vcache));
    memset((char *)tvp, 0, sizeof(struct vcache) * astatSize);

    Initial_freeVCList = tvp;
    freeVCList = &(tvp[0]);
    for (i = 0; i < astatSize - 1; i++) {
	tvp[i].nextfree = &(tvp[i + 1]);
    }
    tvp[astatSize - 1].nextfree = NULL;
#ifdef  KERNEL_HAVE_PIN
    pin((char *)tvp, astatSize * sizeof(struct vcache));	/* XXX */
#endif
#endif
#endif

#if defined(AFS_SGI_ENV)
    for (i = 0; i < astatSize; i++) {
	char name[METER_NAMSZ];
	struct vcache *tvc = &tvp[i];

	tvc->v.v_number = ++afsvnumbers;
	tvc->vc_rwlockid = OSI_NO_LOCKID;
	initnsema(&tvc->vc_rwlock, 1,
		  makesname(name, "vrw", tvc->v.v_number));
#ifndef	AFS_SGI53_ENV
	initnsema(&tvc->v.v_sync, 0, makesname(name, "vsy", tvc->v.v_number));
#endif
#ifndef AFS_SGI62_ENV
	initnlock(&tvc->v.v_lock, makesname(name, "vlk", tvc->v.v_number));
#endif /* AFS_SGI62_ENV */
    }
#endif

    QInit(&VLRU);


}

/*
 * shutdown_vcache
 *
 */
void
shutdown_vcache(void)
{
    int i;
    struct afs_cbr *tsp, *nsp;
    /*
     * XXX We may potentially miss some of the vcaches because if when there're no
     * free vcache entries and all the vcache entries are active ones then we allocate
     * an additional one - admittedly we almost never had that occur.
     */

    {
	register struct afs_q *tq, *uq;
	register struct vcache *tvc;
	for (tq = VLRU.prev; tq != &VLRU; tq = uq) {
	    tvc = QTOV(tq);
	    uq = QPrev(tq);
	    if (tvc->mvid) {
		osi_FreeSmallSpace(tvc->mvid);
		tvc->mvid = (struct VenusFid *)0;
	    }
#ifdef	AFS_AIX_ENV
	    aix_gnode_rele(AFSTOV(tvc));
#endif
	    if (tvc->linkData) {
		afs_osi_Free(tvc->linkData, strlen(tvc->linkData) + 1);
		tvc->linkData = 0;
	    }
	}
	/*
	 * Also free the remaining ones in the Cache
	 */
	for (i = 0; i < VCSIZE; i++) {
	    for (tvc = afs_vhashT[i]; tvc; tvc = tvc->hnext) {
		if (tvc->mvid) {
		    osi_FreeSmallSpace(tvc->mvid);
		    tvc->mvid = (struct VenusFid *)0;
		}
#ifdef	AFS_AIX_ENV
		if (tvc->v.v_gnode)
		    afs_osi_Free(tvc->v.v_gnode, sizeof(struct gnode));
#ifdef	AFS_AIX32_ENV
		if (tvc->segid) {
		    AFS_GUNLOCK();
		    vms_delete(tvc->segid);
		    AFS_GLOCK();
		    tvc->segid = tvc->vmh = NULL;
		    if (VREFCOUNT(tvc))
			osi_Panic("flushVcache: vm race");
		}
		if (tvc->credp) {
		    crfree(tvc->credp);
		    tvc->credp = NULL;
		}
#endif
#endif
#if	defined(AFS_SUN5_ENV)
		if (tvc->credp) {
		    crfree(tvc->credp);
		    tvc->credp = NULL;
		}
#endif
		if (tvc->linkData) {
		    afs_osi_Free(tvc->linkData, strlen(tvc->linkData) + 1);
		    tvc->linkData = 0;
		}

		afs_FreeAllAxs(&(tvc->Access));
	    }
	    afs_vhashT[i] = 0;
	}
    }
    /*
     * Free any leftover callback queue
     */
    for (tsp = afs_cbrSpace; tsp; tsp = nsp) {
	nsp = tsp->next;
	afs_osi_Free((char *)tsp, AFS_NCBRS * sizeof(struct afs_cbr));
    }
    afs_cbrSpace = 0;

#ifdef AFS_LINUX26_ENV
    {
	struct vcache *tvp = Initial_freeVCList;
	while (tvp) {
	    struct vcache *next = tvp->nextfree;
	    
	    afs_osi_Free(tvp, sizeof(struct vcache));
	    tvp = next;
	}
    }
#else
#ifdef  KERNEL_HAVE_PIN
    unpin(Initial_freeVCList, afs_cacheStats * sizeof(struct vcache));
#endif
#if	!defined(AFS_OSF_ENV)
    afs_osi_Free(Initial_freeVCList, afs_cacheStats * sizeof(struct vcache));
#endif
#endif

#if	!defined(AFS_OSF_ENV)
    freeVCList = Initial_freeVCList = 0;
#endif
    RWLOCK_INIT(&afs_xvcache, "afs_xvcache");
    LOCK_INIT(&afs_xvcb, "afs_xvcb");
    QInit(&VLRU);

}
