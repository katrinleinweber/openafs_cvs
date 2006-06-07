/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#ifdef	KERNEL
#include "afs/param.h"
#else
#include <afs/param.h>
#endif

RCSID
    ("$Header: /cvs/openafs/src/rx/rx_null.c,v 1.8 2006/06/07 04:31:33 shadow Exp $");

#ifdef KERNEL
#ifndef	UKERNEL
#include "h/types.h"
#else /* !UKERNEL */
#include "afs/sysincludes.h"
#endif /* !UKERNEL */
#include "rx/rx.h"
#include "afs/magic.h"
#else /* KERNEL */
#include "rx.h"
#include <afs/magic.h>
#endif /* KERNEL */

/* The null security object.  No authentication, no nothing. */

static struct rx_securityOps null_ops;
static struct rx_securityClass null_object = { MAGIC_RXSECURITY, &null_ops, 0, 0 };

struct rx_securityClass *
rxnull_NewServerSecurityObject(void)
{
    return &null_object;
}

struct rx_securityClass *
rxnull_NewClientSecurityObject(void)
{
    return &null_object;
}
