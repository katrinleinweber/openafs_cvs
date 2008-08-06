/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <windows.h>
#include <winsock2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "afsd.h"
#include <WINNT\afssw.h>
#include <WINNT\afsreg.h>

#include <afs/param.h>
#include <afs/stds.h>
#include <afs/cellconfig.h>

#ifdef AFS_AFSDB_ENV
#include "cm_dns.h"
#include <afs/afsint.h>
#endif

static long cm_ParsePair(char *lineBufferp, char *leftp, char *rightp)
{
    char *tp;
    char tc;
    int sawEquals;
    int sawBracket;
        
    sawEquals = 0;
    sawBracket = 0;
    for(tp = lineBufferp; *tp; tp++) {
        tc = *tp;

	if (sawBracket) {
	    if (tc == ']')
		sawBracket = 0;
	    continue;
	}

	/* comment or line end */
        if (tc == '#' || tc == '\r' || tc == '\n') 
            break;

	/* square bracket comment -- look for closing delim */
	if (tc == '[') {
            sawBracket = 1; 
            continue;
        }	

	/* space or tab */
        if (tc == ' ' || tc == '\t') 
            continue;

        if (tc == '=') {
            sawEquals = 1;
            continue;
	}

        /* now we have a real character, put it in the appropriate bucket */
        if (sawEquals == 0) {
	    *leftp++ = tc;
        }	
        else {	
	    *rightp++ = tc;
        }
    }

    /* null terminate the strings */
    *leftp = 0;
    *rightp = 0;

    return 0;	/* and return success */
}

static int
IsWindowsModule(const char * name)
{
    const char * p;
    int i;

    /* Do not perform searches for probable Windows modules */
    for (p = name, i=0; *p; p++) {
	if ( *p == '.' )
	    i++;
    }
    p = strrchr(name, '.');
    if (p) {
	if (i == 1 && 
	    (!cm_stricmp_utf8N(p,".dll") ||
	     !cm_stricmp_utf8N(p,".exe") ||
	     !cm_stricmp_utf8N(p,".ini") ||
	     !cm_stricmp_utf8N(p,".db") ||
	     !cm_stricmp_utf8N(p,".drv")))
	    return 1;
    }
    return 0;
}

/* search for a cell, and either return an error code if we don't find it,
 * or return 0 if we do, in which case we also fill in the addresses in
 * the cellp field.
 *
 * new feature:  we can handle abbreviations and are insensitive to case.
 * If the caller wants the "real" cell name, it puts a non-null pointer in
 * newCellNamep.  Anomaly:  if cellNamep is ambiguous, we may modify
 * newCellNamep but return an error code.
 *
 * newCellNamep is required to be CELL_MAXNAMELEN+1 in size.
 */
