@BOTTOM@
#undef PACKAGE
#undef VERSION
#define RCSID(msg) \
static /**/const char *const rcsid[] = { (char *)rcsid, "\100(#)" msg }
#undef HAVE_CONNECT
#undef HAVE_GETHOSTBYNAME
#undef HAVE_RES_SEARCH
#undef HAVE_SOCKET
#undef STRUCT_SOCKADDR_HAS_SA_LEN
#if ENDIANESS_IN_SYS_PARAM_H
# ifndef KERNEL
#  include <sys/types.h>
#  include <sys/param.h>
#  if BYTE_ORDER == BIG_ENDIAN
#  define WORDS_BIGENDIAN 1
#  endif
# endif
#endif
#undef AFS_AFSDB_ENV
#undef AFS_NAMEI_ENV
#undef BITMAP_LATER
#undef BOS_RESTRICTED_MODE
#undef BOS_NEW_CONFIG
#undef FAST_RESTART
#undef FULL_LISTVOL_SWITCH
#undef COMPLETION_H_EXISTS
#undef DEFINED_FOR_EACH_PROCESS
#undef DEFINED_PREV_TASK
#undef EXPORTED_KALLSYMS_ADDRESS
#undef EXPORTED_KALLSYMS_SYMBOL
#undef EXPORTED_SYS_CALL_TABLE
#undef EXPORTED_IA32_SYS_CALL_TABLE
#undef EXPORTED_TASKLIST_LOCK
#undef INODE_SETATTR_NOT_VOID
#undef RECALC_SIGPENDING_TAKES_VOID
#undef STRUCT_ADDRESS_SPACE_HAS_GFP_MASK
#undef STRUCT_ADDRESS_SPACE_HAS_PAGE_LOCK
#undef STRUCT_FS_HAS_FS_ROLLED
#undef STRUCT_INODE_HAS_I_DEVICES
#undef STRUCT_INODE_HAS_I_DIRTY_DATA_BUFFERS
#undef STRUCT_INODE_HAS_I_ALLOC_SEM
#undef STRUCT_INODE_HAS_I_TRUNCATE_SEM
#undef STRUCT_TASK_STRUCT_HAS_PARENT
#undef STRUCT_TASK_STRUCT_HAS_REAL_PARENT
#undef STRUCT_TASK_STRUCT_HAS_SIG
#undef STRUCT_TASK_STRUCT_HAS_SIGHAND
#undef STRUCT_TASK_STRUCT_HAS_SIGMASK_LOCK
#undef ssize_t
#undef HAVE_STRUCT_BUF
/* glue for RedHat kernel bug */
#undef ENABLE_REDHAT_BUILDSYS
#if defined(ENABLE_REDHAT_BUILDSYS) && defined(KERNEL) && defined(REDHAT_FIX)
#include "redhat-fix.h"
#endif
