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

RCSID
    ("$Header: /cvs/openafs/src/afs/afs_osi_alloc.c,v 1.11.6.2 2006/07/31 21:27:00 shadow Exp $");



#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"	/* afs statistics */

#ifndef AFS_FBSD_ENV

#ifdef AFS_AIX41_ENV
#include "sys/lockl.h"
#include "sys/sleep.h"
#include "sys/syspest.h"
#include "sys/lock_def.h"
/*lock_t osi_fsplock = LOCK_AVAIL;*/
#endif

afs_lock_t osi_fsplock;



static struct osi_packet {
    struct osi_packet *next;
} *freePacketList = NULL, *freeSmallList;
afs_lock_t osi_flplock;


/* free space allocated by AllocLargeSpace.  Also called by mclput when freeing
 * a packet allocated by osi_NetReceive. */

void
osi_FreeLargeSpace(void *adata)
{

    AFS_ASSERT_GLOCK();

    AFS_STATCNT(osi_FreeLargeSpace);
    afs_stats_cmperf.LargeBlocksActive--;
    MObtainWriteLock(&osi_flplock, 322);
    ((struct osi_packet *)adata)->next = freePacketList;
    freePacketList = adata;
    MReleaseWriteLock(&osi_flplock);
}

void
osi_FreeSmallSpace(void *adata)
{

    AFS_ASSERT_GLOCK();

    AFS_STATCNT(osi_FreeSmallSpace);
    afs_stats_cmperf.SmallBlocksActive--;
    MObtainWriteLock(&osi_fsplock, 323);
    ((struct osi_packet *)adata)->next = freeSmallList;
    freeSmallList = adata;
    MReleaseWriteLock(&osi_fsplock);
}


/* allocate space for sender */
void *
osi_AllocLargeSpace(size_t size)
{
    register struct osi_packet *tp;

    AFS_ASSERT_GLOCK();

    AFS_STATCNT(osi_AllocLargeSpace);
    if (size > AFS_LRALLOCSIZ)
	osi_Panic("osi_AllocLargeSpace: size=%d\n", size);
    afs_stats_cmperf.LargeBlocksActive++;
    if (!freePacketList) {
	char *p;

	afs_stats_cmperf.LargeBlocksAlloced++;
	p = (char *)afs_osi_Alloc(AFS_LRALLOCSIZ);
#ifdef  KERNEL_HAVE_PIN
	/*
	 * Need to pin this memory since under heavy conditions this memory
	 * could be swapped out; the problem is that we could inside rx where
	 * interrupts are disabled and thus we would panic if we don't pin it.
	 */
	pin(p, AFS_LRALLOCSIZ);
#endif
	return p;
    }
    MObtainWriteLock(&osi_flplock, 324);
    tp = freePacketList;
    if (tp)
	freePacketList = tp->next;
    MReleaseWriteLock(&osi_flplock);
    return (char *)tp;
}


/* allocate space for sender */
void *
osi_AllocSmallSpace(size_t size)
{
    register struct osi_packet *tp;

    AFS_STATCNT(osi_AllocSmallSpace);
    if (size > AFS_SMALLOCSIZ)
	osi_Panic("osi_AllocSmallS: size=%d\n", size);

    if (!freeSmallList) {
	afs_stats_cmperf.SmallBlocksAlloced++;
	afs_stats_cmperf.SmallBlocksActive++;
	return afs_osi_Alloc(AFS_SMALLOCSIZ);
    }
    afs_stats_cmperf.SmallBlocksActive++;
    MObtainWriteLock(&osi_fsplock, 327);
    tp = freeSmallList;
    if (tp)
	freeSmallList = tp->next;
    MReleaseWriteLock(&osi_fsplock);
    return (char *)tp;
}



void
shutdown_osinet(void)
{
    extern int afs_cold_shutdown;

    AFS_STATCNT(shutdown_osinet);
    if (afs_cold_shutdown) {
	struct osi_packet *tp;

	while ((tp = freePacketList)) {
	    freePacketList = tp->next;
	    afs_osi_Free(tp, AFS_LRALLOCSIZ);
#ifdef  KERNEL_HAVE_PIN
	    unpin(tp, AFS_LRALLOCSIZ);
#endif
	}

	while ((tp = freeSmallList)) {
	    freeSmallList = tp->next;
	    afs_osi_Free(tp, AFS_SMALLOCSIZ);
#ifdef  KERNEL_HAVE_PIN
	    unpin(tp, AFS_SMALLOCSIZ);
#endif
	}
	LOCK_INIT(&osi_fsplock, "osi_fsplock");
	LOCK_INIT(&osi_flplock, "osi_flplock");
    }
}
#endif