long cm_SearchCellFile(char *cellNamep, char *newCellNamep,
                       cm_configProc_t *procp, void *rockp)
{
    char wdir[MAX_PATH]="";
    FILE *tfilep = NULL, *bestp, *tempp;
    char *tp;
    char lineBuffer[257];
    struct hostent *thp;
    char *valuep;
    struct sockaddr_in vlSockAddr;
    int inRightCell;
    int foundCell = 0;
    long code;
    int tracking = 1, partial = 0;

    if ( IsWindowsModule(cellNamep) )
	return -3;

    cm_GetCellServDB(wdir, sizeof(wdir));
    if (*wdir)
        tfilep = fopen(wdir, "r");

    if (!tfilep) 
        return -2;

    bestp = fopen(wdir, "r");
    
#ifdef CELLSERV_DEBUG
    osi_Log2(afsd_logp,"cm_searchfile fopen handle[%p], wdir[%s]", bestp, 
	     osi_LogSaveString(afsd_logp,wdir));
#endif
    /* have we seen the cell line for the guy we're looking for? */
    inRightCell = 0;
    while (1) {
        tp = fgets(lineBuffer, sizeof(lineBuffer), tfilep);
        if (tracking)
	    (void) fgets(lineBuffer, sizeof(lineBuffer), bestp);
        if (tp == NULL) {
	    if (feof(tfilep)) {
		/* hit EOF */
		if (partial) {
		    /*
		     * found partial match earlier;
		     * now go back to it
		     */
		    tempp = bestp;
		    bestp = tfilep;
		    tfilep = tempp;
		    inRightCell = 1;
		    partial = 0;
		    continue;
		}
		else {
		    fclose(tfilep);
		    fclose(bestp);
		    return (foundCell? 0 : -3);
		}
	    }
        }	

        /* turn trailing cr or lf into null */
        tp = strchr(lineBuffer, '\r');
        if (tp) *tp = 0;
        tp = strchr(lineBuffer, '\n');
        if (tp) *tp = 0;

	/* skip blank lines */
        if (lineBuffer[0] == 0) continue;

        if (lineBuffer[0] == '>') {
	    /* trim off at white space or '#' chars */
            tp = strchr(lineBuffer, ' ');
            if (tp) *tp = 0;
            tp = strchr(lineBuffer, '\t');
            if (tp) *tp = 0;
            tp = strchr(lineBuffer, '#');
            if (tp) *tp = 0;

	    /* now see if this is the right cell */
            if (stricmp(lineBuffer+1, cellNamep) == 0) {
		/* found the cell we're looking for */
		if (newCellNamep) {
		    strncpy(newCellNamep, lineBuffer+1,CELL_MAXNAMELEN+1);
                    newCellNamep[CELL_MAXNAMELEN] = '\0';
                    strlwr(newCellNamep);
                }
                inRightCell = 1;
		tracking = 0;
#ifdef CELLSERV_DEBUG                
		osi_Log2(afsd_logp, "cm_searchfile is cell inRightCell[%p], linebuffer[%s]",
			 inRightCell, osi_LogSaveString(afsd_logp,lineBuffer));
#endif
	    }
	    else if (cm_stricmp_utf8(lineBuffer+1, cellNamep) == 0) {
		/* partial match */
		if (partial) {	/* ambiguous */
		    fclose(tfilep);
		    fclose(bestp);
		    return -5;
		}
		if (newCellNamep) {
		    strncpy(newCellNamep, lineBuffer+1,CELL_MAXNAMELEN+1);
                    newCellNamep[CELL_MAXNAMELEN] = '\0';
                    strlwr(newCellNamep);
                }
		inRightCell = 0;
		tracking = 0;
		partial = 1;
	    }
            else inRightCell = 0;
        }
        else {
            valuep = strchr(lineBuffer, '#');
	    if (valuep == NULL) {
		fclose(tfilep);
		fclose(bestp);
		return -4;
	    }
            valuep++;	/* skip the "#" */

            valuep += strspn(valuep, " \t"); /* skip SP & TAB */
            /* strip spaces and tabs in the end. They should not be there according to CellServDB format
             * so do this just in case                        
	     */
            while (valuep[strlen(valuep) - 1] == ' ' || valuep[strlen(valuep) - 1] == '\t') 
                valuep[strlen(valuep) - 1] = '\0';

	    if (inRightCell) {
		/* add the server to the VLDB list */
                WSASetLastError(0);
                thp = gethostbyname(valuep);
#ifdef CELLSERV_DEBUG
		osi_Log3(afsd_logp,"cm_searchfile inRightCell thp[%p], valuep[%s], WSAGetLastError[%d]",
			 thp, osi_LogSaveString(afsd_logp,valuep), WSAGetLastError());
#endif
		if (thp) {
		    memcpy(&vlSockAddr.sin_addr.s_addr, thp->h_addr,
                            sizeof(long));
                    vlSockAddr.sin_family = AF_INET;
                    /* sin_port supplied by connection code */
		    if (procp)
			(*procp)(rockp, &vlSockAddr, valuep);
                    foundCell = 1;
		}
                if (!thp) {
                    long ip_addr;
		    int c1, c2, c3, c4;
		    char aname[241] = "";                    
                    
                    /* Since there is no gethostbyname() data 
		     * available we will read the IP address
		     * stored in the CellServDB file
                     */
                    code = sscanf(lineBuffer, "%d.%d.%d.%d #%s",
                                   &c1, &c2, &c3, &c4, aname);
                    tp = (char *) &ip_addr;
                    *tp++ = c1;
                    *tp++ = c2;
                    *tp++ = c3;
                    *tp++ = c4;
                    memcpy(&vlSockAddr.sin_addr.s_addr, &ip_addr,
                            sizeof(long));
                    vlSockAddr.sin_family = AF_INET;
                    /* sin_port supplied by connection code */
                    if (procp)
                        (*procp)(rockp, &vlSockAddr, valuep);
                    foundCell = 1;
                }
            }
        }	/* a vldb line */
    }		/* while loop processing all lines */

    /* if for some unknown reason cell is not found, return negative code (-11) ??? */
    return (foundCell) ? 0 : -11;
}

