/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#include <afs/param.h>

RCSID
    ("$Header: /cvs/openafs/src/util/test/ktest.c,v 1.5 2003/07/15 23:17:18 shadow Exp $");

#include "ktime.h"

main(argc, argv)
     int argc;
     char **argv;
{
    struct ktime ttime;
    long ntime, code;

    if (argc <= 1) {
	printf("ktest: usage is 'ktest <periodic date to evaluate>'\n");
	exit(1);
    }

    code = ktime_ParsePeriodic(argv[1], &ttime);
    if (code) {
	printf("got error code %d from ParsePeriodic.\n", code);
	exit(1);
    }

    ntime = ktime_next(&ttime, 0);
    printf("time is %d, %s", ntime, ctime(&ntime));
    exit(0);
}
