#include <afs/param.h>
#include <afs/stds.h>

#ifndef DJGPP
#include <windows.h>
#include <winsock2.h>
#else
#include <netdb.h>
#endif /* !DJGPP */
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include <rx/rx.h>

#include "afsd.h"
#ifdef AFS_FREELANCE_CLIENT
#include "cm_freelance.h"
#include "stdio.h"

int cm_noLocalMountPoints;
int cm_fakeDirSize;
int cm_fakeDirCallback=0;
int cm_fakeGettingCallback=0;
int cm_fakeDirVersion = 0x8;
cm_localMountPoint_t* cm_localMountPoints;
osi_mutex_t cm_Freelance_Lock;
int cm_localMountPointChangeFlag = 0;
int cm_freelanceEnabled = 0;

void cm_InitFakeRootDir();

void cm_InitFreelance() {
  
	lock_InitializeMutex(&cm_Freelance_Lock, "Freelance Lock");
  
	// yj: first we make a call to cm_initLocalMountPoints
	// to read all the local mount points from an ini file
	cm_InitLocalMountPoints();
	
	// then we make a call to InitFakeRootDir to create
	// a fake root directory based on the local mount points
	cm_InitFakeRootDir();

	// --- end of yj code
}

/* yj: Initialization of the fake root directory */
/* to be called while holding freelance lock unless during init. */
void cm_InitFakeRootDir() {
	
	int i, j, t1, t2;
	char* currentPos;
	int noChunks;
	char mask;
	

	// allocate space for the fake info
	cm_dirHeader_t fakeDirHeader;
	cm_dirEntry_t fakeEntry;
	cm_pageHeader_t fakePageHeader;

	// i'm going to calculate how much space is needed for
	// this fake root directory. we have these rules:
	// 1. there are cm_noLocalMountPoints number of entries
	// 2. each page is CM_DIR_PAGESIZE in size
	// 3. the first 13 chunks of the first page are used for
	//    some header stuff
	// 4. the first chunk of all subsequent pages are used
	//    for page header stuff
	// 5. a max of CM_DIR_EPP entries are allowed per page
	// 6. each entry takes 1 or more chunks, depending on 
	//    the size of the mount point string, as determined
	//    by cm_NameEntries
	// 7. each chunk is CM_DIR_CHUNKSIZE bytes

	int CPP = CM_DIR_PAGESIZE / CM_DIR_CHUNKSIZE;
	int curChunk = 13;	// chunks 0 - 12 are used for header stuff
						// of the first page in the directory
	int curPage = 0;
	int curDirEntry = 0;
	int curDirEntryInPage = 0;
	int sizeOfCurEntry;
	int dirSize;

	/* Reserve 2 directory chunks for "." and ".." */
	curChunk += 2;

	while (curDirEntry!=cm_noLocalMountPoints) {
		sizeOfCurEntry = cm_NameEntries((cm_localMountPoints+curDirEntry)->namep, 0);
		if ((curChunk + sizeOfCurEntry >= CPP) ||
			(curDirEntryInPage + 1 >= CM_DIR_EPP)) {
			curPage++;
			curDirEntryInPage = 0;
			curChunk = 1;
		}
		curChunk += sizeOfCurEntry;
		curDirEntry++;
		curDirEntryInPage++;
	}

	dirSize = (curPage+1) *  CM_DIR_PAGESIZE;
	cm_FakeRootDir = malloc(dirSize);
	cm_fakeDirSize = dirSize;

	

	// yj: when we get here, we've figured out how much memory we need and 
	// allocated the appropriate space for it. we now prceed to fill
	// it up with entries.
	curPage = 0;
	curDirEntry = 0;
	curDirEntryInPage = 0;
	curChunk = 0;
	
	// fields in the directory entry that are unused.
	fakeEntry.flag = 1;
	fakeEntry.length = 0;
	fakeEntry.next = 0;
	fakeEntry.fid.unique = htonl(1);

	// the first page is special, it uses fakeDirHeader instead of fakePageHeader
	// we fill up the page with dirEntries that belong there and we make changes
	// to the fakeDirHeader.header.freeBitmap along the way. Then when we're done
	// filling up the dirEntries in this page, we copy the fakeDirHeader into 
	// the top of the page.

	// init the freeBitmap array
	for (i=0; i<8; i++) 
		fakeDirHeader.header.freeBitmap[i]=0;

	fakeDirHeader.header.freeBitmap[0] = 0xff;
	fakeDirHeader.header.freeBitmap[1] = 0x7f;
	

	// we start counting at 13 because the 0th to 12th chunks are used for header
	curChunk = 13;

	// stick the first 2 entries "." and ".." in
	fakeEntry.fid.unique = htonl(1);
	fakeEntry.fid.vnode = htonl(1);
	strcpy(fakeEntry.name, ".");
	currentPos = cm_FakeRootDir + curPage * CM_DIR_PAGESIZE + curChunk * CM_DIR_CHUNKSIZE;
	memcpy(currentPos, &fakeEntry, CM_DIR_CHUNKSIZE);
	curChunk++; curDirEntryInPage++;
	strcpy(fakeEntry.name, "..");
	currentPos = cm_FakeRootDir + curPage * CM_DIR_PAGESIZE + curChunk * CM_DIR_CHUNKSIZE;
	memcpy(currentPos, &fakeEntry, CM_DIR_CHUNKSIZE);
	curChunk++; curDirEntryInPage++;

	// keep putting stuff into page 0 if
	// 1. we're not done with all entries
	// 2. we have less than CM_DIR_EPP entries in page 0
	// 3. we're not out of chunks in page 0

	while( (curDirEntry!=cm_noLocalMountPoints) && 
		   (curDirEntryInPage < CM_DIR_EPP) &&
		   (curChunk + cm_NameEntries((cm_localMountPoints+curDirEntry)->namep, 0) <= CPP)) 
	{

		noChunks = cm_NameEntries((cm_localMountPoints+curDirEntry)->namep, 0);
		fakeEntry.fid.vnode = htonl(curDirEntry + 2);
		currentPos = cm_FakeRootDir + curPage * CM_DIR_PAGESIZE + curChunk * CM_DIR_CHUNKSIZE;

		memcpy(currentPos, &fakeEntry, CM_DIR_CHUNKSIZE);
		strcpy(currentPos + 12, (cm_localMountPoints+curDirEntry)->namep);
		curDirEntry++;
		curDirEntryInPage++;
		for (i=0; i<noChunks; i++) {
			t1 = (curChunk + i) / 8;
			t2 = curChunk + i - (t1*8);
			fakeDirHeader.header.freeBitmap[t1] |= (1 << t2);
		}
		curChunk+=noChunks;
	}

	// when we get here, we're done with filling in the entries for page 0
	// copy in the header info

	memcpy(cm_FakeRootDir, &fakeDirHeader, 13 * CM_DIR_CHUNKSIZE);

	curPage++;

	// ok, page 0's done. Move on to the next page.
	while (curDirEntry!=cm_noLocalMountPoints) {
		// setup a new page
		curChunk = 1;			// the zeroth chunk is reserved for page header
		curDirEntryInPage = 0; 
		for (i=0; i<8; i++) {
			fakePageHeader.freeBitmap[i]=0;
		}
		fakePageHeader.freeCount = 0;
		fakePageHeader.pgcount = 0;
		fakePageHeader.tag = htons(1234);
		
		// while we're on the same page...
		while ( (curDirEntry!=cm_noLocalMountPoints) &&
				(curDirEntryInPage < CM_DIR_EPP) &&
			    (curChunk + cm_NameEntries((cm_localMountPoints+curDirEntry)->namep, 0) <= CPP))
		{
			// add an entry to this page

			noChunks = cm_NameEntries((cm_localMountPoints+curDirEntry)->namep, 0);
			fakeEntry.fid.vnode=htonl(curDirEntry+2);
			currentPos = cm_FakeRootDir + curPage * CM_DIR_PAGESIZE + curChunk * CM_DIR_CHUNKSIZE;
			memcpy(currentPos, &fakeEntry, CM_DIR_CHUNKSIZE);
			strcpy(currentPos + 12, (cm_localMountPoints+curDirEntry)->namep);
			curDirEntry++;
			curDirEntryInPage++;
			for (i=0; i<noChunks; i++) {
				t1 = (curChunk + i) / 8;
				t2 = curChunk + i - (t1*8);
				fakePageHeader.freeBitmap[t1] |= (1 << t2);
			}
			curChunk+=noChunks;
		}
		memcpy(cm_FakeRootDir + curPage * CM_DIR_PAGESIZE, &fakePageHeader, sizeof(fakePageHeader));

		curPage++;
	}
	
	// we know the fakeDir is setup properly, so we claim that we have callback
	cm_fakeDirCallback=1;

	// when we get here, we've set up everything! done!


}

