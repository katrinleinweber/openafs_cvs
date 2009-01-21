/*
 * COPYRIGHT  ©  2000
 * THE REGENTS OF THE UNIVERSITY OF MICHIGAN
 * ALL RIGHTS RESERVED
 * 
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 * 
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY O 
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */
 
 /*
 * Portions Copyright (c) 2008
 * The Linux Box Corporation
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of the Linux Box
 * Corporation is not used in any advertising or publicity
 * pertaining to the use or distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * Linux Box Corporation is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * This software is provided as is, without representation
 * from the Linux Box Corporation as to its fitness for any
 * purpose, and without warranty by the Linux Box Corporation
 * of any kind, either express or implied, including
 * without limitation the implied warranties of
 * merchantability and fitness for a particular purpose.  The
 * Linux Box Corporation shall not be liable for any damages, 
 * including special, indirect, incidental, or consequential 
 * damages, with respect to any claim arising out of or in 
 * connection with the use of the software, even if it has been 
 * or is hereafter advised of the possibility of such damages.
 */


#include <afsconfig.h>
#include "afs/param.h"

#if defined(AFS_CACHE_BYPASS)

#include "afs/afs_bypasscache.h"

/*
 * afs_bypasscache.c
 *
 */
#include "afs/sysincludes.h" /* Standard vendor system headers */
#include "afs/afsincludes.h" /* Afs-based standard headers */
#include "afs/afs_stats.h"   /* statistics */
#include "afs/nfsclient.h"  
#include "rx/rx_globals.h"

#if defined(AFS_LINUX26_ENV)
#define LockPage(pp) lock_page(pp)
#define UnlockPage(pp) unlock_page(pp)
#endif
#define AFS_KMAP_ATOMIC

#ifndef afs_min
#define afs_min(A,B) ((A)<(B)) ? (A) : (B)
#endif

/* conditional GLOCK macros */
#define COND_GLOCK(var)	\
	do { \
		var = ISAFS_GLOCK(); \
		if(!var) \
			RX_AFS_GLOCK(); \
	} while(0);
	
#define COND_RE_GUNLOCK(var) \
	do { \
		if(var)	\
			RX_AFS_GUNLOCK(); \
	} while(0);


/* conditional GUNLOCK macros */

#define COND_GUNLOCK(var) \
	do {	\
		var = ISAFS_GLOCK(); \
		if(var)	\
			RX_AFS_GUNLOCK(); \
	} while(0);

#define COND_RE_GLOCK(var) \
	do { \
		if(var)	\
			RX_AFS_GLOCK();	\
	} while(0);


int cache_bypass_strategy 	= 	NEVER_BYPASS_CACHE;
int cache_bypass_threshold  =  	AFS_CACHE_BYPASS_DISABLED; /* file size > threshold triggers bypass */
int cache_bypass_prefetch = 1;	/* Should we do prefetching ? */

extern afs_rwlock_t afs_xcbhash;

/*
 * This is almost exactly like the PFlush() routine in afs_pioctl.c,
 * but that routine is static.  We are about to change a file from
 * normal caching to bypass it's caching.  Therefore, we want to
 * free up any cache space in use by the file, and throw out any
 * existing VM pages for the file.  We keep track of the number of
 * times we go back and forth from caching to bypass.
 */
