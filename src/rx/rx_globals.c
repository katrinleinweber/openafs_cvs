/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/* RX:  Globals for internal use, basically */

/* This controls the size of an fd_set; it must be defined early before
 * the system headers define that type and the macros that operate on it.
 * Its value should be as large as the maximum file descriptor limit we
 * are likely to run into on any platform.  Right now, that is 65536
 * which is the default hard fd limit on Solaris 9 */
#if !defined(_WIN32) && !defined(KERNEL)
#define FD_SETSIZE 65536
#endif

#include <afsconfig.h>
#ifdef KERNEL
#include "afs/param.h"
#else
#include <afs/param.h>
#endif

RCSID
    ("$Header: /cvs/openafs/src/rx/rx_globals.c,v 1.14 2008/11/20 22:56:12 shadow Exp $");

/* Enable data initialization when the header file is included */
#define GLOBALSINIT(stuff) = stuff
#if defined(AFS_NT40_ENV) && defined(AFS_PTHREAD_ENV)
#define EXT __declspec(dllexport)
#define EXT2 __declspec(dllexport)
#else
#define EXT
#define EXT2 
#endif

#ifdef KERNEL
#ifndef UKERNEL
#include "h/types.h"
#else /* !UKERNEL */
#include	"afs/sysincludes.h"
#endif /* UKERNEL */
#endif /* KERNEL */

#include "rx_globals.h"

#ifdef AFS_NT40_ENV

void rx_SetRxDeadTime(int seconds)
{
    rx_connDeadTime = seconds;
}

int rx_GetMinUdpBufSize(void)
{
    return 64*1024;
}

void rx_SetUdpBufSize(int x)
{
    if (x > rx_GetMinUdpBufSize())
        rx_UdpBufSize = x;
}

void rx_SetMaxClonesPerConn(int x)
{
    rx_max_clones_per_connection = x;
}

#endif /* AFS_NT40_ENV */