int cm_FakeRootFid(cm_fid_t *fidp)
{
  fidp->cell = 0x1;            /* root cell */
	fidp->volume = 0x20000001;   /* root.afs ? */
	fidp->vnode = 0x1;
	fidp->unique = 0x1;
}
  
int cm_getLocalMountPointChange() {
  return cm_localMountPointChangeFlag;
}

int cm_clearLocalMountPointChange() {
  cm_localMountPointChangeFlag = 0;
}

/* called directly from ioctl */
/* called while not holding freelance lock */
int cm_noteLocalMountPointChange() {
  lock_ObtainMutex(&cm_Freelance_Lock);
  cm_fakeDirVersion++;
  cm_localMountPointChangeFlag = 1;
  lock_ReleaseMutex(&cm_Freelance_Lock);
  return 1;
}

int cm_reInitLocalMountPoints() {
	cm_fid_t aFid;
	int i, j, hash;
	cm_scache_t *scp, **lscpp, *tscp;

	
	printf("\n\n----- reinitialization starts ----- \n");


	// first we invalidate all the SCPs that were created
	// for the local mount points

	printf("Invalidating local mount point scp...  ");

	aFid.cell = 0x1;
	aFid.volume=0x20000001;
	aFid.unique=0x1;
	aFid.vnode=0x2;

	lock_ObtainWrite(&cm_scacheLock);
	lock_ObtainMutex(&cm_Freelance_Lock);  /* always scache then freelance lock */
	for (i=0; i<cm_noLocalMountPoints; i++) {
		hash = CM_SCACHE_HASH(&aFid);
		for (scp=cm_hashTablep[hash]; scp; scp=scp->nextp) {
			if (scp->fid.volume == aFid.volume &&
				scp->fid.vnode == aFid.vnode &&
				scp->fid.unique == aFid.unique 
				) {

				// mark the scp to be reused
				lock_ReleaseWrite(&cm_scacheLock);
				lock_ObtainMutex(&scp->mx);
				cm_DiscardSCache(scp);
				lock_ReleaseMutex(&scp->mx);
				cm_CallbackNotifyChange(scp);
				lock_ObtainWrite(&cm_scacheLock);
				scp->refCount--;

				// take the scp out of the hash
				lscpp = &cm_hashTablep[hash];
				for (tscp=*lscpp; tscp; lscpp = &tscp->nextp, tscp = *lscpp) {
					if (tscp == scp) break;
				}
				*lscpp = scp->nextp;
				scp->flags &= ~CM_SCACHEFLAG_INHASH;


			}
		}
		aFid.vnode = aFid.vnode + 1;
	}
	lock_ReleaseWrite(&cm_scacheLock);
	printf("\tall old scp cleared!\n");

	// we must free the memory that was allocated in the prev
	// cm_InitLocalMountPoints call
	printf("Removing old localmountpoints...  ");
	free(cm_localMountPoints);
	printf("\tall old localmountpoints cleared!\n");

	// now re-init the localmountpoints
	printf("Creating new localmountpoints...  ");
	cm_InitLocalMountPoints();
	printf("\tcreated new set of localmountpoints!\n");
	
	
	// now we have to free the memory allocated in cm_initfakerootdir
	printf("Removing old fakedir...  ");
	free(cm_FakeRootDir);
	printf("\t\told fakedir removed!\n");

	// then we re-create that dir
	printf("Creating new fakedir...  ");
	cm_InitFakeRootDir();
	printf("\t\tcreated new fakedir!\n");	

	lock_ReleaseMutex(&cm_Freelance_Lock);

	printf("----- reinit complete -----\n\n");
}


