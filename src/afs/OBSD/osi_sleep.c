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

RCSID("$Header: /cvs/openafs/src/afs/OBSD/osi_sleep.c,v 1.1 2002/10/28 21:28:27 rees Exp $");

#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afs/afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"   /* afs statistics */

static char waitV;


void afs_osi_InitWaitHandle(struct afs_osi_WaitHandle *achandle)
{
    AFS_STATCNT(osi_InitWaitHandle);
    achandle->proc = NULL;
}

/* cancel osi_Wait */
void afs_osi_CancelWait(struct afs_osi_WaitHandle *achandle)
{
    caddr_t proc;

    AFS_STATCNT(osi_CancelWait);
    proc = achandle->proc;
    if (proc == 0)
	return;
    achandle->proc = NULL;
    wakeup(&waitV);
}

/* afs_osi_Wait
 * Waits for data on ahandle, or ams ms later.  ahandle may be null.
 * Returns 0 if timeout and EINTR if signalled.
 */
int afs_osi_Wait(afs_int32 ams, struct afs_osi_WaitHandle *ahandle, int aintok)
{
    int code = 0;
    afs_int32 endTime;
    int timo = (ams * afs_hz) / 1000 + 1;

    AFS_STATCNT(osi_Wait);
    endTime = osi_Time() + (ams / 1000);
    if (ahandle)
	ahandle->proc = (caddr_t) curproc;
    do {
	if (aintok) {
	    code = tsleep(&waitV, PCATCH | (PZERO+8), "afs_osi_Wait", timo);
	    if (code)	/* if interrupted, return EINTR */
		code = EINTR;
	} else
	    tsleep(&waitV, (PZERO-3), "afs_osi_Wait", timo);

	/* if we were cancelled, quit now */
	if (ahandle && (ahandle->proc == NULL)) {
	    /* we've been signalled */
	    break;
	}
    } while (osi_Time() < endTime);
    return code;
}

int afs_osi_SleepSig(void *event)
{
    afs_osi_Sleep(event);
    return 0;
}
