/* 
 * Copyright (C) 1998, 1989 Transarc Corporation - All rights reserved
 *
 * (C) COPYRIGHT IBM CORPORATION 1987, 1988
 * LICENSED MATERIALS - PROPERTY OF IBM
 *
 */

/* Copyright (C) 1994 Cazamar Systems, Inc. */

#ifndef __OSIDB_H_ENV_
#define __OSIDB_H_ENV_ 1

/* mapped over remote debugging integer array for rwlock and mutexes
 * mutexes only have writers
 */
typedef struct osi_remLockInfo {
	/* the type: rwlock or mutex */
	long type;

	/* the addr */
	long lockAddr;

	/* raw state */
	long readers;
	long writers;	/* should be 0 or 1 */
	long waiters;	/* non-zero means someone waiting */
	long owner;	/* one tid, not complete if >1 readers, 0 if unknown */

	/* raw statistics, times in ms.  For mutexes, we only
	 * fill in the write times.
	 */
	long writeLockedCount;
	long writeLockedTime;
	long writeBlockedCount;
	long writeBlockedTime;
	long readLockedCount;
	long readLockedTime;
	long readBlockedCount;
	long readBlockedTime;
} osi_remLockInfo_t;

/* mapped over remote debugging integer array */
typedef struct osi_remSleepInfo {
	long tid;		/* thread id of the blocked thread */
	long sleepValue;	/* the value we're sleeping at */
} osi_remSleepInfo_t;

#define OSI_MAXRPCCALLS 2	/* one for osidb, one for AFS RPC */

extern long osi_InitDebug(osi_uid_t *);

/* only available within the OSI package */
extern long osi_maxCalls;

/* use this from outside of the OSI package */
extern long *osi_maxCallsp;

#endif /* __OSIDB_H_ENV_ */