// yj: open up the ini file and read all the local mount 
// points that are stored there. Part of the initialization
// process for the freelance client.
/* to be called while holding freelance lock unless during init. */
long cm_InitLocalMountPoints() {
	
	FILE *fp;
	char line[200];
	int n, i;
	char* t;
	cm_localMountPoint_t* aLocalMountPoint;
	char hdir[120];

	cm_GetConfigDir(hdir);
	strcat(hdir, AFS_FREELANCE_INI);
	// open the ini file for reading
	fp = fopen(hdir, "r");

	// if we fail to open the file, create an empty one
	if (!fp) {
	  fp = fopen(hdir, "w");
	  fputs("0\n", fp);
	  fclose(fp);
	  return 0;  /* success */
	}

	// we successfully opened the file
#ifdef DEBUG
	fprintf(stderr, "opened afs_freelance.ini\n");
#endif
	
	// now we read the first line to see how many entries
	// there are
	fgets(line, 200, fp);

	// if the line is empty at any point when we're reading
	// we're screwed. report error and return.
	if (*line==0) {
		afsi_log("error occurred while reading afs_freelance.ini");
		fprintf(stderr, "error occurred while reading afs_freelance.ini");
		return -1;
	}

	// get the number of entries there are from the first line
	// that we read
	cm_noLocalMountPoints = atoi(line);

	// create space to store the local mount points
	cm_localMountPoints = malloc(sizeof(cm_localMountPoint_t) * cm_noLocalMountPoints);
	aLocalMountPoint = cm_localMountPoints;
		
	// now we read n lines and parse them into local mount points
	// where n is the number of local mount points there are, as
	// determined above.
	// Each line in the ini file represents 1 local mount point and 
	// is in the format xxx#yyy:zzz, where xxx is the directory
	// entry name, yyy is the cell name and zzz is the volume name.
	// #yyy:zzz together make up the mount point.
	for (i=0; i<cm_noLocalMountPoints; i++) {
		fgets(line, 200, fp);
		// check that the line is not empty
		if (line[0]==0) {
			afsi_log("error occurred while parsing entry in %s: empty line in line %d", AFS_FREELANCE_INI, i);
			fprintf(stderr, "error occurred while parsing entry in afs_freelance.ini: empty line in line %d", i);
			return -1;
		}
		// line is not empty, so let's parse it
		t = strchr(line, '#');
		// make sure that there is a '#' separator in the line
		if (!t) {
			afsi_log("error occurred while parsing entry in %s: no # separator in line %d", AFS_FREELANCE_INI, i);
			fprintf(stderr, "error occurred while parsing entry in afs_freelance.ini: no # separator in line %d", i);
			return -1;
		}
		aLocalMountPoint->namep=malloc(t-line+1);
		memcpy(aLocalMountPoint->namep, line, t-line);
		*(aLocalMountPoint->namep + (t-line)) = 0;
		aLocalMountPoint->mountPointStringp=malloc(strlen(line) - (t-line) + 1);
		memcpy(aLocalMountPoint->mountPointStringp, t, strlen(line)-(t-line)-2);
		*(aLocalMountPoint->mountPointStringp + (strlen(line)-(t-line)-2)) = 0;
#ifdef DEBUG
		fprintf(stderr, "found mount point: name %s, string %s\n",
			aLocalMountPoint->namep,
			aLocalMountPoint->mountPointStringp);
#endif
		
		aLocalMountPoint++;

	}
	fclose(fp);
	return 0;
}


