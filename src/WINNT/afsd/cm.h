/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef __CM_H_ENV__
#define __CM_H_ENV__ 1

#include <rx/rx.h>
#ifdef DJGPP      /* we need these for vldbentry decl., etc. */
#include <afs/vldbint.h>
#include <afs/afsint.h>
#endif /* DJGPP */

/* from .xg file */
long VL_GetEntryByID(struct rx_connection *, long, long, struct vldbentry *);
long VL_GetEntryByNameO(struct rx_connection *, char *, struct vldbentry *);
long VL_ProbeServer(struct rx_connection *);
long VL_GetEntryBYIDN(struct rx_connection *, long, long, struct nvldbentry *);
long VL_GetEntryByNameN(struct rx_connection *, char *, struct nvldbentry *);

/* from .xg file */
extern StartRXAFS_FetchData (struct rx_call *,
	struct AFSFid *Fid,
	afs_int32 Pos, 
	afs_int32 Length);
extern EndRXAFS_FetchData (struct rx_call *,
	struct AFSFetchStatus *OutStatus, 
	struct AFSCallBack *CallBack, 
	struct AFSVolSync *Sync);

extern RXAFS_FetchACL(struct rx_connection *,
	struct AFSFid *Fid, 
	struct AFSOpaque *AccessList, 
	struct AFSFetchStatus *OutStatus, 
	struct AFSVolSync *Sync);

extern RXAFS_FetchStatus (struct rx_connection *,
	struct AFSFid *Fid, 
	struct AFSFetchStatus *OutStatus, 
	struct AFSCallBack *CallBack, 
	struct AFSVolSync *Sync);

int StartRXAFS_StoreData (struct rx_call *,
	struct AFSFid *Fid, 
	struct AFSStoreStatus *InStatus, 
	afs_int32 Pos, 
	afs_int32 Length, 
	afs_int32 FileLength);

int EndRXAFS_StoreData(struct rx_call *,
	struct AFSFetchStatus *OutStatus, 
	struct AFSVolSync *Sync);

int RXAFS_StoreACL (struct rx_connection *,
	struct AFSFid *Fid, 
	struct AFSOpaque *AccessList,  
	struct AFSFetchStatus *OutStatus, 
	struct AFSVolSync *Sync);

int RXAFS_StoreStatus(struct rx_connection *,
	struct AFSFid *Fid, 
	struct AFSStoreStatus *InStatus, 
	struct AFSFetchStatus *OutStatus, 
	struct AFSVolSync *Sync);

int RXAFS_RemoveFile (struct rx_connection *,
	struct AFSFid *DirFid, 
	char *namep,
	struct AFSFetchStatus *OutStatus, 
	struct AFSVolSync *Sync);

int RXAFS_CreateFile (struct rx_connection *,
	struct AFSFid *DirFid, 
	char *Name,
	struct AFSStoreStatus *InStatus, 
	struct AFSFid *OutFid, 
	struct AFSFetchStatus *OutFidStatus, 
	struct AFSFetchStatus *OutDirStatus, 
	struct AFSCallBack *CallBack, 
	struct AFSVolSync *Sync);

int RXAFS_Rename (struct rx_connection *,
	struct AFSFid *OldDirFid, 
	char *OldName,
	struct AFSFid *NewDirFid, 
	char *NewName,
	struct AFSFetchStatus *OutOldDirStatus, 
	struct AFSFetchStatus *OutNewDirStatus, 
	struct AFSVolSync *Sync);

int RXAFS_Symlink (struct rx_connection *,
	struct AFSFid *DirFid, 
	char *name,
	char *LinkContents,
	struct AFSStoreStatus *InStatus,
	struct AFSFid *OutFid, 
	struct AFSFetchStatus *OutFidStatus, 
	struct AFSFetchStatus *OutDirStatus, 
	struct AFSVolSync *Sync);

int RXAFS_Link (struct rx_connection *,
	struct AFSFid *DirFid, 
	char *Name,
	struct AFSFid *ExistingFid, 
	struct AFSFetchStatus *OutFidStatus, 
	struct AFSFetchStatus *OutDirStatus, 
	struct AFSVolSync *Sync);

int RXAFS_MakeDir (struct rx_connection *,
	struct AFSFid *DirFid, 
	char *name,
	struct AFSStoreStatus *InStatus, 
	struct AFSFid *OutFid, 
	struct AFSFetchStatus *OutFidStatus, 
	struct AFSFetchStatus *OutDirStatus, 
	struct AFSCallBack *CallBack, 
	struct AFSVolSync *Sync);

int RXAFS_RemoveDir (struct rx_connection *,
	struct AFSFid *DirFid, 
	char *Name,
	struct AFSFetchStatus *OutDirStatus, 
	struct AFSVolSync *Sync);

int RXAFS_GetStatistics (struct rx_connection *,
	struct ViceStatistics *Statistics);

int RXAFS_GiveUpCallBacks (struct rx_connection *,
	struct AFSCBFids *Fids_Array,
	struct AFSCBs *CallBacks_Array);

int RXAFS_GetVolumeInfo (struct rx_connection *,
	char *VolumeName,
	struct VolumeInfo *Volumeinfo);

int RXAFS_GetVolumeStatus (struct rx_connection *,
	afs_int32 Volumeid, 
	struct AFSFetchVolumeStatus *Volumestatus, 
	char **name,
        char **offlineMsg,
        char **motd);

int RXAFS_SetVolumeStatus (struct rx_connection *,
	afs_int32 Volumeid, 
	struct AFSStoreVolumeStatus *Volumestatus,
	char *name,
	char *olm,
	char *motd);

