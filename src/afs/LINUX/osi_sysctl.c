/*
 * osi_sysctl.c: Linux sysctl interface to OpenAFS
 *
 * $Id: osi_sysctl.c,v 1.1 2002/02/07 00:07:18 shadow Exp $
 *
 * Written Jan 30, 2002 by Kris Van Hees (Sine Nomine Associates)
 */

#include <linux/sysctl.h>
#include <linux/config.h>

#include <afsconfig.h>
#include "../afs/param.h"

#include "../afs/sysincludes.h"	/* Standard vendor system headers */
#include "../afs/afsincludes.h"	/* Afs-based standard headers */
#include "../afs/afs_stats.h"   /* afs statistics */

/* From afs_analyze.c */
extern afs_int32 hm_retry_RO;
extern afs_int32 hm_retry_RW;
extern afs_int32 hm_retry_int;

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *afs_sysctl = NULL;

static ctl_table afs_sysctl_table[] = {
        {1, "hm_retry_RO",
         &hm_retry_RO, sizeof(afs_int32), 0644, NULL,
         &proc_dointvec},
        {2, "hm_retry_RW",
         &hm_retry_RW, sizeof(afs_int32), 0644, NULL,
         &proc_dointvec},
        {3, "hm_retry_int",
         &hm_retry_int, sizeof(afs_int32), 0644, NULL,
         &proc_dointvec},
	{0}
};

static ctl_table fs_sysctl_table[] = {
	{1, "afs", NULL, 0, 0555, afs_sysctl_table},
	{0}
};

int osi_sysctl_init()
{
	afs_sysctl = register_sysctl_table(fs_sysctl_table, 0);
	if (!afs_sysctl)
		return -1;

	return 0;
}

void osi_sysctl_clean()
{
	if (afs_sysctl) {
		unregister_sysctl_table(afs_sysctl);
		afs_sysctl = NULL;
	}
}

#endif /* CONFIG_SYSCTL */
