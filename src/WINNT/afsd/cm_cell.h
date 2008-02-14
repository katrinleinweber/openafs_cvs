/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef __CELL_H_ENV_
#define __CELL_H_ENV_ 1

#include "cm_server.h"

#define CELL_MAXNAMELEN                 256

#define CM_CELL_MAGIC    ('C' | 'E' <<8 | 'L'<<16 | 'L'<<24)

/* a cell structure */
typedef struct cm_cell {        
    afs_uint32  magic;
    long cellID;		        /* cell ID */
    struct cm_cell *nextp;	        /* locked by cm_cellLock */
    char name[CELL_MAXNAMELEN];         /* cell name; never changes */
    cm_serverRef_t *vlServersp;         /* locked by cm_serverLock */
    osi_mutex_t mx;			/* mutex locking fields (flags) */
    long flags;			        /* locked by mx */
    time_t timeout;                     /* if dns, time at which the server addrs expire */
} cm_cell_t;

/* These are bit flag values */
#define CM_CELLFLAG_SUID	     1	/* setuid flag; not yet used */
#define CM_CELLFLAG_DNS              2  /* cell servers are from DNS */
#define CM_CELLFLAG_VLSERVER_INVALID 4  /* cell servers are invalid */
#define CM_CELLFLAG_FREELANCE        8  /* local freelance fake cell */

extern void cm_InitCell(int newFile, long maxCells);

extern long cm_ShutdownCell(void);

extern long cm_ValidateCell(void);

extern cm_cell_t *cm_GetCell(char *namep, long flags);

extern cm_cell_t *cm_GetCell_Gen(char *namep, char *newnamep, long flags);

extern cm_cell_t *cm_FindCellByID(long cellID);

extern void cm_ChangeRankCellVLServer(cm_server_t       *tsp);

extern osi_rwlock_t cm_cellLock;

extern cm_cell_t *cm_allCellsp;

#endif /* __CELL_H_ENV_ */