int cm_getNoLocalMountPoints() {
	return cm_noLocalMountPoints;
}

cm_localMountPoint_t* cm_getLocalMountPoint(int vnode) {
	return 0;
}

long cm_FreelanceAddMount(char *filename, char *cellname, char *volume, cm_fid_t *fidp)
{
    FILE *fp;
    char hfile[120];
    char line[200];
    char fullname[200];
    int n;

    /* before adding, verify the cell name; if it is not a valid cell,
       don't add the mount point */
    /* major performance issue? */
    if (!cm_GetCell_Gen(cellname, fullname, CM_FLAG_CREATE))
      return -1;
#if 0
    if (strcmp(cellname, fullname) != 0)   /* no partial matches allowed */
      return -1;
#endif
    
    lock_ObtainMutex(&cm_Freelance_Lock);

     cm_GetConfigDir(hfile);
     strcat(hfile, AFS_FREELANCE_INI);
     fp = fopen(hfile, "r+");
     if (!fp)
       return CM_ERROR_INVAL;
     fgets(line, 200, fp);
     n = atoi(line);
     n++;
     fseek(fp, 0, SEEK_SET);
     fprintf(fp, "%d", n);
     fseek(fp, 0, SEEK_END);
     fprintf(fp, "%s#%s:%s\n", filename, fullname, volume);
     fclose(fp);
     lock_ReleaseMutex(&cm_Freelance_Lock);

     /*cm_reInitLocalMountPoints(&vnode);*/
     if (fidp) {
       fidp->unique = 1;
       fidp->vnode = cm_noLocalMountPoints + 1;   /* vnode value of last mt pt */
     }
     cm_noteLocalMountPointChange();
     
     return 0;
}

long cm_FreelanceRemoveMount(char *toremove)
{
     int i, n, t1, t2;
     char* cp;
     char line[200];
     char shortname[200];
     char hfile[120], hfile2[120];
     FILE *fp1, *fp2;
     char cmd[200];
     int found=0;

    lock_ObtainMutex(&cm_Freelance_Lock);

     cm_GetConfigDir(hfile);
     strcat(hfile, AFS_FREELANCE_INI);
     strcpy(hfile2, hfile);
     strcat(hfile2, "2");
     fp1=fopen(hfile, "r+");
     if (!fp1)
       return CM_ERROR_INVAL;
     fp2=fopen(hfile2, "w+");
     if (!fp2) {
       fclose(fp1);
       return CM_ERROR_INVAL;
     }

     fgets(line, 200, fp1);
     n=atoi(line);
     fprintf(fp2, "%d\n", n-1);

     for (i=0; i<n; i++) {
          fgets(line, 200, fp1);
          cp=strchr(line, '#');
          memcpy(shortname, line, cp-line);
          shortname[cp-line]=0;

          if (strcmp(shortname, toremove)==0) {

          } else {
	    found = 1;
	    fputs(line, fp2);
          }
     }

     fclose(fp1);
     fclose(fp2);
     if (!found)
       return CM_ERROR_NOSUCHFILE;

     unlink(hfile);
     rename(hfile2, hfile);

     lock_ReleaseMutex(&cm_Freelance_Lock);

     cm_noteLocalMountPointChange();
     return 0;
}

#endif /* AFS_FREELANCE_CLIENT */
