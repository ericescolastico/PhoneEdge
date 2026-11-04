#ifndef SC_COMPAT_H
#define SC_COMPAT_H

#include "config.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


#ifndef _WIN32
# define PRIu64_ PRIu64
# define SC_PRIsizet "zu"
#else
# define PRIu64_ "I64u"  // Windows...
# define SC_PRIsizet "Iu"
#endif

#ifndef HAVE_STRDUP
char *strdup(const char *s);
#endif

#ifndef HAVE_ASPRINTF
int asprintf(char **strp, const char *fmt, ...);
#endif

#ifndef HAVE_VASPRINTF
int vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#ifndef HAVE_NRAND48
long nrand48(unsigned short xsubi[3]);
#endif

#ifndef HAVE_JRAND48
long jrand48(unsigned short xsubi[3]);
#endif

#ifndef HAVE_REALLOCARRAY
void *reallocarray(void *ptr, size_t nmemb, size_t size);
#endif

#endif
