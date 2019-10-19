#ifndef LOGLOG_DSCAO__
#define LOGLOG_DSCAO__
#include <stdarg.h>
#include <syslog.h>

#ifdef LOGLOG
#define loginfo(level, fmt, ap)			\
	do {					\
		vsyslog(level, fmt, ap);	\
	} while (0)				
#else
#define loginfo(level, fmt, ap)			\
	do {					\
		vfprintf(stderr, fmt, ap);	\
	} while (0)
#endif

static inline void logmsg(int level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	loginfo(level, fmt, ap);
	va_end(ap);
}
#endif  /* LOGLOG_DSCAO__ */