/* newCellNamep is required to be CELL_MAXNAMELEN+1 in size */
long cm_SearchCellByDNS(char *cellNamep, char *newCellNamep, int *ttl,
               cm_configProc_t *procp, void *rockp)
{
#ifdef AFS_AFSDB_ENV
    int rc;
    int  cellHostAddrs[AFSMAXCELLHOSTS];
    char cellHostNames[AFSMAXCELLHOSTS][MAXHOSTCHARS];
    int numServers;
    int i;
    struct sockaddr_in vlSockAddr;
#ifdef CELLSERV_DEBUG
    osi_Log1(afsd_logp,"SearchCellDNS-Doing search for [%s]", osi_LogSaveString(afsd_logp,cellNamep));
#endif
    if ( IsWindowsModule(cellNamep) )
	return -1;
    rc = getAFSServer(cellNamep, cellHostAddrs, cellHostNames, &numServers, ttl);
    if (rc == 0 && numServers > 0) {     /* found the cell */
        for (i = 0; i < numServers; i++) {
            memcpy(&vlSockAddr.sin_addr.s_addr, &cellHostAddrs[i],
                   sizeof(long));
            vlSockAddr.sin_family = AF_INET;
            /* sin_port supplied by connection code */
            if (procp)
                (*procp)(rockp, &vlSockAddr, cellHostNames[i]);
            if (newCellNamep) {
                strncpy(newCellNamep,cellNamep,CELL_MAXNAMELEN+1);
                newCellNamep[CELL_MAXNAMELEN] = '\0';
                strlwr(newCellNamep);
            }
        }
        return 0;   /* found cell */
    }
    else
       return -1;  /* not found */
#else
    return -1;  /* not found */
#endif /* AFS_AFSDB_ENV */
}

/* use cm_GetConfigDir() plus AFS_CELLSERVDB to 
 * generate the fully qualified name of the CellServDB 
 * file.
 */
long cm_GetCellServDB(char *cellNamep, afs_uint32 len)
{
    size_t tlen;
    
    cm_GetConfigDir(cellNamep, len);

    /* add trailing backslash, if required */
    tlen = (int)strlen(cellNamep);
    if (tlen) {
        if (cellNamep[tlen-1] != '\\') {
            strncat(cellNamep, "\\", len);
            cellNamep[len-1] = '\0';
        }
        
        strncat(cellNamep, AFS_CELLSERVDB, len);
        cellNamep[len-1] = '\0';
    }
    return 0;
}

/* look up the root cell's name in the Registry */
long cm_GetRootCellName(char *cellNamep)
{
    DWORD code, dummyLen;
    HKEY parmKey;

    code = RegOpenKeyEx(HKEY_LOCAL_MACHINE, AFSREG_CLT_SVC_PARAM_SUBKEY,
                        0, KEY_QUERY_VALUE, &parmKey);
    if (code != ERROR_SUCCESS)
        return -1;

    dummyLen = 256;
    code = RegQueryValueEx(parmKey, "Cell", NULL, NULL,
				cellNamep, &dummyLen);
    RegCloseKey (parmKey);
    if (code != ERROR_SUCCESS || cellNamep[0] == 0)
        return -1;

    return 0;
}

cm_configFile_t *cm_CommonOpen(char *namep, char *rwp)
{
    char wdir[MAX_PATH]="";
    FILE *tfilep = NULL;

    cm_GetConfigDir(wdir, sizeof(wdir));
    if (*wdir) {
        strncat(wdir, namep, sizeof(wdir));
        wdir[sizeof(wdir)-1] = '\0';
        
        tfilep = fopen(wdir, rwp);
    }
    return ((cm_configFile_t *) tfilep);        
}	

long cm_WriteConfigString(char *labelp, char *valuep)
{
    DWORD code, dummyDisp;
    HKEY parmKey;

    code = RegCreateKeyEx(HKEY_LOCAL_MACHINE, AFSREG_CLT_SVC_PARAM_SUBKEY,
			   0, "container", 0, KEY_SET_VALUE, NULL,
			   &parmKey, &dummyDisp);
    if (code != ERROR_SUCCESS)
	return -1;

    code = RegSetValueEx(parmKey, labelp, 0, REG_SZ,
			  valuep, (DWORD)strlen(valuep) + 1);
    RegCloseKey (parmKey);
    if (code != ERROR_SUCCESS)
	return (long)-1;

    return (long)0;
}

