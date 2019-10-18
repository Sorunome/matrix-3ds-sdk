#ifndef _UTIL_H_
#define _UTIL_H_

#include <string>
#include <3ds.h>

std::string urlencode(std::string str);

Result httpcDownloadDataTimeout(httpcContext *context, u8* buffer, u32 size, u32 *downloadedsize, u64 timeout);

#endif // _UTIL_H_
