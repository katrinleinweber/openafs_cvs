/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef __CM_VNODEOPS_H_ENV__
#define __CM_VNODEOPS_H_ENV__ 1

extern unsigned int cm_mountRootGen;

/* parms for attribute setting call */
typedef struct cm_attr {
	int mask;
	unsigned long clientModTime;
        osi_hyper_t length;
	int unixModeBits;
        long owner;
        long group;
} cm_attr_t;

#define CM_ATTRMASK_CLIENTMODTIME	1	/* set if field is valid */
#define CM_ATTRMASK_LENGTH		2	/* field is valid */
#define CM_ATTRMASK_UNIXMODEBITS	4	/* field is valid */
#define CM_ATTRMASK_OWNER		8	/* field is valid */
#define CM_ATTRMASK_GROUP		0x10	/* field is valid */

/* type of rock for lookup's searches */
typedef struct cm_lookupSearch {
        cm_fid_t fid;
        char *searchNamep;
        int found;
        int LCfound, UCfound, NCfound;
        int caseFold;
        int hasTilde;
} cm_lookupSearch_t;

#include "cm_dir.h"

typedef int (*cm_DirFuncp_t)(struct cm_scache *, struct cm_dirEntry *, void *,
	osi_hyper_t *entryOffsetp);

/* arrays */

extern unsigned char cm_foldUpper[];

/* functions */

extern int cm_NoneLower(char *s);

extern int cm_NoneUpper(char *s);

extern int cm_Is8Dot3(char *namep);

extern int cm_stricmp(const char *, const char *);

extern void cm_Gen8Dot3Name(struct cm_dirEntry *dep, char *shortName,
	char **shortNameEndp);

extern long cm_ReadMountPoint(cm_scache_t *scp, cm_user_t *userp,
	cm_req_t *reqp);

extern long cm_NameI(cm_scache_t *rootSCachep, char *pathp, long flags,
	cm_user_t *userp, char *tidPathp, cm_req_t *reqp,
	cm_scache_t **outScpp);

extern long cm_Lookup(cm_scache_t *dscp, char *namep, long flags,
	cm_user_t *userp, cm_req_t *reqp, cm_scache_t **outpScpp);

extern void cm_TryBulkStat(cm_scache_t *dscp, osi_hyper_t *offsetp,
	cm_user_t *userp, cm_req_t *reqp);

extern long cm_SetAttr(cm_scache_t *scp, cm_attr_t *attrp, cm_user_t *userp,
	cm_req_t *reqp);

extern long cm_Create(cm_scache_t *scp, char *namep, long flags,
	cm_attr_t *attrp, cm_scache_t **scpp, cm_user_t *userp, cm_req_t *reqp);

extern long cm_FSync(cm_scache_t *scp, cm_user_t *userp, cm_req_t *reqp);

extern void cm_StatusFromAttr(struct AFSStoreStatus *statusp,
	struct cm_scache *scp, struct cm_attr *attrp);

extern long cm_Unlink(cm_scache_t *dscp, char *namep, cm_user_t *userp,
	cm_req_t *reqp);

extern long cm_ApplyDir(cm_scache_t *scp, cm_DirFuncp_t funcp, void *parmp,
	osi_hyper_t *startOffsetp, cm_user_t *userp, cm_req_t *reqp, 
	cm_scache_t **retscp);

extern long cm_MakeDir(cm_scache_t *dscp, char *lastNamep, long flags,
	cm_attr_t *attrp, cm_user_t *userp, cm_req_t *reqp);

extern long cm_RemoveDir(cm_scache_t *dscp, char *lastNamep, cm_user_t *userp,
	cm_req_t *reqp);

extern long cm_Rename(cm_scache_t *oldDscp, char *oldLastNamep,
	cm_scache_t *newDscp, char *newLastNamep, cm_user_t *userp,
	cm_req_t *reqp);

extern long cm_HandleLink(cm_scache_t *linkScp, struct cm_user *userp,
	cm_req_t *reqp);

extern long cm_SymLink(cm_scache_t *dscp, char *namep, char *contentsp,
	long flags, cm_attr_t *attrp, cm_user_t *userp, cm_req_t *reqp);

extern char cm_sysName[100];

extern int cm_ExpandSysName(char *inp, char *outp, long outSize);

extern long cm_Open(cm_scache_t *scp, int type, cm_user_t *userp);

extern long cm_CheckOpen(cm_scache_t *scp, int openMode, int trunc,
	cm_user_t *userp, cm_req_t *reqp);

/*
 * Combinations of file opening access bits for AFS.
 * We don't insist on write rights to open the file with FILE_WRITE_ATTRIBUTES,
 * because we want to enable the owner to set/clear the READONLY flag.
 * The RPC will fail if you can't modify the attributes, anyway.
 */
#define AFS_ACCESS_READ (FILE_GENERIC_READ & ~SYNCHRONIZE)
#define AFS_ACCESS_WRITE ((FILE_GENERIC_WRITE & ~(READ_CONTROL | SYNCHRONIZE)) \
				& ~FILE_WRITE_ATTRIBUTES)

extern long cm_CheckNTOpen(cm_scache_t *scp, unsigned int desiredAccess,
	unsigned int createDisp, cm_user_t *userp, cm_req_t *reqp);

extern long cm_CheckNTDelete(cm_scache_t *dscp, cm_scache_t *scp,
	cm_user_t *userp, cm_req_t *reqp);

extern long cm_EvaluateSymLink(cm_scache_t *dscp, cm_scache_t *linkScp,
	cm_scache_t **outScpp, cm_user_t *userp, cm_req_t *reqp);

extern long cm_Lock(cm_scache_t *scp, unsigned char LockType,
	LARGE_INTEGER LOffset, LARGE_INTEGER LLength,
	u_long Timeout, cm_user_t *userp, cm_req_t *reqp,
	void **lockpp);

extern long cm_Unlock(cm_scache_t *scp, unsigned char LockType,
	LARGE_INTEGER LOffset, LARGE_INTEGER LLength,
	cm_user_t *userp, cm_req_t *reqp);

extern void cm_CheckLocks();

extern long cm_RetryLock(cm_file_lock_t *oldFileLock, int vcp_is_dead);

#endif /*  __CM_VNODEOPS_H_ENV__ */