void afs_TransitionToBypass(register struct vcache *avc, register struct AFS_UCRED *acred, int aflags)
{

    afs_int32 code;
    struct vrequest treq;
    int setDesire = 0;
    int setManual = 0;

    if(!avc)
	return;
		
    if(avc->states & FCSBypass)
	osi_Panic("afs_TransitionToBypass: illegal transition to bypass--already FCSBypass\n");		
		
    if(aflags & TRANSChangeDesiredBit)
	setDesire = 1;
    if(aflags & TRANSSetManualBit)
	setManual = 1;
		
#ifdef AFS_BOZONLOCK_ENV
    afs_BozonLock(&avc->pvnLock, avc);	/* Since afs_TryToSmush will do a pvn_vptrunc */
#else
	AFS_GLOCK();	
#endif
    ObtainWriteLock(&avc->lock, 925);
	
    /* If we never cached this, just change state */
    if(setDesire && (!avc->cachingStates & FCSBypass)) {
	avc->states |= FCSBypass;
	goto done;
    }
	/* cg2v, try to store any chunks not written 20071204 */
	if (avc->execsOrWriters > 0) { 
		code = afs_InitReq(&treq, acred);
		if(!code)
			code = afs_StoreAllSegments(avc, &treq, AFS_SYNC | AFS_LASTSTORE); 
	}
#if 0
	/* also cg2v, don't dequeue the callback */
    ObtainWriteLock(&afs_xcbhash, 956);	
    afs_DequeueCallback(avc);
	ReleaseWriteLock(&afs_xcbhash);
#endif	
    avc->states &= ~(CStatd | CDirty);	/* next reference will re-stat cache entry */
    /* now find the disk cache entries */
    afs_TryToSmush(avc, acred, 1);
    osi_dnlc_purgedp(avc);
    if (avc->linkData && !(avc->states & CCore)) {
	afs_osi_Free(avc->linkData, strlen(avc->linkData) + 1);
	avc->linkData = NULL;
    }		
	
    avc->cachingStates |= FCSBypass;    /* Set the bypass flag */
    if(setDesire)
	avc->cachingStates |= FCSDesireBypass;
    if(setManual)
	avc->cachingStates |= FCSManuallySet;
    avc->cachingTransitions++;

done:		
    ReleaseWriteLock(&avc->lock);
#ifdef AFS_BOZONLOCK_ENV
    afs_BozonUnlock(&avc->pvnLock, avc);
#else
	AFS_GUNLOCK();
#endif
}

/*
 * This is almost exactly like the PFlush() routine in afs_pioctl.c,
 * but that routine is static.  We are about to change a file from
 * bypassing caching to normal caching.  Therefore, we want to
 * throw out any existing VM pages for the file.  We keep track of
 * the number of times we go back and forth from caching to bypass.
 */
void afs_TransitionToCaching(register struct vcache *avc, register struct AFS_UCRED *acred, int aflags)
{
    int resetDesire = 0;
    int setManual = 0;

    if(!avc)
	return;
		
    if(!avc->states & FCSBypass)
	osi_Panic("afs_TransitionToCaching: illegal transition to caching--already caching\n");		
		
    if(aflags & TRANSChangeDesiredBit)
	resetDesire = 1;
    if(aflags & TRANSSetManualBit)
	setManual = 1;

#ifdef AFS_BOZONLOCK_ENV
    afs_BozonLock(&avc->pvnLock, avc);	/* Since afs_TryToSmush will do a pvn_vptrunc */
#else
	AFS_GLOCK();	
#endif
    ObtainWriteLock(&avc->lock, 926);
	
    /* Ok, we actually do need to flush */
    ObtainWriteLock(&afs_xcbhash, 957);
    afs_DequeueCallback(avc);
    avc->states &= ~(CStatd | CDirty);	/* next reference will re-stat cache entry */
    ReleaseWriteLock(&afs_xcbhash);
    /* now find the disk cache entries */
    afs_TryToSmush(avc, acred, 1);
    osi_dnlc_purgedp(avc);
    if (avc->linkData && !(avc->states & CCore)) {
	afs_osi_Free(avc->linkData, strlen(avc->linkData) + 1);
	avc->linkData = NULL;
    }
		
    avc->cachingStates &= ~(FCSBypass);    /* Reset the bypass flag */
    if (resetDesire)
	avc->cachingStates &= ~(FCSDesireBypass);
    if (setManual)
	avc->cachingStates |= FCSManuallySet;
    avc->cachingTransitions++;

    ReleaseWriteLock(&avc->lock);
#ifdef AFS_BOZONLOCK_ENV
    afs_BozonUnlock(&avc->pvnLock, avc);
#else
	AFS_GUNLOCK();
#endif
}

/* In the case where there's an error in afs_NoCacheFetchProc or
 * afs_PrefetchNoCache, all of the pages they've been passed need
 * to be unlocked.
 */
