/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/* Copyright (C) 1994 Cazamar Systems, Inc. */

#ifndef _BUF_H__ENV_
#define _BUF_H__ENV_ 1

#include <osi.h>
#ifdef DISKCACHE95
#include "cm_diskcache.h"
#endif /* DISKCACHE95 */

/* default # of buffers if not changed */
#define CM_BUF_BUFFERS	100

/* default buffer size */
#define CM_BUF_BLOCKSIZE CM_CONFIGDEFAULT_BLOCKSIZE

/* default hash size */
#define CM_BUF_HASHSIZE	1024

/* cache type */
#define CM_BUF_CACHETYPE_FILE 1
#define CM_BUF_CACHETYPE_VIRTUAL 2
extern int buf_cacheType;

/* force it to be signed so that mod comes out positive or 0 */
#define BUF_HASH(fidp,offsetp) ((((fidp)->vnode+((fidp)->unique << 5)	\
				+(fidp)->volume+(fidp)->cell		\
				+((offsetp)->LowPart / cm_data.buf_blockSize))	\
				  & 0x7fffffff)				\
				   % cm_data.buf_hashSize)

/* another hash fn */
#define BUF_FILEHASH(fidp) ((((fidp)->vnode+((fidp)->unique << 5)	\
				+(fidp)->volume+(fidp)->cell)		\
				  & 0x7fffffff)				\
				   % cm_data.buf_hashSize)

/* backup over pointer to the buffer */
#define BUF_OVERTOBUF(op) ((cm_buf_t *)(((char *)op) - ((long)(&((cm_buf_t *)0)->over))))

#ifdef notdef
/* pretend we have logs, too */
typedef char cm_log_t;
#endif

#define CM_BUF_MAGIC    ('B' | 'U' <<8 | 'F'<<16 | 'F'<<24)

/* represents a single buffer */
typedef struct cm_buf {
    osi_queue_t q;		/* queue of all zero-refcount buffers */
    afs_uint32     magic;
    struct cm_buf *allp;	/* next in all list */
    struct cm_buf *hashp;	/* hash bucket pointer */
    struct cm_buf *fileHashp;   /* file hash bucket pointer */
    struct cm_buf *fileHashBackp;	/* file hash bucket back pointer */
				/*
				 * The file hash chain is doubly linked, since
				 * these chains can get rather long.  The
				 * regular hash chain is only singly linked,
				 * since the chains should be short if the
				 * hash function is good and if there are
				 * enough buckets for the size of the cache.
				 */
    osi_mutex_t mx;		/* mutex protecting structure except refcount */
    unsigned long refCount;	/* reference count (buf_globalLock) */
    long idCounter;		/* counter for softrefs; bumped at each recycle */
    long dirtyCounter;	        /* bumped at each dirty->clean transition */
#ifdef notdef
    cm_log_t *logp;	        /* log for this buffer, if any */
    osi_hyper_t lsn;	        /* lsn to force to (last LSN changing this buffer) */
#endif /* notdef */
    osi_hyper_t offset;	        /* offset */
    cm_fid_t fid;		/* file ID */
    long flags;		        /* flags we're using */
    long size;		        /* size in bytes of this buffer */
    char *datap;		/* data in this buffer */
    unsigned long error;	/* last error code, if CM_BUF_ERROR is set */
    cm_user_t *userp;	        /* user who wrote to the buffer last */
#ifndef DJGPP
    OVERLAPPED over;	        /* overlapped structure for I/O */
#endif
        
    /* fields added for the CM; locked by scp->mx */
    long dataVersion;	        /* data version of this page */
    long cmFlags;		/* flags for cm */
#ifdef DISKCACHE95
    cm_diskcache_t *dcp;        /* diskcache structure */
#endif /* DISKCACHE95 */
} cm_buf_t;

/* values for cmFlags */
#define CM_BUF_CMFETCHING	1	/* fetching this buffer */
#define CM_BUF_CMSTORING	2	/* storing this buffer */
#define CM_BUF_CMFULLYFETCHED	4	/* read-while-fetching optimization */
/* waiting is done based on scp->flags */

