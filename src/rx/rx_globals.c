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
#define FD_SETSIZE 65536

#include <afsconfig.h>
#ifdef KERNEL
#include "afs/param.h"
#else
#include <afs/param.h>
#endif

RCSID
    ("$Header: /cvs/openafs/src/rx/rx_globals.c,v 1.7 2004/07/08 05:16:58 shadow Exp $");

/* Enable data initialization when the header file is included */
#define INIT(stuff) = stuff
#if defined(AFS_NT40_ENV) && defined(AFS_PTHREAD_ENV)
#define EXT __declspec(dllexport)
#else
#define EXT
#endif

#ifdef KERNEL
#ifndef UKERNEL
#include "h/types.h"
#else /* !UKERNEL */
#include	"afs/sysincludes.h"
#endif /* UKERNEL */
#endif /* KERNEL */

#include "rx_globals.h"