#if defined(AFS_LINUX24_ENV)
#define unlock_pages(auio) \
    do { \
	struct iovec *ciov;	\
	struct page *pp; \
	afs_int32 iovmax; \
	afs_int32 iovno = 0; \
	ciov = auio->uio_iov; \
	iovmax = auio->uio_iovcnt - 1;	\
	pp = (struct page*) ciov->iov_base;	\
	afs_warn("BYPASS: Unlocking pages...");	\
	while(1) { \
	    if(pp != NULL && PageLocked(pp)) \
		UnlockPage(pp);	\
	    iovno++; \
	    if(iovno > iovmax) \
		break; \
	    ciov = (auio->uio_iov + iovno);	\
	    pp = (struct page*) ciov->iov_base;	\
	} \
	afs_warn("Pages Unlocked.\n"); \
    } while(0);
#else
#ifdef UKERNEL
#define unlock_pages(auio) \
	 do { } while(0);
#else
#error AFS_CACHE_BYPASS not implemented on this platform
#endif
#endif

/* no-cache prefetch routine */
static afs_int32
afs_NoCacheFetchProc(register struct rx_call *acall, 
                     register struct vcache *avc, 
					 register uio_t *auio, 
                     afs_int32 release_pages)
{
    afs_int32 length;
    afs_int32 code;
    int tlen;
    int moredata, iovno, iovoff, iovmax, clen, result, locked;
    struct iovec *ciov;
    struct page *pp;
    char *address;
#ifdef AFS_KMAP_ATOMIC
    char *page_buffer = osi_Alloc(PAGE_SIZE);
#else
    char *page_buffer = NULL;
#endif

    ciov = auio->uio_iov;
    pp = (struct page*) ciov->iov_base;
    iovmax = auio->uio_iovcnt - 1;
    iovno = iovoff = result = 0;	
    do {

	COND_GUNLOCK(locked);
	code = rx_Read(acall, (char *)&length, sizeof(afs_int32));
	COND_RE_GLOCK(locked);

	if (code != sizeof(afs_int32)) {
	    result = 0;
	    afs_warn("Preread error. code: %d instead of %d\n",
		code, sizeof(afs_int32));
	    unlock_pages(auio);
	    goto done;
	} else
	    length = ntohl(length);		
					
	/*
	 * The fetch protocol is extended for the AFS/DFS translator
	 * to allow multiple blocks of data, each with its own length,
	 * to be returned. As long as the top bit is set, there are more
	 * blocks expected.
	 *
	 * We do not do this for AFS file servers because they sometimes
	 * return large negative numbers as the transfer size.
	 */
	if (avc->states & CForeign) {
	    moredata = length & 0x80000000;
	    length &= ~0x80000000;
	} else {
	    moredata = 0;
	}
				
	while (length > 0) {

	    clen = ciov->iov_len - iovoff; 
	    tlen = afs_min(length, clen);
#ifdef AFS_LINUX24_ENV
#ifndef AFS_KMAP_ATOMIC
	    if(pp)
		address = kmap(pp);
	    else { 
		/* rx doesn't provide an interface to simply advance
		   or consume n bytes.  for now, allocate a PAGE_SIZE 
		   region of memory to receive bytes in the case that 
		   there were holes in readpages */
		if(page_buffer == NULL)
		    page_buffer = osi_Alloc(PAGE_SIZE);
		    address = page_buffer;
		}
#else
	    address = page_buffer;
#endif
#else
#ifndef UKERNEL
#error AFS_CACHE_BYPASS not implemented on this platform
#endif
#endif /* LINUX24 */
	    COND_GUNLOCK(locked);
	    code = rx_Read(acall, address, tlen);
	    COND_RE_GLOCK(locked);
		
	    if (code < 0) {
	        afs_warn("afs_NoCacheFetchProc: rx_Read error. Return code was %d\n", code);
		result = 0;
		unlock_pages(auio);
		goto done;
	    } else if (code == 0) {
		result = 0;
		afs_warn("afs_NoCacheFetchProc: rx_Read returned zero. Aborting.\n");
		unlock_pages(auio);
		goto done;
	    }
	    length -= code;
	    tlen -= code;
		
	    if(tlen > 0) {
		iovoff += code;
		address += code;
			
	    } else {
#ifdef AFS_LINUX24_ENV
#ifdef AFS_KMAP_ATOMIC
		if(pp) {
		    address = kmap_atomic(pp, KM_USER0);
		    memcpy(address, page_buffer, PAGE_SIZE);
		    kunmap_atomic(address, KM_USER0);
		}
#endif
#else
#ifndef UKERNEL
#error AFS_CACHE_BYPASS not implemented on this platform
#endif
#endif /* LINUX 24 */
			/* we filled a page, conditionally release it */
			if(release_pages && ciov->iov_base) {
				/* this is appropriate when no caller intends to unlock
				 * and release the page */
#ifdef AFS_LINUX24_ENV
				SetPageUptodate(pp);
				if(PageLocked(pp))
					UnlockPage(pp);	
				else
					afs_warn("afs_NoCacheFetchProc: page not locked at iovno %d!\n", iovno);
				
#ifndef AFS_KMAP_ATOMIC
				kunmap(pp);		
#endif
#else
#ifndef UKERNEL
#error AFS_CACHE_BYPASS not implemented on this platform
#endif
#endif /* LINUX24 */
			}
			/* and carry uio_iov */
			iovno++;
			if(iovno > iovmax) goto done;
			
			ciov = (auio->uio_iov + iovno);
			pp = (struct page*) ciov->iov_base;
			iovoff = 0;
	    }
	}
    } while (moredata);
	
done:
    if(page_buffer)
    	osi_Free(page_buffer, PAGE_SIZE);
    return result;
}


