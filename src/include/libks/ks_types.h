/*
 * Copyright (c) 2018 SignalWire, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _KS_TYPES_H_
#define _KS_TYPES_H_

#include "ks.h"

KS_BEGIN_EXTERN_C

#define KS_STR2ENUM_P(_FUNC1, _FUNC2, _TYPE) KS_DECLARE(_TYPE) _FUNC1 (const char *name); KS_DECLARE(const char *) _FUNC2 (_TYPE type);

#define KS_STR2ENUM(_FUNC1, _FUNC2, _TYPE, _STRINGS, _MAX)  \
    KS_DECLARE(_TYPE) _FUNC1 (const char *name)             \
    {                                                       \
        int i;                                              \
        _TYPE t = _MAX ;                                    \
                                                            \
        for (i = 0; i < _MAX ; i++) {                       \
            if (!strcasecmp(name, _STRINGS[i])) {           \
                t = (_TYPE) i;                              \
                break;                                      \
            }                                               \
        }                                                   \
                                                            \
        return t;                                           \
    }                                                       \
    KS_DECLARE(const char *) _FUNC2 (_TYPE type)            \
    {                                                       \
        if (type > _MAX) {                                  \
            type = _MAX;                                    \
        }                                                   \
        return _STRINGS[(int)type];                         \
    }                                                       \

#define KS_ENUM_NAMES(_NAME, _STRINGS) static const char * _NAME [] = { _STRINGS , NULL };

#define KS_VA_NONE "%s", ""

	typedef enum {
		KS_POLL_READ = (1 << 0),
		KS_POLL_WRITE = (1 << 1),
		KS_POLL_ERROR = (1 << 2)
	} ks_poll_t;

#if defined(_MSC_VER)
#include <BaseTsd.h>
	typedef SSIZE_T ssize_t;
#endif

	typedef uint16_t ks_port_t;
	typedef size_t ks_size_t;
	typedef unsigned char ks_byte_t;
	typedef enum {
		KS_STATUS_SUCCESS,
		KS_STATUS_END,						/* End of some kind of enumeration */
		KS_STATUS_FAIL,
		KS_STATUS_BREAK,
		KS_STATUS_DISCONNECTED,
		KS_STATUS_GENERR,
		KS_STATUS_INVALID_ARGUMENT,
		KS_STATUS_INACTIVE,
		KS_STATUS_TIMEOUT,
		KS_STATUS_DUPLICATE_OPERATION,
		KS_STATUS_THREAD_STOP_REQUESTED,	/* A thread exited due to the fact that the caller requested it to stop */
		KS_STATUS_THREAD_ALREADY_STOPPED,	/* A call to request stop has already been made */
		KS_STATUS_THREAD_ALREADY_JOINED,	/* A call to join has already been made */
		KS_STATUS_POOL_PREFIX_LOCKED,		/* Freeing of the pool block was not allowed due to a lock on the prefix (typically ks_thread_t) */
		/* Memory pool errors */
		KS_STATUS_REFS_EXIST,      /* references exist */
		KS_STATUS_ARG_NULL,        /* function argument is null */
		KS_STATUS_ARG_INVALID,     /* function argument is invalid */
		KS_STATUS_PNT,	           /* invalid ks_pool pointer */
		KS_STATUS_POOL_OVER,	   /* ks_pool structure was overwritten */
		KS_STATUS_PAGE_SIZE,	   /* could not get system page-size */
		KS_STATUS_OPEN_ZERO,	   /* could not open /dev/zero */
		KS_STATUS_NO_MEM,	       /* no memory available */
		KS_STATUS_SIZE,	           /* error processing requested size */
		KS_STATUS_TOO_BIG,	       /* allocation exceeded max size */
		KS_STATUS_MEM,	           /* invalid memory address */
		KS_STATUS_MEM_OVER,	       /* memory lower bounds overwritten */
		KS_STATUS_NOT_FOUND,	   /* memory block not found in pool */
		KS_STATUS_IS_FREE,	       /* memory block already free */
		KS_STATUS_BLOCK_STAT,      /* invalid internal block status */
		KS_STATUS_FREE_ADDR,	   /* invalid internal free address */
		KS_STATUS_NO_PAGES,	       /* ran out of pages in pool */
		KS_STATUS_ALLOC,	       /* calloc,malloc,free,realloc failed */
		KS_STATUS_PNT_OVER,	       /* pointer structure was overwritten */
		KS_STATUS_INVALID_POINTER, /* address is not valid */
		KS_STATUS_NOT_ALLOWED,     /* operation is not allowed */
		/* handle errors */
		KS_STATUS_HANDLE_INVALID,				/* An invalid handle was used */
		KS_STATUS_HANDLE_READY,					/* A slot that was ready (when we expected it to be not ready) was used */
		KS_STATUS_HANDLE_SEQ_MISMATCH,			/* The sequence id in the handle did not match the slot */
		KS_STATUS_HANDLE_TYPE_MISMATCH,			/* The type of the handle did not match the actual handles encoded type */
		KS_STATUS_HANDLE_NOMEM,					/* Allocation of a handle slot failed */
		KS_STATUS_HANDLE_NO_MORE_SLOTS,			/* Max slots reached in a group (too many handles) */
		KS_STATUS_HANDLE_PARENT_ALREADY_SET, 	/* An attempt to set a handles parent failed since one was already set */
		KS_STATUS_HANDLE_INVALID_REQUEST,       /* An internal operation failed */
		KS_STATUS_HANDLE_PENDING_CHILDREN,		/* A destroy failed due to children having refcounts */
		/* Always insert new entries above this line*/
		KS_STATUS_COUNT
	} ks_status_t;

