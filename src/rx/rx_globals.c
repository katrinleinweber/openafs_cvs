/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/* RX:  Globals for internal use, basically */

#ifdef KERNEL
#include "../afs/param.h"
#else
#include <afs/param.h>
#endif
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/rx/rx_globals.c,v 1.3 2001/07/05 15:20:44 shadow Exp $");

/* Enable data initialization when the header file is included */
#define INIT(stuff) = stuff
#if defined(AFS_NT40_ENV) && defined(AFS_PTHREAD_ENV)
#define EXT __declspec(dllexport)
#else
#define EXT
#endif

#ifdef KERNEL
#ifndef UKERNEL
#include "../h/types.h"
#else /* !UKERNEL */
#include	"../afs/sysincludes.h"
#endif /* UKERNEL */
#endif /* KERNEL */

#include "rx_globals.h"