/* dispatch a no-cache read request */
afs_int32
afs_ReadNoCache(register struct vcache *avc, 
				register struct nocache_read_request *bparms, 
				struct AFS_UCRED *acred)
{
    afs_int32 code;
    afs_int32 bcnt;
    struct brequest *breq;
    struct vrequest *areq;
		
    /* the reciever will free this */
    areq = osi_Alloc(sizeof(struct vrequest));
	
    if (avc && avc->vc_error) {
		code = EIO;
		afs_warn("afs_ReadNoCache VCache Error!\n");
		goto cleanup;
    }
    if ((code = afs_InitReq(areq, acred))) {
		afs_warn("afs_ReadNoCache afs_InitReq error!\n");
		goto cleanup;
    }

    AFS_GLOCK();		
    code = afs_VerifyVCache(avc, areq);
    AFS_GUNLOCK();
	
    if (code) {
		code = afs_CheckCode(code, areq, 11);	/* failed to get it */
		afs_warn("afs_ReadNoCache Failed to verify VCache!\n");
		goto cleanup;
    }
	
    bparms->areq = areq;
	
    /* and queue this one */
    bcnt = 1;
    AFS_GLOCK();
    while(bcnt < 20) {
    	breq = afs_BQueue(BOP_FETCH_NOCACHE, avc, B_DONTWAIT, 0, acred, 1, 1, bparms);
		if(breq != 0) {
			code = 0;
			break;
		}	
		afs_osi_Wait(10 * bcnt, 0, 0);
    }
    AFS_GUNLOCK();
    
    if(!breq) {
    	code = EBUSY;
		goto cleanup;
    }

    return code;

cleanup:
    /* If there's a problem before we queue the request, we need to
     * do everything that would normally happen when the request was
     * processed, like unlocking the pages and freeing memory.
     */
#ifdef AFS_LINUX24_ENV
    unlock_pages(bparms->auio);
#else
#ifndef UKERNEL
#error AFS_CACHE_BYPASS not implemented on this platform
#endif
#endif
    osi_Free(areq, sizeof(struct vrequest));
    osi_Free(bparms->auio->uio_iov, bparms->auio->uio_iovcnt * sizeof(struct iovec));	
    osi_Free(bparms->auio, sizeof(uio_t));
    osi_Free(bparms, sizeof(struct nocache_read_request));
    return code;

}


