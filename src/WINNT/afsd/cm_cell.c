/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afs/param.h>
#include <afs/stds.h>

#ifndef DJGPP
#include <windows.h>
#include <nb30.h>
#include <winsock2.h>
#endif /* !DJGPP */
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <osi.h>
#include <string.h>

#include "afsd.h"

osi_rwlock_t cm_cellLock;

cm_cell_t *cm_allCellsp;

/* function called as callback proc from cm_SearchCellFile.  Return 0 to
 * continue processing.
 */
long cm_AddCellProc(void *rockp, struct sockaddr_in *addrp, char *namep)
{
	cm_server_t *tsp;
	cm_serverRef_t *tsrp;
        cm_cell_t *cellp;
        
	cellp = rockp;

	/* if this server was previously created by fs setserverprefs */
	if ( tsp = cm_FindServer(addrp, CM_SERVER_VLDB))
	{
		if ( !tsp->cellp )
			tsp->cellp = cellp;
	}
	else
        	tsp = cm_NewServer(addrp, CM_SERVER_VLDB, cellp);

	/* Insert the vlserver into a sorted list, sorted by server rank */
	tsrp = cm_NewServerRef(tsp);
	cm_InsertServerList(&cellp->vlServersp, tsrp);

	return 0;
}

/* load up a cell structure from the cell database, afsdcell.ini */
cm_cell_t *cm_GetCell(char *namep, long flags)
{
  return cm_GetCell_Gen(namep, NULL, flags);
}

cm_cell_t *cm_GetCell_Gen(char *namep, char *newnamep, long flags)
{
	cm_cell_t *cp;
        long code;
        static cellCounter = 1;		/* locked by cm_cellLock */
	int ttl;
	char fullname[200];

	lock_ObtainWrite(&cm_cellLock);
	for(cp = cm_allCellsp; cp; cp=cp->nextp) {
		if (strcmp(namep, cp->namep) == 0) {
		  strcpy(fullname, cp->namep);
		  break;
		}
        }

	if ((!cp && (flags & CM_FLAG_CREATE))
#ifdef AFS_AFSDB_ENV
	    /* if it's from DNS, see if it has expired */
	    || (cp && (cp->flags & CM_CELLFLAG_DNS) && (time(0) > cp->timeout))
#endif
	  ) {
		if (!cp) cp = malloc(sizeof(*cp));
                memset(cp, 0, sizeof(*cp));
                code = cm_SearchCellFile(namep, fullname, cm_AddCellProc, cp);
		if (code) {
#ifdef AFS_AFSDB_ENV
		  if (cm_dnsEnabled /*&& cm_DomainValid(namep)*/)
		    code = cm_SearchCellByDNS(namep, fullname, &ttl, cm_AddCellProc, cp);
#endif
		  if (code) {
		    free(cp);
		    cp = NULL;
		    goto done;
		  }
#ifdef AFS_AFSDB_ENV
		  else {   /* got cell from DNS */
		    cp->flags |= CM_CELLFLAG_DNS;
		    cp->timeout = time(0) + ttl;
		  }
#endif
		}

		/* randomise among those vlservers having the same rank*/ 
		cm_RandomizeServer(&cp->vlServersp);

                /* otherwise we found the cell, and so we're nearly done */
                lock_InitializeMutex(&cp->mx, "cm_cell_t mutex");

		/* copy in name */
                cp->namep = malloc(strlen(fullname)+1);
                strcpy(cp->namep, fullname);

		/* thread on global list */
                cp->nextp = cm_allCellsp;
                cm_allCellsp = cp;
                
                cp->cellID = cellCounter++;
        }

done:
	if (newnamep)
	  strcpy(newnamep, fullname);
	lock_ReleaseWrite(&cm_cellLock);
        return cp;
}

cm_cell_t *cm_FindCellByID(long cellID)
{
	cm_cell_t *cp;
	int ttl;
     int code;

	lock_ObtainWrite(&cm_cellLock);
	for(cp = cm_allCellsp; cp; cp=cp->nextp) {
		if (cellID == cp->cellID) break;
        }

#ifdef AFS_AFSDB_ENV
	/* if it's from DNS, see if it has expired */
	if (cp && cm_dnsEnabled && (cp->flags & CM_CELLFLAG_DNS) && (time(0) > cp->timeout)) {
	  code = cm_SearchCellByDNS(cp->namep, NULL, &ttl, cm_AddCellProc, cp);
	  if (code == 0) {   /* got cell from DNS */
	    cp->flags |= CM_CELLFLAG_DNS;
#ifdef DEBUG
	    fprintf(stderr, "cell %s: ttl=%d\n", cp->namep, ttl);
#endif
	    cp->timeout = time(0) + ttl;
	  }
	  /* if we fail to find it this time, we'll just do nothing and leave the
	     current entry alone */
	}
#endif /* AFS_AFSDB_ENV */

	lock_ReleaseWrite(&cm_cellLock);	
	
        return cp;
}

void cm_InitCell(void)
{
	static osi_once_t once;
        
        if (osi_Once(&once)) {
		lock_InitializeRWLock(&cm_cellLock, "cell global lock");
                cm_allCellsp = NULL;
		osi_EndOnce(&once);
        }
}
void cm_ChangeRankCellVLServer(cm_server_t       *tsp)
{
	cm_cell_t *cp;
	int code;

	cp = tsp->cellp;	/* cell that this vlserver belongs to */
	osi_assert(cp);

	lock_ObtainMutex(&cp->mx);
	code = cm_ChangeRankServer(&cp->vlServersp, tsp);

	if ( !code ) 		/* if the server list was rearranged */
	    cm_RandomizeServer(&cp->vlServersp);

	lock_ReleaseMutex(&cp->mx);
}