int RXAFS_GetRootVolume (struct rx_connection *,
	char **VolumeName);

int RXAFS_CheckToken (struct rx_connection *,
	afs_int32 ViceId,
	struct AFSOpaque *token);

int RXAFS_GetTime (struct rx_connection *,
	afs_uint32 *Seconds, 
	afs_uint32 *USeconds);

int RXAFS_BulkStatus (struct rx_connection *,
	struct AFSCBFids *FidsArray,
	struct AFSBulkStats *StatArray,
	struct AFSCBs *CBArray,
	struct AFSVolSync *Sync);

int RXAFS_SetLock (struct rx_connection *,
	struct AFSFid *Fid, 
	int Type, 
	struct AFSVolSync *Sync);

int RXAFS_ExtendLock (struct rx_connection *,
	struct AFSFid *Fid, 
	struct AFSVolSync *Sync);

int RXAFS_ReleaseLock (struct rx_connection *,
	struct AFSFid *Fid, 
	struct AFSVolSync *Sync);

/* This interface is to supported the AFS/DFS Protocol Translator */
int RXAFS_Lookup (struct rx_connection *,
	struct AFSFid *DirFid,
	char *Name,
	struct AFSFid *OutFid,
	struct AFSFetchStatus *OutFidStatus,
	struct AFSFetchStatus *OutDirStatus,
	struct AFSCallBack *CallBack,
	struct AFSVolSync *Sync);

/* common flags to many procedures */
#define CM_FLAG_CREATE		1		/* create entry */
#define CM_FLAG_CASEFOLD	2		/* fold case in namei, lookup, etc. */
#define CM_FLAG_EXCLUSIVE	4		/* create exclusive */
#define CM_FLAG_FOLLOW		8		/* follow symlinks, even at the end (namei) */
#define CM_FLAG_8DOT3		0x10		/* restrict to 8.3 name */
#define CM_FLAG_NOMOUNTCHASE	0x20		/* don't follow mount points */
#define CM_FLAG_DIRSEARCH	0x40		/* for directory search */
#define CM_FLAG_CHECKPATH	0x80		/* Path instead of File */

/* error codes */
#define CM_ERROR_BASE			0x66543200
#define CM_ERROR_NOSUCHCELL		(CM_ERROR_BASE+0)
#define CM_ERROR_NOSUCHVOLUME		(CM_ERROR_BASE+1)
#define CM_ERROR_TIMEDOUT		(CM_ERROR_BASE+2)
#define CM_ERROR_RETRY			(CM_ERROR_BASE+3)
#define CM_ERROR_NOACCESS		(CM_ERROR_BASE+4)
#define CM_ERROR_NOSUCHFILE		(CM_ERROR_BASE+5)
#define CM_ERROR_STOPNOW		(CM_ERROR_BASE+6)
#define CM_ERROR_TOOBIG			(CM_ERROR_BASE+7)
#define CM_ERROR_INVAL			(CM_ERROR_BASE+8)
#define CM_ERROR_BADFD			(CM_ERROR_BASE+9)
#define CM_ERROR_BADFDOP		(CM_ERROR_BASE+10)
#define CM_ERROR_EXISTS			(CM_ERROR_BASE+11)
#define CM_ERROR_CROSSDEVLINK		(CM_ERROR_BASE+12)
#define CM_ERROR_BADOP			(CM_ERROR_BASE+13)
#define CM_ERROR_BADSMB			(CM_ERROR_BASE+32)
/* CM_ERROR_BADPASSWORD used to be here */
#define CM_ERROR_NOTDIR			(CM_ERROR_BASE+15)
#define CM_ERROR_ISDIR			(CM_ERROR_BASE+16)
#define CM_ERROR_READONLY		(CM_ERROR_BASE+17)
#define CM_ERROR_WOULDBLOCK		(CM_ERROR_BASE+18)
#define CM_ERROR_QUOTA			(CM_ERROR_BASE+19)
#define CM_ERROR_SPACE			(CM_ERROR_BASE+20)
#define CM_ERROR_BADSHARENAME		(CM_ERROR_BASE+21)
#define CM_ERROR_BADTID			(CM_ERROR_BASE+22)
#define CM_ERROR_UNKNOWN		(CM_ERROR_BASE+23)
#define CM_ERROR_NOMORETOKENS		(CM_ERROR_BASE+24)
#define CM_ERROR_NOTEMPTY		(CM_ERROR_BASE+25)
#define CM_ERROR_USESTD			(CM_ERROR_BASE+26)
#define CM_ERROR_REMOTECONN		(CM_ERROR_BASE+27)
#define CM_ERROR_ATSYS			(CM_ERROR_BASE+28)
#define CM_ERROR_NOSUCHPATH		(CM_ERROR_BASE+29)

#define CM_ERROR_CLOCKSKEW		(CM_ERROR_BASE+31)

#define CM_ERROR_ALLBUSY		(CM_ERROR_BASE+33)
#define CM_ERROR_NOFILES		(CM_ERROR_BASE+34)
#define CM_ERROR_PARTIALWRITE		(CM_ERROR_BASE+35)
#define CM_ERROR_NOIPC			(CM_ERROR_BASE+36)
#define CM_ERROR_BADNTFILENAME		(CM_ERROR_BASE+37)
#define CM_ERROR_BUFFERTOOSMALL		(CM_ERROR_BASE+38)
#define CM_ERROR_RENAME_IDENTICAL	(CM_ERROR_BASE+39)

#endif /*  __CM_H_ENV__ */
