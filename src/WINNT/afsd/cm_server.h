/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef __CM_SERVER_H_ENV__
#define __CM_SERVER_H_ENV__ 1

#ifndef DJGPP
#include <winsock2.h>
#else /* DJGPP */
#include <netinet/in.h>
#endif /* !DJGPP */
#include <osi.h>

/* pointed to by volumes and cells without holds; cm_serverLock is obtained
 * at the appropriate times to change the pointers to these servers.
 */
typedef struct cm_server {
    struct cm_server *allNextp;		/* locked by cm_serverLock */
    struct sockaddr_in addr;		/* by mx */
    int type;				/* by mx */
    struct cm_conn *connsp;			/* locked by cm_connLock */
    long flags;				/* by mx */
    struct cm_cell *cellp;			/* cell containing this server */
    unsigned long refCount;				/* locked by cm_serverLock */
    osi_mutex_t mx;
    unsigned short ipRank;			/* server priority */
} cm_server_t;

enum repstate {not_busy, busy, offline};

typedef struct cm_serverRef {
    struct cm_serverRef *next;      /* locked by cm_serverLock */
    struct cm_server *server;       /* locked by cm_serverLock */
    enum repstate status;           /* locked by cm_serverLock */
    unsigned long refCount;                   /* locked by cm_serverLock */
} cm_serverRef_t;

/* types */
#define CM_SERVER_VLDB		1	/* a VLDB server */
#define CM_SERVER_FILE		2	/* a file server */

/* flags */
#define CM_SERVERFLAG_DOWN	1	/* server is down */

/* flags for procedures */
#define CM_FLAG_CHECKUPSERVERS		1	/* check working servers */
#define CM_FLAG_CHECKDOWNSERVERS	2	/* check down servers */

/* values for ipRank */
#define CM_IPRANK_TOP	5000	/* on same machine */
#define CM_IPRANK_HI	20000	/* on same subnet  */
#define CM_IPRANK_MED	30000	/* on same network */
#define CM_IPRANK_LOW	40000	/* on different networks */

/* the maximum number of network interfaces that this client has */ 

#define CM_MAXINTERFACE_ADDR          16

extern cm_server_t *cm_NewServer(struct sockaddr_in *addrp, int type,
	struct cm_cell *cellp);

extern cm_serverRef_t *cm_NewServerRef(struct cm_server *serverp);

extern long cm_ChecksumServerList(cm_serverRef_t *serversp);

extern void cm_GetServer(cm_server_t *);

extern void cm_GetServerNoLock(cm_server_t *);

extern void cm_PutServer(cm_server_t *);

extern void cm_PutServerNoLock(cm_server_t *);

extern cm_server_t *cm_FindServer(struct sockaddr_in *addrp, int type);

extern osi_rwlock_t cm_serverLock;

extern void cm_InitServer(void);

extern void cm_CheckServers(long flags, struct cm_cell *cellp);

extern cm_server_t *cm_allServersp;

extern void cm_SetServerPrefs(cm_server_t * serverp);

extern void cm_InsertServerList(cm_serverRef_t** list,cm_serverRef_t* element);

extern long cm_ChangeRankServer(cm_serverRef_t** list, cm_server_t* server); 

extern void cm_RandomizeServer(cm_serverRef_t** list); 

extern void cm_FreeServer(cm_server_t* server);

extern void cm_FreeServerList(cm_serverRef_t** list);

#endif /*  __CM_SERVER_H_ENV__ */
