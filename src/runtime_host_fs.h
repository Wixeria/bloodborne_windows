#ifndef BB_RUNTIME_HOST_FS_H
#define BB_RUNTIME_HOST_FS_H
#include <stdio.h>
#include <stdint.h>
typedef struct RuntimeHostDirectory RuntimeHostDirectory;
typedef struct { char name[1024]; int directory; } RuntimeHostEntry;
typedef struct { int directory; int64_t mtime; } RuntimeHostStat;
RuntimeHostDirectory *runtime_host_opendir(const char *path);
const RuntimeHostEntry *runtime_host_readdir(RuntimeHostDirectory *dir);
void runtime_host_closedir(RuntimeHostDirectory *dir);
int runtime_host_stat(const char *path,RuntimeHostStat *out);
int runtime_host_mkdir(const char *path,int mode);
FILE *runtime_host_fopen(const char *path,const char *mode);
int runtime_host_flush(FILE *file);
int runtime_host_replace(const char *from,const char *to);
int runtime_host_unlink(const char *path);
int runtime_host_remove_tree(const char *path);
int runtime_host_resize(FILE *file,uint64_t size);
int runtime_host_seek(FILE *file,int64_t offset,int whence);
int64_t runtime_host_tell(FILE *file);
#endif
