/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/* ReallyAbort:  called from assert. May/85 */
#include <afsconfig.h>
#include <afs/param.h>
#include <stdlib.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif

RCSID
    ("$Header: /cvs/openafs/src/util/assert.c,v 1.11 2003/08/20 22:41:06 zacheiss Exp $");

#include <stdio.h>
#include "afsutil.h"

#ifdef AFS_NT40_ENV
void
afs_NTAbort(void)
{
    _asm int 3h;		/* always trap. */
}
#endif


void
AssertionFailed(char *file, int line)
{
    char tdate[26];
    time_t when;

    time(&when);
    afs_ctime(&when, tdate, 25);
    fprintf(stderr, "%s: Assertion failed! file %s, line %d.\n", tdate, file,
	    line);
    fflush(stderr);
#ifdef AFS_NT40_ENV
    afs_NTAbort();
#else
    abort();
#endif
}
