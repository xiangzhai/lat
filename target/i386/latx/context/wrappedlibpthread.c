#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sys/syscall.h>

#include "wrappedlibs.h"

#include "debug.h"
#include "wrapper.h"
#include "bridge.h"
#include "library_private.h"
#include "box64context.h"
#include "librarian.h"
#include "myalign.h"

const char* libpthreadName = "libpthread.so.0";
#define ALTNAME "libc.so"

#define LIBNAME libpthread

#define SUPER() \
GO(0)			\
GO(1)			\
GO(2)			\
GO(3)			\
GO(4)			\
GO(5)			\
GO(6)			\
GO(7)			\
GO(8)			\
GO(9)			\
GO(10)			\
GO(11)			\
GO(12)			\
GO(13)			\
GO(14)			\
GO(15)			\
GO(16)			\
GO(17)			\
GO(18)			\
GO(19)			\
GO(20)			\
GO(21)			\
GO(22)			\
GO(23)			\
GO(24)			\
GO(25)			\
GO(26)			\
GO(27)			\
GO(28)			\
GO(29)

// key_dtor
#define GO(A)   \
static uintptr_t my_key_dtor_fct_##A = 0;  		\
static void my_key_dtor_##A(void* a)    		\
{                                       		\
    RunFunctionWithState(my_key_dtor_fct_##A, 1, a);	\
}
SUPER()
#undef GO
static void* findkey_dtorFct(void* fct)
{
    if(!fct) return fct;
    if(GetNativeFnc((uintptr_t)fct))  return GetNativeFnc((uintptr_t)fct);
    #define GO(A) if(my_key_dtor_fct_##A == (uintptr_t)fct) return my_key_dtor_##A;
    SUPER()
    #undef GO
    #define GO(A) if(my_key_dtor_fct_##A == 0) {my_key_dtor_fct_##A = (uintptr_t)fct; return my_key_dtor_##A; }
    SUPER()
    #undef GO
    printf_log(LOG_NONE, "Warning, no more slot for pthread key_dtor callback\n");
    return NULL;
}

#undef SUPER

EXPORT int my_pthread_key_create(pthread_key_t* key, void* dtor);
EXPORT int my_pthread_key_create(pthread_key_t* key, void* dtor)
{
    int ret = pthread_key_create(key, findkey_dtorFct(dtor));
    return ret;
}
EXPORT int my___pthread_key_create(pthread_key_t* key, void* dtor) __attribute__((alias("my_pthread_key_create")));

#include "wrappedlib_init.h"
