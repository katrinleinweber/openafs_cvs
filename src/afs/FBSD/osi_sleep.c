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
    ("$Header: /cvs/openafs/src/afs/FBSD/osi_sleep.c,v 1.11 2004/03/10 23:01:51 rees Exp $");

#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"	/* afs statistics */

#ifndef AFS_FBSD50_ENV
static int osi_TimedSleep(char *event, afs_int32 ams, int aintok);
static char waitV;
#endif


void
afs_osi_InitWaitHandle(struct afs_osi_WaitHandle *achandle)
{
    AFS_STATCNT(osi_InitWaitHandle);
#ifdef AFS_FBSD50_ENV
    cv_init(&achandle->wh_condvar, "afscondvar");
    achandle->wh_inited = 1;
#else
    achandle->proc = NULL;
#endif
}

/* cancel osi_Wait */
/* XXX
 * I can't tell -- is this supposed to be cv_signal() or cv_waitq_remove()?
 * Or perhaps cv_broadcast()?
 * Assuming cv_signal() is the desired meaning.  -GAW
 */
void
afs_osi_CancelWait(struct afs_osi_WaitHandle *achandle)
{
#ifndef AFS_FBSD50_ENV
    caddr_t proc;
#endif

    AFS_STATCNT(osi_CancelWait);

#ifdef AFS_FBSD50_ENV
    /* XXX should not be necessary */
    if (!achandle->wh_inited)
	return;
    AFS_ASSERT_GLOCK();
    cv_signal(&achandle->wh_condvar);
#else
    proc = achandle->proc;
    if (proc == 0)
	return;
    achandle->proc = NULL;	/* so dude can figure out he was signalled */
    afs_osi_Wakeup(&waitV);
#endif
}

/* afs_osi_Wait
 * Waits for data on ahandle, or ams ms later.  ahandle may be null.
 * Returns 0 if timeout and EINTR if signalled.
 */
int
afs_osi_Wait(afs_int32 ams, struct afs_osi_WaitHandle *ahandle, int aintok)
{
    int code;
#ifdef AFS_FBSD50_ENV
    struct timeval tv;
    int ticks;
#else
    afs_int32 endTime;
#endif

    AFS_STATCNT(osi_Wait);
#ifdef AFS_FBSD50_ENV
    tv.tv_sec = ams / 1000;
    tv.tv_usec = (ams % 1000) * 1000;
    ticks = tvtohz(&tv);

    AFS_ASSERT_GLOCK();
    if (ahandle == NULL) {
	/* This is nasty and evil and rude. */
	code = msleep(&tv, &afs_global_mtx, (aintok ? PPAUSE|PCATCH : PVFS),
	    "afswait", ticks);
    } else {
	if (!ahandle->wh_inited)
	    afs_osi_InitWaitHandle(ahandle);	/* XXX should not be needed */

	if (aintok)
	    code = cv_timedwait_sig(&ahandle->wh_condvar, &afs_global_mtx,
		ticks);
	else
	    code = cv_timedwait(&ahandle->wh_condvar, &afs_global_mtx, ticks);
    }
#else
    endTime = osi_Time() + (ams / 1000);
    if (ahandle)
	ahandle->proc = (caddr_t) curproc;
    do {
	AFS_ASSERT_GLOCK();
	code = osi_TimedSleep(&waitV, ams, aintok);
	if (code)
	    break;		/* if something happened, quit now */
	/* if we we're cancelled, quit now */
	if (ahandle && (ahandle->proc == NULL)) {
	    /* we've been signalled */
	    break;
	}
    } while (osi_Time() < endTime);
#endif
    return code;
}

/*
 * All this gluck should probably also be replaced with CVs.
 */
typedef struct afs_event {
    struct afs_event *next;	/* next in hash chain */
    char *event;		/* lwp event: an address */
    int refcount;		/* Is it in use? */
    int seq;			/* Sequence number: this is incremented
				 * by wakeup calls; wait will not return until
				 * it changes */
    int cond;
} afs_event_t;

#define HASHSIZE 128
afs_event_t *afs_evhasht[HASHSIZE];	/* Hash table for events */
#define afs_evhash(event)	(afs_uint32) ((((long)event)>>2) & (HASHSIZE-1));
int afs_evhashcnt = 0;

/* Get and initialize event structure corresponding to lwp event (i.e. address)
 * */
static afs_event_t *
afs_getevent(char *event)
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
	newp = (afs_event_t *) osi_AllocSmallSpace(sizeof(afs_event_t));
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


void
afs_osi_Sleep(void *event)
{
    struct afs_event *evp;
    int seq;

    evp = afs_getevent(event);
    seq = evp->seq;
    while (seq == evp->seq) {
	AFS_ASSERT_GLOCK();
#ifdef AFS_FBSD50_ENV
	msleep(event, &afs_global_mtx, PVFS, "afsslp", 0);
#else
	AFS_GUNLOCK();
	tsleep(event, PVFS, "afs_osi_Sleep", 0);
	AFS_GLOCK();
#endif
    }
    relevent(evp);
}

int
afs_osi_SleepSig(void *event)
{
    afs_osi_Sleep(event);
    return 0;
}

#ifndef AFS_FBSD50_ENV
/* osi_TimedSleep
 * 
 * Arguments:
 * event - event to sleep on
 * ams --- max sleep time in milliseconds
 * aintok - 1 if should sleep interruptibly
 *
 * Returns 0 if timeout and EINTR if signalled.
 */
static int
osi_TimedSleep(char *event, afs_int32 ams, int aintok)
{
    int code = 0;
    struct afs_event *evp;
    int ticks;
    int seq, prio;

    ticks = (ams * afs_hz) / 1000;


    evp = afs_getevent(event);
    seq = evp->seq;
    AFS_GUNLOCK();
    if (aintok)
	prio = PCATCH | PPAUSE;
    else
	prio = PVFS;
    code = tsleep(event, prio, "afs_osi_TimedSleep", ticks);
    AFS_GLOCK();
    if (seq == evp->seq)
	code = EINTR;
    relevent(evp);
    return code;
}
#endif /* not AFS_FBSD50_ENV */

int
afs_osi_Wakeup(void *event)
{
    int ret = 1;
    struct afs_event *evp;

    evp = afs_getevent(event);
    if (evp->refcount > 1) {
	evp->seq++;
	wakeup(event);
	ret = 0;
    }
    relevent(evp);
    return ret;
}