long cm_WriteConfigInt(char *labelp, long value)
{
    DWORD code, dummyDisp;
    HKEY parmKey;

    code = RegCreateKeyEx(HKEY_LOCAL_MACHINE, AFSREG_CLT_SVC_PARAM_SUBKEY,
                           0, "container", 0, KEY_SET_VALUE, NULL,
                           &parmKey, &dummyDisp);
    if (code != ERROR_SUCCESS)
        return -1;

    code = RegSetValueEx(parmKey, labelp, 0, REG_DWORD,
                          (LPBYTE)&value, sizeof(value));
    RegCloseKey (parmKey);
    if (code != ERROR_SUCCESS)
        return -1;

    return 0;
}

cm_configFile_t *cm_OpenCellFile(void)
{
    cm_configFile_t *cfp;

    cfp = cm_CommonOpen(AFS_CELLSERVDB ".new", "w");
    return cfp;
}

long cm_AppendPrunedCellList(cm_configFile_t *ofp, char *cellNamep)
{
    cm_configFile_t *tfilep;	/* input file */
    char *tp;
    char lineBuffer[256];
    char *valuep;
    int inRightCell;
    int foundCell;

    tfilep = cm_CommonOpen(AFS_CELLSERVDB, "r");
    if (!tfilep) 
        return -1;

    foundCell = 0;

    /* have we seen the cell line for the guy we're looking for? */
    inRightCell = 0;
    while (1) {
        tp = fgets(lineBuffer, sizeof(lineBuffer), (FILE *)tfilep);
        if (tp == NULL) {
            if (feof((FILE *)tfilep)) {
                /* hit EOF */
                fclose((FILE *)tfilep);
                return 0;
            }
        }

        /* turn trailing cr or lf into null */
        tp = strchr(lineBuffer, '\r');
        if (tp) *tp = 0;
        tp = strchr(lineBuffer, '\n');
        if (tp) *tp = 0;
                
        /* skip blank lines */
        if (lineBuffer[0] == 0) {
            fprintf((FILE *)ofp, "%s\n", lineBuffer);
            continue;
        }

        if (lineBuffer[0] == '>') {
            /* trim off at white space or '#' chars */
            tp = strchr(lineBuffer, ' ');
            if (tp) *tp = 0;
            tp = strchr(lineBuffer, '\t');
            if (tp) *tp = 0;
            tp = strchr(lineBuffer, '#');
            if (tp) *tp = 0;

            /* now see if this is the right cell */
            if (strcmp(lineBuffer+1, cellNamep) == 0) {
                /* found the cell we're looking for */
                inRightCell = 1;
            }
            else {
                inRightCell = 0;
                fprintf((FILE *)ofp, "%s\n", lineBuffer);
            }
        }
        else {
            valuep = strchr(lineBuffer, '#');
            if (valuep == NULL) return -2;
            valuep++;	/* skip the "#" */
            if (!inRightCell) {
                fprintf((FILE *)ofp, "%s\n", lineBuffer);
            }
        }	/* a vldb line */
    }		/* while loop processing all lines */
}       

long cm_AppendNewCell(cm_configFile_t *filep, char *cellNamep)
{
    fprintf((FILE *)filep, ">%s\n", cellNamep);
    return 0;
}

long cm_AppendNewCellLine(cm_configFile_t *filep, char *linep)
{
    fprintf((FILE *)filep, "%s\n", linep);
    return 0;
}

long cm_CloseCellFile(cm_configFile_t *filep)
{
    char wdir[MAX_PATH];
    char sdir[MAX_PATH];
    long code;
    long closeCode;
    closeCode = fclose((FILE *)filep);

    cm_GetConfigDir(wdir, sizeof(wdir));
    strcpy(sdir, wdir);

    if (closeCode != 0) {
        /* something went wrong, preserve original database */
        strncat(wdir, AFS_CELLSERVDB ".new", sizeof(wdir));
        wdir[sizeof(wdir)-1] = '\0';
        unlink(wdir);
        return closeCode;
    }

    strncat(wdir, AFS_CELLSERVDB, sizeof(wdir));
    wdir[sizeof(wdir)-1] = '\0';
    strncat(sdir, AFS_CELLSERVDB ".new", sizeof(sdir));/* new file */
    sdir[sizeof(sdir)-1] = '\0';

    unlink(sdir);			/* delete old file */

    code = rename(sdir, wdir);	/* do the rename */

    if (code) 
        code = errno;

    return code;
}   

void cm_GetConfigDir(char *dir, afs_uint32 len)
{
    char * dirp = NULL;

    if (!afssw_GetClientCellServDBDir(&dirp)) {
        strncpy(dir, dirp, len);
        dir[len-1] = '\0';
        free(dirp);
    } else {
        dir[0] = '\0';
    }
}
