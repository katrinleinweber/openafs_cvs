/* Copyright (C) 1998 Transarc Corporation - All rights reserved.
 *
 */

/*
 * This file implements the pts related funtions for afscp
 */

#include <afs/param.h>
#include <afs/stds.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>

#include <afs/afs_Admin.h>
#include <afs/afs_ptsAdmin.h>
#include <afs/afs_utilAdmin.h>

#include <afs/cellconfig.h>
#include <afs/cmd.h>
#include "common.h"

void
SetupPtsAdminCmd(void);
