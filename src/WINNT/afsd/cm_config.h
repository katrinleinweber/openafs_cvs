/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef __CONFIG_H_ENV_
#define __CONFIG_H_ENV_ 1

#define CM_CONFIGDEFAULT_CACHESIZE	98304
#define CM_CONFIGDEFAULT_BLOCKSIZE	4096
#define CM_CONFIGDEFAULT_ASYNCSTORESIZE	131072  /* 128K */
#define CM_CONFIGDEFAULT_CELLS          1024
#define CM_CONFIGDEFAULT_STATS		10000
#define CM_CONFIGDEFAULT_CHUNKSIZE	18      /* 256KB */
#define CM_CONFIGDEFAULT_DAEMONS	4
#define CM_CONFIGDEFAULT_SVTHREADS	25
#define CM_CONFIGDEFAULT_TRACEBUFSIZE	10000

#ifndef __CM_CONFIG_INTERFACES_ONLY__

#include <stdio.h>

typedef FILE cm_configFile_t;

typedef long (cm_configProc_t)(void *rockp, struct sockaddr_in *addrp, char *namep);

extern long cm_GetRootCellName(char *namep);

extern long cm_SearchCellFile(char *cellNamep, char *newCellNamep,
                              cm_configProc_t *procp, void *rockp);

extern long cm_SearchCellFileEx(char *cellNamep, char *newCellNamep,
                                char *linkedNamep,
                                cm_configProc_t *procp, void *rockp);

extern long cm_SearchCellByDNS(char *cellNamep, char *newCellNamep, int *ttl,
                               cm_configProc_t *procp, void *rockp);

extern long cm_WriteConfigString(char *labelp, char *valuep);

extern long cm_WriteConfigInt(char *labelp, long value);

extern cm_configFile_t *cm_OpenCellFile(void);

extern long cm_AppendPrunedCellList(cm_configFile_t *filep, char *cellNamep);

extern long cm_AppendNewCell(cm_configFile_t *filep, char *cellNamep);

extern long cm_AppendNewCellLine(cm_configFile_t *filep, char *linep);

extern long cm_CloseCellFile(cm_configFile_t *filep);

extern long cm_GetCellServDB(char *cellNamep, afs_uint32 len);

extern void cm_GetConfigDir(char *dir, afs_uint32 len);

/* TODO: these should be pulled in from dirpath.h */
#define AFS_THISCELL "ThisCell"
#define AFS_CELLSERVDB_UNIX "CellServDB"
#define AFS_CELLSERVDB AFS_CELLSERVDB_UNIX

#endif /* __CM_CONFIG_INTERFACES_ONLY__ */

#endif /* __CONFIG_H_ENV_ */
