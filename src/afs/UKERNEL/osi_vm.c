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

RCSID("$Header: /cvs/openafs/src/afs/UKERNEL/osi_vm.c,v 1.6 2002/10/16 03:58:24 shadow Exp $");

#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"  /* statistics */

void osi_VM_Truncate(struct vcache *avc, int alen, struct AFS_UCRED *acred)
{
    return;
}

int osi_VM_FlushVCache(struct vcache *avc, int *slept)
{
    return 0;
}

void osi_VM_StoreAllSegments(struct vcache *avc)
{
    return;
}

void osi_VM_TryToSmush(struct vcache *avc, struct AFS_UCRED *acred, int sync)
{
    return;
}

void osi_VM_FlushPages(struct vcache *avc, struct AFS_UCRED *credp)
{
    return;
}
