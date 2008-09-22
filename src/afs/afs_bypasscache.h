/*
 * COPYRIGHT  �  2000
 * THE REGENTS OF THE UNIVERSITY OF MICHIGAN
 * ALL RIGHTS RESERVED
 * 
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 * 
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY O 
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */
 
 /*
 * Portions Copyright (c) 2008
 * The Linux Box Corporation
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of the Linux Box
 * Corporation is not used in any advertising or publicity
 * pertaining to the use or distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * Linux Box Corporation is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * This software is provided as is, without representation
 * from the Linux Box Corporation as to its fitness for any
 * purpose, and without warranty by the Linux Box Corporation
 * of any kind, either express or implied, including
 * without limitation the implied warranties of
 * merchantability and fitness for a particular purpose.  The
 * Linux Box Corporation shall not be liable for any damages, 
 * including special, indirect, incidental, or consequential 
 * damages, with respect to any claim arising out of or in 
 * connection with the use of the software, even if it has been 
 * or is hereafter advised of the possibility of such damages.
 */


#ifndef _AFS_BYPASSCACHE_H
#define _AFS_BYPASSCACHE_H

#if defined(AFS_CACHE_BYPASS)

#include <afsconfig.h>
#include "afs/param.h"
#include "afs/sysincludes.h"
#include "afsincludes.h"

#define AFS_CACHE_BYPASS_DISABLED -1

#ifdef UKERNEL
typedef struct uio uio_t;
#ifndef PAGE_SIZE
#define PAGE_SIZE 1024 * sizeof(long) / 8
#endif
#endif

/* A ptr to an object of the following type is expected to be passed
 * as the ab->parm[0] to afs_BQueue */
struct nocache_read_request {
    /* Why can't we all get along? */
#if defined(AFS_SUN5_ENV)
    /* SOLARIS */
    u_offset_t offset;
    struct seg *segment;
    caddr_t address;
#elif defined(AFS_SGI_ENV)
    /* SGI (of some vintage) */
    int32 offset;
    int32 rem;
    int32 pmp; /* mmm */
    int32 length;
#elif defined(AFS_LINUX24_ENV) || defined(AFS_USR_LINUX24_ENV)
    /* The tested platform, as CITI impl. just packs ab->parms */
    uio_t * auio;
    struct vrequest *areq;
    afs_size_t offset;
    afs_size_t length;
#endif
};

enum cache_bypass_strategies
{
    ALWAYS_BYPASS_CACHE,
    NEVER_BYPASS_CACHE,
    LARGE_FILES_BYPASS_CACHE
};

extern int cache_bypass_prefetch;
extern int cache_bypass_strategy;
extern int cache_bypass_threshold;

void afs_TransitionToBypass(register struct vcache *, register struct AFS_UCRED *, int);
void afs_TransitionToCaching(register struct vcache *, register struct AFS_UCRED *, int);

/* Cache strategy permits vnode transition between caching and no-cache--
 * currently, this means LARGE_FILES_BYPASS_CACHE.  Currently, no pioctl permits
 * setting FCSBypass manually for a vnode */
#define variable_cache_strategy					\
    (! ((cache_bypass_strategy == ALWAYS_BYPASS_CACHE) ||	\
	(cache_bypass_strategy == NEVER_BYPASS_CACHE)) )

/* Cache-coherently toggle cache/no-cache for a vnode */
#define trydo_cache_transition(avc, credp, bypasscache)			\
    do {								\
	if(variable_cache_strategy) {					\
	    if(bypasscache) {						\
		if(!(avc->cachingStates & FCSBypass))			\
		    afs_TransitionToBypass(avc, credp, TRANSChangeDesiredBit); \
	    } else {							\
		if(avc->cachingStates & FCSBypass)			\
		    afs_TransitionToCaching(avc, credp, TRANSChangeDesiredBit);	\
	    }								\
	}								\
    }									\
    while(0);

/* dispatch a no-cache read request */
afs_int32
afs_ReadNoCache(register struct vcache *avc, register struct nocache_read_request *bparms, 
		struct AFS_UCRED *acred);

/* no-cache prefetch routine */
afs_int32
afs_PrefetchNoCache(register struct vcache *avc, register struct AFS_UCRED *acred,
			struct nocache_read_request *bparms);


#endif /* AFS_CACHE_BYPASS */
#endif /* _AFS_BYPASSCACHE_H */

