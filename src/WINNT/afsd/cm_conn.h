/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef __CM_CONN_H_ENV__
#define __CM_CONN_H_ENV__ 1

#define	CM_CONN_DEFAULTRDRTIMEOUT	45
#define CM_CONN_CONNDEADTIME		60
#define CM_CONN_HARDDEADTIME        120

extern long ConnDeadtimeout;
extern long HardDeadtimeout;

typedef struct cm_conn {
	struct cm_conn *nextp;		/* locked by cm_connLock */
	struct cm_server *serverp;	/* locked by cm_serverLock */
        struct rx_connection *callp;	/* locked by mx */
        struct cm_user *userp;		/* locked by mx; a held reference */
        osi_mutex_t mx;			/* mutex for some of these fields */
        int refCount;			/* locked by cm_connLock */
	int ucgen;			/* ucellp's generation number */
        long flags;			/* locked by mx */
	int cryptlevel;			/* encrytion status */
} cm_conn_t;

/* structure used for tracking RPC progress */
typedef struct cm_req {
	DWORD startTime;		/* Quit before RDR times us out */
	int rpcError;			/* RPC error code */
	int volumeError;		/* volume error code */
	int accessError;		/* access error code */
	int flags;
} cm_req_t;

/* flags in cm_req structure */
#define	CM_REQ_NORETRY		0x1

/*
 * Vice2 error codes
 * 3/20/85
 * Note:  all of the errors listed here are currently generated by the volume
 * package.  Other vice error codes, should they be needed, could be included
 * here also.
 */

#define VREADONLY	EROFS	/* Attempt to write a read-only volume */

/* Special error codes, which may require special handling (other than just
   passing them through directly to the end user) are listed below */

#define VICE_SPECIAL_ERRORS	101	/* Lowest special error code */

#define VSALVAGE	101	/* Volume needs salvage */
#define VNOVNODE	102	/* Bad vnode number quoted */
#define VNOVOL		103	/* Volume not attached, doesn't exist, 
				   not created or not online */
#define VVOLEXISTS	104	/* Volume already exists */
#define VNOSERVICE	105	/* Volume is not in service (i.e. it's
				   is out of funds, is obsolete, or somesuch) */
#define VOFFLINE	106	/* Volume is off line, for the reason
				   given in the offline message */
#define VONLINE		107	/* Volume is already on line */
#define VDISKFULL	108 	/* ENOSPC - Partition is "full",
				   i.e. roughly within n% of full */
#define VOVERQUOTA	109	/* EDQUOT - Volume max quota exceeded */
#define VBUSY		110	/* Volume temporarily unavailable; try again.
				   The volume should be available again shortly;
				   if it isn't something is wrong.
				   Not normally to be propagated to the
				   application level */
#define VMOVED		111	/* Volume has moved to another server;
				   do a VGetVolumeInfo to THIS server to find
				   out where */

#define VRESTARTING	-100    /* server is restarting, otherwise similar to 
				   VBUSY above.  This is negative so that old
				   cache managers treat it as "server is down"*/

#include "cm_server.h"

extern void cm_InitConn(void);

extern void cm_InitReq(cm_req_t *reqp);

extern int cm_Analyze(cm_conn_t *connp, struct cm_user *up, struct cm_req *reqp,
	struct cm_fid *fidp,
	struct AFSVolSync *volInfop,
    struct cm_serverRef_t * serversp,
	struct cm_callbackRequest *cbrp, long code);

extern long cm_ConnByMServers(struct cm_serverRef *, struct cm_user *,
	cm_req_t *, cm_conn_t **);

extern long cm_ConnByServer(struct cm_server *, struct cm_user *, cm_conn_t **);

extern long cm_Conn(struct cm_fid *, struct cm_user *, struct cm_req *,
	cm_conn_t **);

extern void cm_PutConn(cm_conn_t *connp);

extern void cm_GCConnections(cm_server_t *serverp);

#endif /*  __CM_CONN_H_ENV__ */
