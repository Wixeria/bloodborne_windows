/* UTF-8 host paths and 64-bit file positions shared by save metadata and blobs. */
#define _GNU_SOURCE
#include "runtime_host_fs.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
struct RuntimeHostDirectory { HANDLE search; WIN32_FIND_DATAW data; int first; RuntimeHostEntry entry; };
static int wide(const char *path,wchar_t out[2048]) {
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,out,2048)) { errno=EINVAL; return -1; }
    return 0;
}
RuntimeHostDirectory *runtime_host_opendir(const char *path) {
    wchar_t p[2048],pattern[2052]; if (wide(path,p)) return NULL;
    swprintf(pattern,2052,L"%ls/*",p);
    RuntimeHostDirectory *d=calloc(1,sizeof(*d)); if (!d) return NULL;
    d->search=FindFirstFileW(pattern,&d->data); d->first=1;
    if (d->search==INVALID_HANDLE_VALUE) { free(d); errno=ENOENT; return NULL; }
    return d;
}
const RuntimeHostEntry *runtime_host_readdir(RuntimeHostDirectory *d) {
    if (!d->first && !FindNextFileW(d->search,&d->data)) return NULL;
    d->first=0;
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,d->data.cFileName,-1,d->entry.name,sizeof(d->entry.name),NULL,NULL)) return NULL;
    d->entry.directory=!!(d->data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    return &d->entry;
}
void runtime_host_closedir(RuntimeHostDirectory *d) { if (d) { FindClose(d->search); free(d); } }
int runtime_host_stat(const char *path,RuntimeHostStat *out) {
    wchar_t p[2048]; if (wide(path,p)) return -1;
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(p,GetFileExInfoStandard,&info)) { errno=ENOENT; return -1; }
    out->directory=!!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    uint64_t ticks=((uint64_t)info.ftLastWriteTime.dwHighDateTime<<32)|info.ftLastWriteTime.dwLowDateTime;
    out->mtime=((int64_t)ticks-INT64_C(116444736000000000))/10000000;
    return 0;
}
int runtime_host_mkdir(const char *path,int mode) {
    (void)mode; wchar_t p[2048]; if (wide(path,p)) return -1;
    if (CreateDirectoryW(p,NULL)) return 0;
    errno=GetLastError()==ERROR_ALREADY_EXISTS ? EEXIST : EACCES; return -1;
}
FILE *runtime_host_fopen(const char *path,const char *mode) {
    wchar_t p[2048],m[32]; if (wide(path,p)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,mode,-1,m,32)) { errno=EINVAL; return NULL; }
    return _wfopen(p,m);
}
int runtime_host_flush(FILE *file) { return fflush(file) || _commit(_fileno(file)) ? -1 : 0; }
int runtime_host_replace(const char *from,const char *to) {
    wchar_t a[2048],b[2048]; if (wide(from,a) || wide(to,b)) return -1;
    if (MoveFileExW(a,b,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return 0;
    errno=EACCES; return -1;
}
int runtime_host_unlink(const char *path) {
    wchar_t p[2048]; if (wide(path,p)) return -1;
    return _wunlink(p);
}
static int remove_wide(const wchar_t *p,unsigned depth) {
    if (depth>128) { errno=ELOOP; return -1; }
    DWORD attributes=GetFileAttributesW(p);
    if (attributes==INVALID_FILE_ATTRIBUTES) { errno=ENOENT; return -1; }
    /* Reparse points are removed themselves, never followed into another tree. */
    if ((attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        wchar_t pattern[2052]; swprintf(pattern,2052,L"%ls/*",p);
        WIN32_FIND_DATAW data; HANDLE h=FindFirstFileW(pattern,&data);
        if (h!=INVALID_HANDLE_VALUE) {
            int result=0;
            do {
                if (!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
                wchar_t child[2048]; int n=swprintf(child,2048,L"%ls/%ls",p,data.cFileName);
                if (n<0 || n>=2048 || remove_wide(child,depth+1)) { result=-1; break; }
            } while (FindNextFileW(h,&data));
            DWORD last=GetLastError(); FindClose(h);
            if (result || last!=ERROR_NO_MORE_FILES) { errno=EIO; return -1; }
        } else if (GetLastError()!=ERROR_FILE_NOT_FOUND) { errno=EACCES; return -1; }
    }
    BOOL ok=(attributes&FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(p) : DeleteFileW(p);
    if (!ok) { errno=EACCES; return -1; } return 0;
}
int runtime_host_remove_tree(const char *path) { wchar_t p[2048]; return wide(path,p) ? -1 : remove_wide(p,0); }
int runtime_host_resize(FILE *file,uint64_t size) { if (size>INT64_MAX) { errno=EINVAL; return -1; } return _chsize_s(_fileno(file),size) ? -1 : 0; }
int runtime_host_seek(FILE *file,int64_t offset,int whence) { return _fseeki64(file,offset,whence); }
int64_t runtime_host_tell(FILE *file) { return _ftelli64(file); }
#else
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>
struct RuntimeHostDirectory { DIR *host; char *path; RuntimeHostEntry entry; };
RuntimeHostDirectory *runtime_host_opendir(const char *path) {
    DIR *host=opendir(path); if (!host) return NULL;
    RuntimeHostDirectory *d=calloc(1,sizeof(*d));
    if (!d) { closedir(host); return NULL; }
    d->host=host; d->path=strdup(path); if (!d->path) { runtime_host_closedir(d); return NULL; } return d;
}
const RuntimeHostEntry *runtime_host_readdir(RuntimeHostDirectory *d) {
    struct dirent *e=readdir(d->host); if (!e) return NULL;
    snprintf(d->entry.name,sizeof(d->entry.name),"%s",e->d_name);
    struct stat st; d->entry.directory=!fstatat(dirfd(d->host),e->d_name,&st,0) && S_ISDIR(st.st_mode);
    return &d->entry;
}
void runtime_host_closedir(RuntimeHostDirectory *d) { if (d) { closedir(d->host); free(d->path); free(d); } }
int runtime_host_stat(const char *path,RuntimeHostStat *out) { struct stat st; if (stat(path,&st)) return -1; out->directory=S_ISDIR(st.st_mode); out->mtime=st.st_mtime; return 0; }
int runtime_host_mkdir(const char *path,int mode) { return mkdir(path,(mode_t)mode); }
FILE *runtime_host_fopen(const char *path,const char *mode) { return fopen(path,mode); }
int runtime_host_flush(FILE *file) { return fflush(file) || fsync(fileno(file)) ? -1 : 0; }
int runtime_host_replace(const char *from,const char *to) { return rename(from,to); }
int runtime_host_unlink(const char *path) { return unlink(path); }
static int remove_entry(const char *path,const struct stat *st,int flag,struct FTW *info) { (void)st; (void)flag; (void)info; return remove(path); }
int runtime_host_remove_tree(const char *path) { return nftw(path,remove_entry,16,FTW_DEPTH|FTW_PHYS); }
int runtime_host_resize(FILE *file,uint64_t size) { if (size>INT64_MAX) { errno=EINVAL; return -1; } return ftruncate(fileno(file),(off_t)size); }
int runtime_host_seek(FILE *file,int64_t offset,int whence) { return fseeko(file,(off_t)offset,whence); }
int64_t runtime_host_tell(FILE *file) { return ftello(file); }
#endif