#define STATUS_STRINGS\
	"SUCCESS",\
	"FAIL",\
	"BREAK",\
	"DISCONNECTED",\
	"GENERR",\
	"INACTIVE",\
	"TIMEOUT",\
	"DUPLICATE_OPERATION",\
	"ARG_NULL",\
	"ARG_INVALID",\
	"PNT",\
	"POOL_OVER",\
	"PAGE_SIZE",\
	"OPEN_ZERO",\
	"NO_MEM",\
	"SIZE",\
	"TOO_BIG",\
	"MEM",\
	"MEM_OVER",\
	"NOT_FOUN",\
	"IS_FREE",\
	"BLOCK_STAT",\
	"FREE_ADDR",\
	"NO_PAGES",\
	"ALLOC",\
	"PNT_OVER",\
	"INVALID_POINTER",\
	/* insert new entries before this */\
	"COUNT"

	KS_STR2ENUM_P(ks_str2ks_status, ks_status2str, ks_status_t)

/*! \brief Used internally for truth test */
	typedef enum {
		KS_TRUE = 1,
		KS_FALSE = 0
	} ks_bool_t;

#ifdef KS_PLAT_WIN
	#include <Rpc.h>
	typedef UUID ks_uuid_t;
#else
	#include <uuid/uuid.h>

	/* Use a structure rather then uuids char array, that way
	 * we can return it by value*/
	typedef struct ks_uuid_s {
		unsigned long  Data1;
		unsigned short Data2;
		unsigned short Data3;
		unsigned char  Data4[ 8 ];
	} ks_uuid_t;
#endif

#ifndef __FUNCTION__
#define __FUNCTION__ (const char *)__func__
#endif

#define KS_PRE __FILE__, __FUNCTION__, __LINE__
#define KS_LOG_LEVEL_DEBUG 7
#define KS_LOG_LEVEL_INFO 6
#define KS_LOG_LEVEL_NOTICE 5
#define KS_LOG_LEVEL_WARNING 4
#define KS_LOG_LEVEL_ERROR 3
#define KS_LOG_LEVEL_CRIT 2
#define KS_LOG_LEVEL_ALERT 1
#define KS_LOG_LEVEL_EMERG 0

