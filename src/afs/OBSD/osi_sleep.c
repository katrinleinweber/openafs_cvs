/*
 * $Id: osi_sleep.c,v 1.5 2003/03/01 18:16:01 rees Exp $
 */

/*
copyright 2002
the regents of the university of michigan
all rights reserved

permission is granted to use, copy, create derivative works 
and redistribute this software and such derivative works 
for any purpose, so long as the name of the university of 
michigan is not used in any advertising or publicity 
pertaining to the use or distribution of this software 
without specific, written prior authorization.  if the 
above copyright notice or any other identification of the 
university of michigan is included in any copy of any 
portion of this software, then the disclaimer below must 
also be included.

this software is provided as is, without representation 
from the university of michigan as to its fitness for any 
purpose, and without warranty by the university of 
michigan of any kind, either express or implied, including 
without limitation the implied warranties of 
merchantability and fitness for a particular purpose. the 
regents of the university of michigan shall not be liable 
for any damages, including special, indirect, incidental, or 
consequential damages, with respect to any claim arising 
out of or in connection with the use of the software, even 
if it has been or is hereafter advised of the possibility of 
such damages.
*/

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

RCSID("$Header: /cvs/openafs/src/afs/OBSD/osi_sleep.c,v 1.5 2003/03/01 18:16:01 rees Exp $");

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
    AFS_ASSERT_GLOCK();
    AFS_GUNLOCK();
    do {
	if (aintok) {
	    code = tsleep(&waitV, PCATCH | PVFS, "afs_osi_Wait", timo);
	    if (code)	/* if interrupted, return EINTR */
		code = EINTR;
	} else
	    tsleep(&waitV, PVFS, "afs_osi_Wait", timo);

	/* if we were cancelled, quit now */
	if (ahandle && (ahandle->proc == NULL)) {
	    /* we've been signalled */
	    break;
	}
    } while (osi_Time() < endTime);
    AFS_GLOCK();
    return code;
}

void afs_osi_Sleep(void *event)
{
    AFS_ASSERT_GLOCK();
    AFS_GUNLOCK();
    tsleep(event, PVFS, "afs", 0);
    AFS_GLOCK();
}

int afs_osi_SleepSig(void *event)
{
    AFS_ASSERT_GLOCK();
    AFS_GUNLOCK();
    tsleep(event, PVFS, "afs", 0);
    AFS_GLOCK();
    return 0;
}

int afs_osi_Wakeup(void *event)
{
    wakeup(event);
    return 1;
}
