/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
	System:		VICE-TWO
	Module:		common.c
	Institution:	The Information Technology Center, Carnegie-Mellon University

 */

#include <sys/types.h>
#include <stdio.h>
#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/vol/common.c,v 1.5 2003/06/02 14:37:48 shadow Exp $");

#include <afs/afsutil.h>

int Statistics = 0;

/*@printflike@*/ void Log(const char *format, ...)
{
    int	level;
    va_list args;

    if (Statistics)
	level = -1;
    else
	level = 0;

    va_start(args, format);
    vViceLog(level, (format, args));
    va_end(args);
}

/*@printflike@*/ void Abort(const char *format, ...)
{
    va_list args;

    ViceLog(0, ("Program aborted: "));
    va_start(args, format);
    vViceLog(0, (format, args));
    va_end(args);
    abort();
}

/*@printflike@*/ void Quit(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vViceLog(0, (format, args));
    va_end(args);
    exit(1);
}
