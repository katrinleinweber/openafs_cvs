/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include "../afs/param.h"       /* Should be always first */
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/afs/DARWIN/osi_sleep.c,v 1.2 2001/07/05 15:20:02 shadow Exp $");

#include "../afs/sysincludes.h" /* Standard vendor system headers */
#include "../afs/afsincludes.h" /* Afs-based standard headers */
#include "../afs/afs_stats.h"   /* afs statistics */


static int osi_TimedSleep(char *event, afs_int32 ams, int aintok);
void afs_osi_Wakeup(char *event);
void afs_osi_Sleep(char *event);

static char waitV;


void afs_osi_InitWaitHandle(struct afs_osi_WaitHandle *achandle)
{
    AFS_STATCNT(osi_InitWaitHandle);
    achandle->proc = (caddr_t) 0;
}

/* cancel osi_Wait */
void afs_osi_CancelWait(struct afs_osi_WaitHandle *achandle)
{
    caddr_t proc;

    AFS_STATCNT(osi_CancelWait);
    proc = achandle->proc;
    if (proc == 0) return;
    achandle->proc = (caddr_t) 0;   /* so dude can figure out he was signalled */
    afs_osi_Wakeup(&waitV);
}

/* afs_osi_Wait
 * Waits for data on ahandle, or ams ms later.  ahandle may be null.
 * Returns 0 if timeout and EINTR if signalled.
 */
int afs_osi_Wait(afs_int32 ams, struct afs_osi_WaitHandle *ahandle, int aintok)
{
    int code;
    afs_int32 endTime, tid;
    struct proc *p=current_proc();

    AFS_STATCNT(osi_Wait);
    endTime = osi_Time() + (ams/1000);
    if (ahandle)
	ahandle->proc = (caddr_t)p;
    do {
	AFS_ASSERT_GLOCK();
	code = 0;
	code = osi_TimedSleep(&waitV, ams, aintok);

	if (code) break;        /* if something happened, quit now */
	/* if we we're cancelled, quit now */
	if (ahandle && (ahandle->proc == (caddr_t) 0)) {
	    /* we've been signalled */
	    break;
	}
    } while (osi_Time() < endTime);
    return code;
}



typedef struct afs_event {
    struct afs_event *next;     /* next in hash chain */
    char *event;                /* lwp event: an address */
    int refcount;               /* Is it in use? */
    int seq;                    /* Sequence number: this is incremented
	                           by wakeup calls; wait will not return until
	                           it changes */
} afs_event_t;

#define HASHSIZE 128
afs_event_t *afs_evhasht[HASHSIZE];/* Hash table for events */
#define afs_evhash(event)       (afs_uint32) ((((long)event)>>2) & (HASHSIZE-1));
int afs_evhashcnt = 0;

/* Get and initialize event structure corresponding to lwp event (i.e. address)
 * */
static afs_event_t *afs_getevent(char *event)
{
    afs_event_t *evp, *newp = 0;
    int hashcode;

    AFS_ASSERT_GLOCK();
    hashcode = afs_evhash(event);
    evp = afs_evhasht[hashcode];
    while (evp) {
	if (evp->event == event) {
	    evp->refcount++;
	    return evp;
	}
	if (evp->refcount == 0)
	    newp = evp;
	evp = evp->next;
    }
    if (!newp) {
	newp = (afs_event_t *) osi_AllocSmallSpace(sizeof (afs_event_t));
	afs_evhashcnt++;
	newp->next = afs_evhasht[hashcode];
	afs_evhasht[hashcode] = newp;
	newp->seq = 0;
    }
    newp->event = event;
    newp->refcount = 1;
    return newp;
}

/* Release the specified event */
#define relevent(evp) ((evp)->refcount--)


void afs_osi_Sleep(char *event)
{
    struct afs_event *evp;
    int seq;

    evp = afs_getevent(event);
    seq = evp->seq;
    while (seq == evp->seq) {
	AFS_ASSERT_GLOCK();
	assert_wait((event_t)event, 0);
	AFS_GUNLOCK();
	thread_block(0);
	AFS_GLOCK();
    }
    relevent(evp);
}

/* osi_TimedSleep
 * 
 * Arguments:
 * event - event to sleep on
 * ams --- max sleep time in milliseconds
 * aintok - 1 if should sleep interruptibly
 *
 * Returns 0 if timeout and EINTR if signalled.
 */
static int osi_TimedSleep(char *event, afs_int32 ams, int aintok)
{
    int code = 0;
    struct afs_event *evp;
    int ticks,seq;

    ticks = ( ams * afs_hz )/1000;


    evp = afs_getevent(event);
    seq=evp->seq;
    assert_wait((event_t)event, aintok ? THREAD_ABORTSAFE : 0);
    AFS_GUNLOCK();
    thread_set_timer(ticks, NSEC_PER_SEC / hz);
    thread_block(0);
    AFS_GLOCK();
#if 0 /* thread_t structure only available if MACH_KERNEL_PRIVATE */
    if (current_thread()->wait_result != THREAD_AWAKENED)
	code = EINTR;
#else
    if (seq == evp->seq)
	code = EINTR;
#endif
    
    relevent(evp);
    return code;
}


void afs_osi_Wakeup(char *event)
{
    struct afs_event *evp;
    
    evp = afs_getevent(event);
    if (evp->refcount > 1) {
	evp->seq++;    
	thread_wakeup((event_t)event);
    }
    relevent(evp);
}
