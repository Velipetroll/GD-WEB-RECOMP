#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>

// Android 4.1 (API 16) Bionic libc compatibility shims:
// 1. __vsnprintf_chk, __strlen_chk (added in API 17)
// 2. __open_2 (added in API 17)
// 3. getauxval (added in API 18)
// 4. statvfs, fstatvfs (added in API 19)
// 5. abs, labs (were inline in API 16 headers, not in libc.so)

__attribute__((visibility("default")))
int __vsnprintf_chk(char *dest, size_t maxlen, int flag, size_t slen, const char *format, va_list ap) {
    (void)flag;
    (void)slen;
    return vsnprintf(dest, maxlen, format, ap);
}

__attribute__((visibility("default")))
size_t __strlen_chk(const char *s, size_t maxlen) {
    (void)maxlen;
    return strlen(s);
}

__attribute__((visibility("default")))
int __open_2(const char *pathname, int flags) {
    return open(pathname, flags);
}

__attribute__((visibility("default")))
unsigned long getauxval(unsigned long type) {
    (void)type;
    return 0;
}

__attribute__((visibility("default")))
int abs(int x) {
    return x < 0 ? -x : x;
}

__attribute__((visibility("default")))
long labs(long x) {
    return x < 0 ? -x : x;
}

static void compat_statfs_to_statvfs(const struct statfs *src, struct statvfs *dst) {
    dst->f_bsize = src->f_bsize;
    dst->f_frsize = src->f_bsize;
    dst->f_blocks = src->f_blocks;
    dst->f_bfree = src->f_bfree;
    dst->f_bavail = src->f_bavail;
    dst->f_files = src->f_files;
    dst->f_ffree = src->f_ffree;
    dst->f_favail = src->f_ffree;
    dst->f_fsid = src->f_fsid.__val[0];
    dst->f_flag = src->f_flags;
    dst->f_namemax = src->f_namelen;
}

__attribute__((visibility("default")))
int statvfs(const char *path, struct statvfs *buf) {
    struct statfs tmp;
    int rc = statfs(path, &tmp);
    if (rc != 0) return rc;
    compat_statfs_to_statvfs(&tmp, buf);
    return 0;
}

__attribute__((visibility("default")))
int fstatvfs(int fd, struct statvfs *buf) {
    struct statfs tmp;
    int rc = fstatfs(fd, &tmp);
    if (rc != 0) return rc;
    compat_statfs_to_statvfs(&tmp, buf);
    return 0;
}
