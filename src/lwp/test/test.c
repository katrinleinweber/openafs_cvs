/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afs/param.h>
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/lwp/test/test.c,v 1.3 2001/07/05 15:20:38 shadow Exp $");

#include <stdio.h>
#include <sys/time.h>
#include <potpourri.h>
#include "lwp.h"

char semaphore;

int OtherProcess()
    {
    for(;;)
	{
	LWP_SignalProcess(&semaphore);
	}
    }

main(argc, argv)
int argc; char *argv[];
    {
    struct timeval t1, t2;
    int pid, otherpid;
    register int i,  count, x;
    char *waitarray[2];
    static char c[] = "OtherProcess";
    
    count = atoi(argv[1]);

    assert(LWP_InitializeProcessSupport(0, &pid) == LWP_SUCCESS);
    assert(LWP_CreateProcess(OtherProcess,4096,0, 0, c, &otherpid) == LWP_SUCCESS);

    waitarray[0] = &semaphore;
    waitarray[1] = 0;
    gettimeofday(&t1, NULL);
    for (i = 0; i < count; i++)
	{
	LWP_MwaitProcess(1, waitarray, 1);
	}
    gettimeofday(&t2, NULL);

    x = (t2.tv_sec -t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec);
    printf("%d milliseconds for %d MWaits (%f usec per Mwait and Signal)\n", x/1000, count, (float)(x/count));
    }