#define KS_LOG_DEBUG KS_PRE, KS_LOG_LEVEL_DEBUG
#define KS_LOG_INFO KS_PRE, KS_LOG_LEVEL_INFO
#define KS_LOG_NOTICE KS_PRE, KS_LOG_LEVEL_NOTICE
#define KS_LOG_WARNING KS_PRE, KS_LOG_LEVEL_WARNING
#define KS_LOG_ERROR KS_PRE, KS_LOG_LEVEL_ERROR
#define KS_LOG_CRIT KS_PRE, KS_LOG_LEVEL_CRIT
#define KS_LOG_ALERT KS_PRE, KS_LOG_LEVEL_ALERT
#define KS_LOG_EMERG KS_PRE, KS_LOG_LEVEL_EMERG

typedef enum {
	KS_LOG_PREFIX_NONE = 0,

	KS_LOG_PREFIX_LEVEL = 1 << 0,
	KS_LOG_PREFIX_FILE = 1 << 1,
	KS_LOG_PREFIX_LINE = 1 << 2,
	KS_LOG_PREFIX_FUNC = 1 << 3,
	KS_LOG_PREFIX_THREAD = 1 << 4,
	KS_LOG_PREFIX_DATE = 1 << 5,
	KS_LOG_PREFIX_TIME = 1 << 6,

	KS_LOG_PREFIX_ALL = KS_LOG_PREFIX_LEVEL | KS_LOG_PREFIX_FILE | KS_LOG_PREFIX_LINE | KS_LOG_PREFIX_FUNC | KS_LOG_PREFIX_THREAD | KS_LOG_PREFIX_DATE | KS_LOG_PREFIX_TIME,
	KS_LOG_PREFIX_DEFAULT = KS_LOG_PREFIX_ALL ^ KS_LOG_PREFIX_DATE,
} ks_log_prefix_t;

struct ks_pool_s;

typedef struct ks_pool_s ks_pool_t;
typedef void (*ks_hash_destructor_t)(void *ptr);

typedef enum {
	KS_MPCL_ANNOUNCE,
	KS_MPCL_TEARDOWN,
	KS_MPCL_DESTROY
} ks_pool_cleanup_action_t;

typedef enum {
	KS_MPCL_FREE,
	KS_MPCL_GLOBAL_FREE,
} ks_pool_cleanup_type_t;

typedef union {
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
} ks_sockaddr_in_t;

typedef struct {
	int family;
	ks_sockaddr_in_t v;
	ks_port_t port;
	char host[48];
} ks_sockaddr_t;

typedef void (*ks_pool_cleanup_callback_t)(void *ptr, void *arg, ks_pool_cleanup_action_t action, ks_pool_cleanup_type_t type);

typedef void (*ks_logger_t) (const char *file, const char *func, int line, int level, const char *fmt, ...);
typedef void (*ks_listen_callback_t) (ks_socket_t server_sock, ks_socket_t client_sock, ks_sockaddr_t *addr, void *user_data);

typedef int64_t ks_time_t;

struct ks_q_s;
typedef struct ks_q_s ks_q_t;
typedef void (*ks_flush_fn_t)(ks_q_t *q, void *ptr, void *flush_data);

typedef struct ks_thread_pool_s ks_thread_pool_t;

struct ks_network_list;
typedef struct ks_network_list ks_network_list_t;

typedef enum {
	KS_FAKTORY_STATE_NONE = 0,
	KS_FAKTORY_STATE_CONNECTING,
	KS_FAKTORY_STATE_UNIDENTIFIED,
	KS_FAKTORY_STATE_IDENTIFIED,
	KS_FAKTORY_STATE_QUIET,
	KS_FAKTORY_STATE_TERMINATE,
	KS_FAKTORY_STATE_END,
	KS_FAKTORY_STATE_SHUTDOWN,
	KS_FAKTORY_STATE_CLEANUP
} ks_faktory_state_t;

struct ks_faktory_s;
typedef struct ks_faktory_s ks_faktory_t;

struct respObject;
typedef struct respObject respObject;
typedef void(*ks_faktory_state_callback_t) (ks_faktory_t *faktory, ks_faktory_state_t state);
typedef void (*ks_faktory_command_callback_t) (ks_faktory_t *faktory, respObject *resp, void *data);

KS_END_EXTERN_C

#endif							/* defined(_KS_TYPES_H_) */

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
