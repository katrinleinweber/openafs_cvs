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

RCSID("$Header: /cvs/openafs/src/volser/common.c,v 1.8 2003/06/02 14:38:05 shadow Exp $");

#include <stdio.h>
#include <afs/afsutil.h>
#include <afs/com_err.h>

/*@printflike@*/ void Log(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vViceLog(0, (format, args));
    va_end(args);
}

void LogError(afs_int32 errcode)
{
    ViceLog(0, ("%s: %s\n", error_table_name(errcode),error_message(errcode)));
}

/*@printflike@*/ void Abort(const char* format, ...)
{
    va_list args;

    ViceLog(0, ("Program aborted: "));
    va_start(args, format);
    vViceLog(0, (format, args));
    va_end(args);
    abort();
}

void InitErrTabs(void)
{
    initialize_KA_error_table();
    initialize_RXK_error_table();
    initialize_KTC_error_table();
    initialize_ACFG_error_table();
    initialize_CMD_error_table();
    initialize_VL_error_table();
    initialize_VOLS_error_table();
    return;
}