/* represents soft reference which is OK to lose on a recycle */
typedef struct cm_softRef {
    cm_buf_t *bufp;	/* buffer (may get reused) */
    long counter;		/* counter of changes to identity */
} cm_softRef_t;

#define CM_BUF_READING	1	/* now reading buffer to the disk */
#define CM_BUF_WRITING	2	/* now writing buffer to the disk */
#define CM_BUF_INHASH	4	/* in the hash table */
#define CM_BUF_DIRTY		8	/* buffer is dirty */
#define CM_BUF_INLRU		0x10	/* in lru queue */
#define CM_BUF_ERROR		0x20	/* something went wrong on delayed write */
#define CM_BUF_WAITING	0x40	/* someone's waiting for a flag to change */
#define CM_BUF_EVWAIT	0x80	/* someone's waiting for the buffer event */
#define CM_BUF_EOF		0x100	/* read 0 bytes; used for detecting EOF */

typedef struct cm_buf_ops {
    long (*Writep)(void *, osi_hyper_t *, long, long, struct cm_user *,
			struct cm_req *);
    long (*Readp)(cm_buf_t *, long, long *, struct cm_user *);
    long (*Stabilizep)(void *, struct cm_user *, struct cm_req *);
    long (*Unstabilizep)(void *, struct cm_user *);
} cm_buf_ops_t;

/* global locks */
extern osi_rwlock_t buf_globalLock;

extern long buf_Init(int newFile, cm_buf_ops_t *, long nbuffers);

extern void buf_Shutdown(void);

extern long buf_CountFreeList(void);

extern void buf_Release(cm_buf_t *);

extern void buf_Hold(cm_buf_t *);

extern void buf_WaitIO(cm_buf_t *);

extern void buf_LockedRelease(cm_buf_t *);

extern cm_buf_t *buf_LockedFind(struct cm_scache *, osi_hyper_t *);

extern cm_buf_t *buf_Find(struct cm_scache *, osi_hyper_t *);

#ifndef DJGPP
extern HANDLE buf_GetFileHandle(long);
#endif /* !DJGPP */

extern void buf_LockedCleanAsync(cm_buf_t *, cm_req_t *);

extern long buf_GetNewLocked(struct cm_scache *, osi_hyper_t *, cm_buf_t **);

extern long buf_Get(struct cm_scache *, osi_hyper_t *, cm_buf_t **);

extern long buf_GetNew(struct cm_scache *, osi_hyper_t *, cm_buf_t **);

extern void buf_CleanAsync(cm_buf_t *, cm_req_t *);

extern void buf_CleanWait(cm_buf_t *);

extern void buf_SetDirty(cm_buf_t *);

extern long buf_CleanAndReset(void);

extern void buf_ReserveBuffers(long);

extern int buf_TryReserveBuffers(long);

extern void buf_UnreserveBuffers(long);

#ifdef TESTING
extern void buf_ValidateBufQueues(void);
#endif /* TESTING */

extern osi_log_t *buf_logp;

extern long buf_Truncate(struct cm_scache *scp, cm_user_t *userp,
	cm_req_t *reqp, osi_hyper_t *sizep);

extern long buf_CleanVnode(struct cm_scache *scp, cm_user_t *userp,
	cm_req_t *reqp);

extern long buf_FlushCleanPages(cm_scache_t *scp, cm_user_t *userp,
	cm_req_t *reqp);

extern long buf_SetNBuffers(long nbuffers);

extern long buf_ValidateBuffers(void);

extern void buf_ForceTrace(BOOL flush);

/* error codes */
#define CM_BUF_EXISTS	1	/* buffer exists, and shouldn't */
#define CM_ERROR_BASEBUF	0x33333333
#define CM_ERROR_TOOFEWBUFS	(CM_ERROR_BASEBUF+0)
#define CM_ERROR_TOOMANYBUFS	(CM_ERROR_BASEBUF+1)
#endif /*  _BUF_H__ENV_ */