/* Cannot have static linkage--called from BPrefetch (afs_daemons) */
afs_int32
afs_PrefetchNoCache(register struct vcache *avc, 
					register struct AFS_UCRED *acred,
					register struct nocache_read_request *bparms)
{
    uio_t *auio;
    struct iovec *iovecp;
    struct vrequest *areq;
    afs_int32 code, length_hi, bytes, locked;    
	
    register struct afs_conn *tc;
    afs_int32 i;
    struct rx_call *tcall;
    struct tlocal1 {
		struct AFSVolSync tsync;
		struct AFSFetchStatus OutStatus;
		struct AFSCallBack CallBack;
    };
    struct tlocal1 *tcallspec;	
			
    auio = bparms->auio;
    areq = bparms->areq;
    iovecp = auio->uio_iov;	
	
    tcallspec = (struct tlocal1 *) osi_Alloc(sizeof(struct tlocal1));
    do {
		tc = afs_Conn(&avc->fid, areq, SHARED_LOCK /* ignored */);
		if (tc) {
			avc->callback = tc->srvr->server;
			i = osi_Time();
			tcall = rx_NewCall(tc->id);
#ifdef AFS_64BIT_CLIENT
			if(!afs_serverHasNo64Bit(tc)) {
				code = StartRXAFS_FetchData64(tcall,
											  (struct AFSFid *) &avc->fid.Fid,
											  auio->uio_offset, 
											  bparms->length);
				if (code == 0) {
					
					COND_GUNLOCK(locked);
					bytes = rx_Read(tcall, (char *)&length_hi, sizeof(afs_int32));
					COND_RE_GLOCK(locked);
					
					if (bytes != sizeof(afs_int32)) {
						length_hi = 0;
						code = rx_Error(tcall);
						COND_GUNLOCK(locked);
						code = rx_EndCall(tcall, code);
						COND_RE_GLOCK(locked);
						tcall = (struct rx_call *)0;
					}
				}					      
				if (code == RXGEN_OPCODE || afs_serverHasNo64Bit(tc)) {
					if (auio->uio_offset > 0x7FFFFFFF) {
						code = EFBIG;
					} else {
						afs_int32 pos;
						pos = auio->uio_offset;
						COND_GUNLOCK(locked);
						if (!tcall)
							tcall = rx_NewCall(tc->id);						
						code = StartRXAFS_FetchData(tcall,
													(struct AFSFid *) &avc->fid.Fid,
													pos, bparms->length);
						COND_RE_GLOCK(locked);
					}
					afs_serverSetNo64Bit(tc);
				}
			} /* afs_serverHasNo64Bit */
#else
			code = StartRXAFS_FetchData(tcall,
										(struct AFSFid *) &avc->fid.Fid,
										auio->uio_offset, bparms->length);
#endif

			if (code == 0) {
				code = afs_NoCacheFetchProc(tcall, avc, auio, 
											1 /* release_pages */);
			} else {
				afs_warn("BYPASS: StartRXAFS_FetchData failed: %d\n", code);
				unlock_pages(auio);
				goto done;
			}
			if (code == 0) {
				code = EndRXAFS_FetchData(tcall,
										  &tcallspec->OutStatus,
										  &tcallspec->CallBack,
										  &tcallspec->tsync);
			} else {
				afs_warn("BYPASS: NoCacheFetchProc failed: %d\n", code);
			}
			code = rx_EndCall(tcall, code);
		}
		else {
			afs_warn("BYPASS: No connection.\n");
			code = -1;
#ifdef AFS_LINUX24_ENV
			unlock_pages(auio);
#else
#ifndef UKERNEL
#error AFS_CACHE_BYPASS not implemented on this platform
#endif
#endif
			goto done;
		}
    } while (afs_Analyze(tc, code, &avc->fid, areq,
						 AFS_STATS_FS_RPCIDX_FETCHDATA,
						 SHARED_LOCK,0));
done:
    /*
     * Copy appropriate fields into vcache
     */

    afs_ProcessFS(avc, &tcallspec->OutStatus, areq);

    osi_Free(areq, sizeof(struct vrequest));
    osi_Free(tcallspec, sizeof(struct tlocal1));
    osi_Free(iovecp, auio->uio_iovcnt * sizeof(struct iovec));	
    osi_Free(bparms, sizeof(struct nocache_read_request));
    osi_Free(auio, sizeof(uio_t));
    return code;
}

#endif /* AFS_CACHE_BYPASS */
