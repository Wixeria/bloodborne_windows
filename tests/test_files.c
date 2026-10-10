#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_host_fs.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
static const struct { const char *nid,*symbol; } names[]={
#include "import_names.inc"
#include "runtime_contract_names.inc"
};
static uintptr_t resolve(const char *symbol) {
    for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) if (!strcmp(names[i].symbol,symbol)) {
        uintptr_t p=runtime_resolve(names[i].nid,0); if (p) return p;
    }
    fprintf(stderr,"Missing export %s\n",symbol); assert(0); return 0;
}
#define GET(type,name) ((type)resolve(name))
typedef struct { int64_t sec,nsec; } Time;
typedef struct { uint32_t dev,ino; uint16_t mode,nlink; uint32_t uid,gid,rdev; Time atime,mtime,ctime;
    int64_t size,blocks; uint32_t blksize,flags,gen; int32_t spare; Time birth; } Stat;
typedef int64_t (ABI *Open)(const char *,int,int);
typedef int64_t (ABI *Pwrite)(int,const void *,uint64_t,int64_t);
typedef int64_t (ABI *Truncate)(int,int64_t);
typedef int64_t (ABI *FdOp)(int);
typedef int64_t (ABI *Rename)(const char *,const char *);
typedef int64_t (ABI *PathOp)(const char *);
static char root[1024],app[1024],user[1024];
static void setup(void) {
#ifdef _WIN32
    wchar_t temp[MAX_PATH],unique[MAX_PATH];
    assert(GetTempPathW(MAX_PATH,temp) && GetTempFileNameW(temp,L"bbp",0,unique));
    assert(DeleteFileW(unique) && CreateDirectoryW(unique,NULL));
    assert(WideCharToMultiByte(CP_UTF8,0,unique,-1,root,sizeof(root),NULL,NULL));
#else
    strcpy(root,"/tmp/bbport-files-XXXXXX"); assert(mkdtemp(root));
#endif
    snprintf(app,sizeof(app),"%s/app0",root); snprintf(user,sizeof(user),"%s/user-\xC5\x9F\xC3\xB6\xC3\xA7",root);
    assert(!runtime_host_mkdir(app,0755) && !runtime_host_mkdir(user,0755));
    char path[1200]; snprintf(path,sizeof(path),"%s/test.bin",app);
    FILE *f=runtime_host_fopen(path,"wb"); assert(f && fwrite("GAME",1,4,f)==4); fclose(f);
    runtime_start(1); runtime_thread_attach_main(); runtime_file_configure(app,user);
}
static void files(void) {
    int fd=(int)runtime_file_open("test.bin",0,0); assert(fd>=3);
    char data[32]={0}; assert(runtime_file_read(fd,data,4)==4 && !memcmp(data,"GAME",4));
    assert(!runtime_file_close(fd)); assert(runtime_file_read(fd,data,1)==-EBADF);
    assert(runtime_file_open("/app0/test.bin",1,0)==-EROFS);
    const char *path="/data/save-\xC5\x9F\xC3\xB6\xC3\xA7.bin";
    fd=(int)runtime_file_open(path,2|0x200|0x400,0600); assert(fd>=3);
    assert(runtime_file_write(fd,"abcdefgh",8)==8);
    assert(runtime_file_pread(fd,data,3,2)==3 && !memcmp(data,"cde",3));
    assert(runtime_file_lseek(fd,0,1)==8); /* pread must not move the sequential offset */
    assert(GET(Pwrite,"pwrite")(fd,"XYZ",3,1)==3 && runtime_file_lseek(fd,0,1)==8);
    assert(!GET(FdOp,"fsync")(fd));
    struct { Stat s; uint64_t guard; } stat={.guard=0xdeadbeef};
    assert(!runtime_file_fstat(fd,&stat.s) && stat.guard==0xdeadbeef && stat.s.size==8);
    assert(stat.s.mtime.sec>1700000000 && stat.s.birth.sec>1700000000 && stat.s.mtime.nsec<1000000000);
    assert(runtime_file_lseek(fd,INT64_C(5)*1024*1024*1024,0)==INT64_C(5)*1024*1024*1024);
    assert(runtime_file_read(fd,data,1)==0); /* >4 GiB seek without allocating gigabytes */
    assert(!GET(Truncate,"ftruncate")(fd,4)); assert(!runtime_file_fstat(fd,&stat.s) && stat.s.size==4);
    assert(!runtime_file_close(fd));
    fd=(int)runtime_file_open(path,1|0x8,0); assert(fd>=3); assert(runtime_file_write(fd,"!",1)==1); assert(!runtime_file_close(fd));
    assert(!GET(Rename,"rename")(path,"/data/renamed.bin")); assert(!runtime_file_stat("/data/renamed.bin",&stat.s) && stat.s.size==5);
    fd=(int)runtime_file_open("/data",0x20000,0); assert(fd>=3);
    char entries[4096]; int64_t base=-1,n=runtime_file_getdents(fd,entries,sizeof(entries),&base); assert(n>0 && base==0);
    int found=0;
    for (int64_t at=0;at<n;) { uint16_t length; memcpy(&length,entries+at+4,2); assert(length>=16 && !(length&7) && at+length<=n);
        if (!strcmp(entries+at+8,"renamed.bin")) { found=1; assert(entries[at+6]==8); } at+=length; }
    assert(found && runtime_file_getdents(fd,entries,sizeof(entries),NULL)==0);
    assert(runtime_file_lseek(fd,0,0)==0 && runtime_file_getdents(fd,entries,sizeof(entries),NULL)>0);
    assert(!runtime_file_close(fd)); assert(!GET(PathOp,"unlink")("/data/renamed.bin"));
    assert(GET(Open,"open")("/data/missing",0,0)==-1 && *runtime_errno()==2);
    assert((uint32_t)GET(Open,"sceKernelOpen")("/data/missing",0,0)==0x80020002);
}
static void paths(void) {
    char translated[1024];
    assert(runtime_file_translate("/data/../escape",translated,sizeof(translated))==EACCES);
    assert(runtime_file_translate("/app00/x",translated,sizeof(translated))==ENOENT);
    assert(!runtime_file_translate("/data/x",translated,sizeof(translated)) && strstr(translated,"/data/x"));
#ifdef _WIN32
    const char *bad[]={"/data/..\\escape","/data/file:stream","C:/outside","/data/x.","/data/x ","/data/x?"};
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) assert(runtime_file_translate(bad[i],translated,sizeof(translated))==EACCES);
#endif
    char long_path[1200]; memset(long_path,'x',sizeof(long_path)-1); long_path[sizeof(long_path)-1]=0;
    assert(runtime_file_translate(long_path,translated,sizeof(translated))==ENAMETOOLONG);
    assert(!runtime_file_mount_mode("/savedata0",user,1)); assert(runtime_file_open("/savedata0/x",1|0x200,0)==-EROFS);
    runtime_file_unmount("/savedata0"); assert(runtime_file_translate("/savedata0/x",translated,sizeof(translated))==ENOENT);
}
static int shared_fd;
static void *reader(void *arg) { (void)arg; char b[4]; for (int i=0;i<500;++i) assert(runtime_file_pread(shared_fd,b,4,0)==4 && !memcmp(b,"GAME",4)); return NULL; }
static void concurrent(void) {
    shared_fd=(int)runtime_file_open("/app0/test.bin",0,0); assert(shared_fd>=3);
    pthread_t threads[8]; for (int i=0;i<8;++i) assert(!pthread_create(&threads[i],NULL,reader,NULL));
    for (int i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
    assert(runtime_file_lseek(shared_fd,0,1)==0); assert(!runtime_file_close(shared_fd));
}
typedef struct { char data[32]; } DirName;
typedef struct { char data[16]; } Point;
typedef struct { int32_t user,pad; const DirName *dir; uint64_t blocks; uint32_t mode; uint8_t reserved[32]; int32_t pad2; } Mount;
typedef struct { Point point; uint64_t blocks; uint32_t unused,status; uint8_t reserved[28]; int32_t pad; } Mounted;
typedef int32_t (ABI *Init)(const void *);
typedef int32_t (ABI *SaveMount)(const Mount *,Mounted *);
typedef int32_t (ABI *Umount)(const Point *);
typedef int32_t (ABI *MemorySetup)(int32_t,uint64_t,const void *);
typedef int32_t (ABI *MemoryIo)(int32_t,void *,uint64_t,int64_t);
typedef struct { char title[128],subtitle[128],detail[1024]; uint32_t value; int32_t pad; int64_t mtime; uint8_t reserved[32]; } Param;
typedef int32_t (ABI *SetParam)(const Point *,uint32_t,const void *,uint64_t);
typedef struct { int32_t user,pad; const void *title; const DirName *dir; uint32_t key,order; uint8_t reserved[32]; } Search;
typedef struct { uint32_t hits; int32_t pad; DirName *names; uint32_t capacity,count; Param *params; void *infos; uint8_t reserved[12]; int32_t pad2; } SearchResult;
typedef int32_t (ABI *SearchFn)(const Search *,SearchResult *);
typedef struct { int32_t user,pad; const void *title; const DirName *dir; uint32_t unused; uint8_t reserved[32]; int32_t pad2; } Delete;
typedef int32_t (ABI *DeleteFn)(const Delete *);
static void saves(void) {
    runtime_savedata_configure("TEST00001"); assert(!GET(Init,"sceSaveDataInitialize")(NULL));
    DirName dir={"SLOT0001"}; Mount m={.user=1,.dir=&dir,.blocks=64,.mode=4}; Mounted mounted;
    SaveMount mount=GET(SaveMount,"sceSaveDataMount2"); Umount unmount=GET(Umount,"sceSaveDataUmount");
    assert(!mount(&m,&mounted) && mounted.status==1 && !strcmp(mounted.point.data,"/savedata0"));
    int fd=(int)runtime_file_open("/savedata0/save.bin",2|0x200,0600); assert(fd>=3);
    assert(runtime_file_write(fd,"PERSIST",7)==7 && !runtime_file_close(fd));
    const char title[]="TEST SAVE";
    assert(!GET(SetParam,"sceSaveDataSetParam")(&mounted.point,1,title,sizeof(title)));
    Point invalid={"/savedata0evil"}; assert(unmount(&invalid)!=0);
    assert(!unmount(&mounted.point));
    m.mode=1; assert(!mount(&m,&mounted) && mounted.status==0);
    assert(runtime_file_open("/savedata0/save.bin",1,0)==-EROFS);
    fd=(int)runtime_file_open("/savedata0/save.bin",0,0); char data[8]={0};
    assert(fd>=3 && runtime_file_read(fd,data,7)==7 && !strcmp(data,"PERSIST") && !runtime_file_close(fd));
    Delete remove={.user=1,.dir=&dir}; DeleteFn erase=GET(DeleteFn,"sceSaveDataDelete");
    assert(erase(&remove)!=0); /* A live mount must prevent deletion. */
    assert(!unmount(&mounted.point));
    DirName found={0}; Param param={0}; Search search={.user=1};
    SearchResult result={.names=&found,.capacity=1,.params=&param}; SearchFn find=GET(SearchFn,"sceSaveDataDirNameSearch");
    assert(!find(&search,&result) && result.hits==1 && result.count==1 && !strcmp(found.data,dir.data));
    assert(!strcmp(param.title,title) && param.mtime>1700000000);
    assert(!erase(&remove)); result.hits=99; assert(!find(&search,&result) && !result.hits);
    MemorySetup setup=GET(MemorySetup,"sceSaveDataSetupSaveDataMemory"); MemoryIo set=GET(MemoryIo,"sceSaveDataSetSaveDataMemory"),get=GET(MemoryIo,"sceSaveDataGetSaveDataMemory");
    assert(!setup(1,4096,NULL)); assert(!set(1,"BLOB",4,32)); memset(data,0,sizeof(data)); assert(!get(1,data,4,32) && !strcmp(data,"BLOB"));
    assert(get(1,data,UINT64_MAX,1)!=0); assert(get(1,data,8,4090)!=0);
    assert(!setup(1,4096,NULL)); memset(data,0,sizeof(data)); assert(!get(1,data,4,32) && !strcmp(data,"BLOB"));
    strcpy(dir.data,"../outside"); m.mode=4; assert(mount(&m,&mounted)!=0);
}
static void host_contents(const char *path,const char *expected) {
    char bytes[32]={0}; FILE *f=runtime_host_fopen(path,"rb"); assert(f);
    size_t count=fread(bytes,1,sizeof(bytes),f); assert(!ferror(f)); fclose(f);
    assert(count==strlen(expected) && !memcmp(bytes,expected,count));
}
static void atomic_save(const char *exe) {
    (void)exe;
    char folder[1200],host[1400]; snprintf(folder,sizeof(folder),"%s/atomic",user);
    assert(!runtime_host_mkdir(folder,0755)); snprintf(host,sizeof(host),"%s/save.bin",folder);
    FILE *f=runtime_host_fopen(host,"wb"); assert(f && fwrite("OLD",1,3,f)==3); fclose(f);
    assert(!runtime_file_mount("/savedata0",folder));
    int fd=(int)runtime_file_open("/savedata0/save.bin",2|0x400,0600); assert(fd>=3);
    assert(runtime_file_write(fd,"NEW-COMPLETE",12)==12);
    host_contents(host,"OLD"); /* A crash before close must leave the previous commit intact. */
    Stat stat; assert(!runtime_file_stat("/savedata0/save.bin",&stat) && stat.size==12);
    int reader=(int)runtime_file_open("/savedata0/save.bin",0,0); assert(reader>=3);
    char bytes[32]={0}; assert(runtime_file_read(reader,bytes,12)==12 && !strcmp(bytes,"NEW-COMPLETE"));
    assert(!runtime_file_close(reader)); assert(!runtime_file_close(fd));
    host_contents(host,"NEW-COMPLETE");
    assert(runtime_file_open("/savedata0/save.bin",1|0x200|0x800,0600)==-EEXIST);
    fd=(int)runtime_file_open("/savedata0/save.bin",1,0); assert(fd>=3);
    assert(!runtime_file_close(fd)); host_contents(host,"NEW-COMPLETE");
#ifdef _WIN32
    wchar_t executable[2048],directory[2400],command[4500];
    assert(GetModuleFileNameW(NULL,executable,2048));
    /* ASCII transport keeps the test independent of the child's CRT argv code page. */
    for (size_t i=0;i<strlen(folder);++i) swprintf(directory+2*i,3,L"%02x",(unsigned char)folder[i]);
    swprintf(command,4300,L"\"%ls\" --abandon-save \"%ls\"",executable,directory);
    STARTUPINFOW startup={.cb=sizeof(startup)}; PROCESS_INFORMATION process={0};
    assert(CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process));
    assert(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);
    DWORD code=1; assert(GetExitCodeProcess(process.hProcess,&code) && code==0);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    host_contents(host,"NEW-COMPLETE");
    unsigned abandoned=0;
    RuntimeHostDirectory *dir=runtime_host_opendir(folder); assert(dir);
    for (const RuntimeHostEntry *entry; (entry=runtime_host_readdir(dir));)
        if (strstr(entry->name,".bbtmp")) ++abandoned;
    runtime_host_closedir(dir); assert(abandoned==1);
    assert(!runtime_file_mount("/savedata1",folder)); /* Next mount discards incomplete save. */
    dir=runtime_host_opendir(folder); assert(dir);
    for (const RuntimeHostEntry *entry; (entry=runtime_host_readdir(dir));) assert(!strstr(entry->name,".bbtmp"));
    runtime_host_closedir(dir); host_contents(host,"NEW-COMPLETE");
#endif
}
int main(int argc,char **argv) {
#ifdef _WIN32
    if (argc==3 && !strcmp(argv[1],"--abandon-save")) {
        char folder[1200]={0}; size_t n=strlen(argv[2]); assert(!(n%2) && n/2<sizeof(folder));
        for (size_t i=0;i<n/2;++i) { unsigned value=0; assert(sscanf(argv[2]+2*i,"%2x",&value)==1); folder[i]=(char)value; }
        assert(!runtime_file_mount("/savedata0",folder));
        int fd=(int)runtime_file_open("/savedata0/save.bin",1|0x400,0600); assert(fd>=3);
        assert(runtime_file_write(fd,"INCOMPLETE",10)==10);
        ExitProcess(0); /* Intentionally no close/commit. Unique test scratch directory only. */
    }
#endif
    assert(argc==2); setup();
    if (!strcmp(argv[1],"io")) files(); else if (!strcmp(argv[1],"paths")) paths();
    else if (!strcmp(argv[1],"concurrency")) concurrent(); else if (!strcmp(argv[1],"saves")) saves();
    else if (!strcmp(argv[1],"atomic")) atomic_save(argv[0]); else assert(0);
    /* Only remove the unique scratch tree created by setup; never real user saves. */
    assert(!runtime_host_remove_tree(root)); printf("File/save %s PASS\n",argv[1]); return 0;
}
