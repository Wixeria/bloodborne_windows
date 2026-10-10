/* Native Unicode file backend. Included after the shared guest layouts/mounts. */
static int win_error(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return ENOENT;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: return EACCES;
    case ERROR_INVALID_HANDLE: return EBADF;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return EEXIST;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return ENOSPC;
    case ERROR_DIR_NOT_EMPTY: return ENOTEMPTY;
    case ERROR_DIRECTORY: return ENOTDIR;
    case ERROR_FILENAME_EXCED_RANGE: return ENAMETOOLONG;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: return ENOMEM;
    case ERROR_INVALID_PARAMETER: case ERROR_INVALID_NAME: return EINVAL;
    case ERROR_WRITE_PROTECT: return EROFS;
    case ERROR_NOT_SAME_DEVICE: return EXDEV;
    case ERROR_NOACCESS: return EFAULT;
    default: return EIO;
    }
}
static int wide_path(const char *path,wchar_t out[2048]) {
    wchar_t input[1024], absolute[2040];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,input,1024)) return EINVAL;
    DWORD n=GetFullPathNameW(input,2040,absolute,NULL);
    if (!n || n>=2040) return ENAMETOOLONG;
    /* Extended absolute paths keep DOS device names from becoming host devices. */
    if (absolute[0]=='\\' && absolute[1]=='\\') swprintf(out,2048,L"\\\\?\\UNC\\%ls",absolute+2);
    else swprintf(out,2048,L"\\\\?\\%ls",absolute);
    return 0;
}
static void free_listing(Listing *l) { if (l) { free(l->names); free(l->offsets); free(l->types); free(l); } }
static int listing_add(Listing *l,const wchar_t *name,unsigned char type) {
    char utf8[1024];
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,utf8,sizeof(utf8),NULL,NULL);
    if (!n || n>256) return ENAMETOOLONG;
    if (temp_name(utf8)) return 0;
    size_t bytes=l->count ? l->offsets[l->count-1]+strlen(l->names+l->offsets[l->count-1])+1 : 0;
    char *names=realloc(l->names,bytes+(size_t)n);
    if (!names) return ENOMEM;
    l->names=names;
    size_t *offsets=realloc(l->offsets,(l->count+1)*sizeof(*offsets));
    if (!offsets) return ENOMEM;
    l->offsets=offsets;
    unsigned char *types=realloc(l->types,l->count+1);
    if (!types) return ENOMEM;
    l->types=types;
    memcpy(l->names+bytes,utf8,(size_t)n);
    l->offsets[l->count]=bytes; l->types[l->count]=type; ++l->count;
    return 0;
}
static Listing *list_directory(const wchar_t *path) {
    wchar_t pattern[2052]; swprintf(pattern,2052,L"%ls\\*",path);
    WIN32_FIND_DATAW data;
    HANDLE search=FindFirstFileW(pattern,&data);
    Listing *l=calloc(1,sizeof(*l));
    if (!l) { if (search!=INVALID_HANDLE_VALUE) FindClose(search); errno=ENOMEM; return NULL; }
    int e=listing_add(l,L".",4);
    if (!e) e=listing_add(l,L"..",4);
    if (search!=INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
            e=listing_add(l,data.cFileName,(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8);
            if (e) break;
        } while (FindNextFileW(search,&data));
        DWORD last=GetLastError();
        if (!e && last!=ERROR_NO_MORE_FILES) e=win_error(last);
        FindClose(search);
    } else if (GetLastError()!=ERROR_FILE_NOT_FOUND) e=win_error(GetLastError());
    if (e) { free_listing(l); errno=e; return NULL; }
    return l;
}
static GuestTimespec file_time(FILETIME t) {
    int64_t ticks=(int64_t)(((uint64_t)t.dwHighDateTime<<32)|t.dwLowDateTime)-INT64_C(116444736000000000);
    int64_t sec=ticks/10000000, rest=ticks%10000000;
    if (rest<0) { --sec; rest+=10000000; }
    return (GuestTimespec){sec,rest*100};
}
static int handle_stat(HANDLE host,GuestStat *g) {
    if (!g) return EFAULT;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(host,&info)) return win_error(GetLastError());
    memset(g,0,sizeof(*g));
    g->dev=info.dwVolumeSerialNumber; g->ino=info.nFileIndexLow;
    g->mode=(uint16_t)((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY ? 0040000 : 0100000)|
        (info.dwFileAttributes&FILE_ATTRIBUTE_READONLY ? 0444 : 0666));
    g->nlink=(uint16_t)info.nNumberOfLinks;
    g->size=(int64_t)(((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow);
    g->blocks=(g->size+511)/512; g->blksize=16384;
    g->atime=file_time(info.ftLastAccessTime); g->mtime=file_time(info.ftLastWriteTime);
    g->birthtime=file_time(info.ftCreationTime);
    FILE_BASIC_INFO basic;
    if (GetFileInformationByHandleEx(host,FileBasicInfo,&basic,sizeof(basic))) {
        FILETIME changed={(DWORD)basic.ChangeTime.LowPart,(DWORD)basic.ChangeTime.HighPart};
        g->ctime=file_time(changed);
    } else g->ctime=g->mtime;
    return 0;
}
static File *get(int fd) { return fd>=3 && fd<MAX_FILES && files[fd].used ? &files[fd] : NULL; }
static int game_path(const char *p) { return readonly_path(p); }
/* Path operations see the working copy while a save descriptor is open. */
static unsigned temp_serial;
static void current_copy(char *path,size_t size,int writing) {
    pthread_mutex_lock(&lock);
    for (int i=3;i<MAX_FILES;++i) {
        File *f=&files[i];
        if (f->used && f->commit && !strcmp(f->commit,path)) {
            snprintf(path,size,"%s",f->temp); f->dirty|=writing; break;
        }
    }
    pthread_mutex_unlock(&lock);
}
static int save_copy(const char *path,const char *source,int flags,char **commit,char **temp) {
    wchar_t original[2048],scratch[2048];
    int e=wide_path(source,original); if (e) return e;
    DWORD attrs=GetFileAttributesW(original),last=GetLastError();
    int exists=attrs!=INVALID_FILE_ATTRIBUTES;
    if (!exists && last!=ERROR_FILE_NOT_FOUND && last!=ERROR_PATH_NOT_FOUND) return win_error(last);
    if (exists && (attrs&FILE_ATTRIBUTE_DIRECTORY)) return EISDIR;
    if (exists && (flags&0x200) && (flags&0x800)) return EEXIST;
    if (!exists && !(flags&0x200)) return ENOENT;
    size_t bytes=strlen(path)+40;
    char *c=strdup(path),*t=malloc(bytes);
    if (!c || !t) { free(c); free(t); return ENOMEM; }
    snprintf(t,bytes,"%s.%lu.%u.bbtmp",path,(unsigned long)GetCurrentProcessId(),
        __atomic_add_fetch(&temp_serial,1,__ATOMIC_RELAXED));
    e=wide_path(t,scratch);
    if (!e && exists && !(flags&0x400)) {
        if (!CopyFileW(original,scratch,TRUE)) e=win_error(GetLastError());
    } else if (!e) {
        HANDLE h=CreateFileW(scratch,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_DELETE,
            NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if (h==INVALID_HANDLE_VALUE) e=win_error(GetLastError());
        else if (!CloseHandle(h)) e=win_error(GetLastError());
    }
    if (e) { free(c); free(t); return e; }
    *commit=c; *temp=t; return 0;
}
static void discard_copy(char *commit,char *temp) {
    if (temp) runtime_host_unlink(temp);
    free(commit); free(temp);
}
static int64_t do_open(const char *guest,int flags,int mode) {
    (void)mode;
    if (game_path(guest) && (flags&(3|0x8|0x200|0x400|0x800))) return -EROFS;
    if ((flags&3)==3 || ((flags&3)==0 && (flags&(0x400|0x8)))) return -EINVAL;
    char path[1024]; wchar_t wide[2048];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    char current[1100]; snprintf(current,sizeof(current),"%s",path);
    if (save_path(guest)) current_copy(current,sizeof(current),0);
    char *commit=NULL,*temp=NULL;
    if (save_path(guest) && (flags&3)) {
        e=save_copy(path,current,flags,&commit,&temp);
        if (e) return -e;
        snprintf(current,sizeof(current),"%s",temp);
    }
    e=wide_path(current,wide);
    if (e) { discard_copy(commit,temp); return -e; }
    DWORD access=(flags&3)==0 ? GENERIC_READ : (flags&3)==1 ? GENERIC_WRITE : GENERIC_READ|GENERIC_WRITE;
    DWORD creation=(flags&0x200) ? ((flags&0x800) ? CREATE_NEW : (flags&0x400) ? CREATE_ALWAYS : OPEN_ALWAYS) :
                   (flags&0x400) ? TRUNCATE_EXISTING : OPEN_EXISTING;
    if (temp) creation=OPEN_EXISTING;
    DWORD attrs=FILE_FLAG_BACKUP_SEMANTICS|((flags&0x80) ? FILE_FLAG_WRITE_THROUGH : 0);
    HANDLE host=CreateFileW(wide,access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,creation,attrs,NULL);
    if (host==INVALID_HANDLE_VALUE) {
        e=win_error(GetLastError());
        if (e==ENOENT) { pthread_mutex_lock(&lock); ++missing; pthread_mutex_unlock(&lock); }
        discard_copy(commit,temp); return -e;
    }
    GuestStat stat; e=handle_stat(host,&stat);
    int directory=!e && (stat.mode&0170000)==0040000;
    if (!e && (flags&0x20000) && !directory) e=ENOTDIR;
    Listing *dir=!e && directory ? list_directory(wide) : NULL;
    if (!e && directory && !dir) e=errno;
    if (e) { CloseHandle(host); free_listing(dir); discard_copy(commit,temp); return -e; }
    pthread_mutex_lock(&lock);
    int fd=3; while (fd<MAX_FILES && files[fd].used) ++fd;
    if (fd==MAX_FILES) { pthread_mutex_unlock(&lock); CloseHandle(host); free_listing(dir); discard_copy(commit,temp); return -EMFILE; }
    files[fd]=(File){.used=1,.host=host,.append=!!(flags&0x8),.dir=dir,.commit=commit,.temp=temp,
        .dirty=temp && (flags&(0x200|0x400))};
    snprintf(files[fd].path,sizeof(files[fd].path),"%s",guest); ++opens;
    pthread_mutex_unlock(&lock);
    return fd;
}
static int64_t do_close(int fd) {
    if (fd>=0 && fd<3) return 0;
    pthread_mutex_lock(&lock); File *f=get(fd);
    int e=0;
    if (!f) e=EBADF;
    else {
        if (f->temp && f->dirty && !FlushFileBuffers(f->host)) e=win_error(GetLastError());
        if (!CloseHandle(f->host) && !e) e=win_error(GetLastError());
        if (f->temp && f->dirty && !e && runtime_host_replace(f->temp,f->commit)) e=errno;
        if (e && f->temp) fprintf(stderr,"Runtime: save %s not replaced; previous save kept (%s)\n",f->path,strerror(e));
        discard_copy(f->commit,f->temp); free_listing(f->dir); *f=(File){0};
    }
    pthread_mutex_unlock(&lock); return -e;
}
static void touch_for_write(void *buffer,uint64_t size) {
    runtime_memory_prepare_cpu_write((uintptr_t)buffer,size);
    uintptr_t start=(uintptr_t)buffer;
    for (uint64_t at=0;at<size;) {
        volatile unsigned char *b=(volatile unsigned char *)(start+at); *b=*b;
        uint64_t step=4096-((start+at)&4095); if (step>size-at) break; at+=step;
    }
}
/* Reference backend serializes handle operations, including pread/pwrite seek/restore,
   to keep offsets and descriptor lifetime correct during close/seek races. */
static int64_t file_io(int fd,void *buffer,uint64_t size,int64_t offset,int positional,int writing) {
    if (!buffer && size) return -EFAULT;
    if (size>INT64_MAX || size>SIZE_MAX-(uintptr_t)buffer || (positional && offset<0)) return -EINVAL;
    if (!writing) touch_for_write(buffer,size);
    pthread_mutex_lock(&lock); File *f=get(fd);
    HANDLE host=f ? f->host : fd>=0 && fd<3 ? GetStdHandle(fd==0 ? STD_INPUT_HANDLE : fd==1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE) : INVALID_HANDLE_VALUE;
    int e=0; uint64_t done=0; LARGE_INTEGER saved={0}, where={0}; int restore=0;
    if (host==INVALID_HANDLE_VALUE || !host) e=EBADF;
    else if (f && f->dir) e=EISDIR;
    else if (positional) {
        if (!SetFilePointerEx(host,where,&saved,FILE_CURRENT)) e=ESPIPE;
        else { restore=1; where.QuadPart=offset; if (!SetFilePointerEx(host,where,NULL,FILE_BEGIN)) e=win_error(GetLastError()); }
    } else if (writing && f && f->append && !SetFilePointerEx(host,where,NULL,FILE_END)) e=win_error(GetLastError());
    while (!e && done<size) {
        DWORD n=0, chunk=(DWORD)(size-done>0x40000000 ? 0x40000000 : size-done);
        BOOL ok=writing ? WriteFile(host,(char *)buffer+done,chunk,&n,NULL) : ReadFile(host,(char *)buffer+done,chunk,&n,NULL);
        if (!ok) { DWORD last=GetLastError(); if (!writing && (last==ERROR_HANDLE_EOF || last==ERROR_BROKEN_PIPE)) break; e=win_error(last); break; }
        done+=n; if (n<chunk) break;
    }
    if (restore && !SetFilePointerEx(host,saved,NULL,FILE_BEGIN) && !e) e=win_error(GetLastError());
    if (!writing) { ++reads; bytes_read+=done; } else { ++writes; if (f && done) f->dirty=1; }
    pthread_mutex_unlock(&lock);
    return done ? (int64_t)done : e ? -e : 0;
}
static int64_t do_read(int fd,void *buffer,uint64_t n) { return file_io(fd,buffer,n,0,0,0); }
static int64_t do_pread(int fd,void *buffer,uint64_t n,int64_t o) { return file_io(fd,buffer,n,o,1,0); }
static int64_t do_write(int fd,const void *buffer,uint64_t n) { return file_io(fd,(void *)buffer,n,0,0,1); }
static int64_t do_pwrite(int fd,const void *buffer,uint64_t n,int64_t o) { return file_io(fd,(void *)buffer,n,o,1,1); }
static int64_t do_lseek(int fd,int64_t offset,int whence) {
    if (whence<0 || whence>2) return -EINVAL;
    pthread_mutex_lock(&lock); File *f=get(fd); int64_t result=-EBADF;
    if (f && f->dir) { if (whence==0 && offset>=0) { f->position=(size_t)offset; result=offset; } else result=-EINVAL; }
    else if (f) { LARGE_INTEGER p={.QuadPart=offset},out; result=SetFilePointerEx(f->host,p,&out,(DWORD)whence) ? out.QuadPart : -win_error(GetLastError()); }
    pthread_mutex_unlock(&lock); return result;
}
static int64_t do_fstat(int fd,GuestStat *out) {
    pthread_mutex_lock(&lock); File *f=get(fd); int e=f ? handle_stat(f->host,out) : EBADF;
    pthread_mutex_unlock(&lock); return -e;
}
static int64_t do_stat(const char *guest,GuestStat *out) {
    char path[1024]; wchar_t wide[2048]; int e=translate(guest,path,sizeof(path));
    if (!out) return -EFAULT;
    if (!e && save_path(guest)) current_copy(path,sizeof(path),0);
    if (!e) e=wide_path(path,wide); if (e) return -e;
    HANDLE h=CreateFileW(wide,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if (h==INVALID_HANDLE_VALUE) return -win_error(GetLastError());
    e=handle_stat(h,out); CloseHandle(h); return -e;
}
static int64_t do_getdents(int fd,char *buffer,uint64_t size,int64_t *base) {
    pthread_mutex_lock(&lock); File *f=get(fd); int64_t result=0;
    if (!f) result=-EBADF;
    else if (!f->dir) result=-EINVAL;
    else if (!buffer) result=-EFAULT;
    else if (size<512 || size>SIZE_MAX-(uintptr_t)buffer) result=-EINVAL;
    else {
        if (base) *base=(int64_t)f->position;
        uint64_t written=0;
        while (f->position<f->dir->count) {
            const char *name=f->dir->names+f->dir->offsets[f->position]; size_t n=strlen(name);
            uint16_t length=(uint16_t)((8+n+1+7)&~(size_t)7);
            if (length>size-written) break;
            char *p=buffer+written; memset(p,0,length); uint32_t ino=(uint32_t)f->position+1;
            memcpy(p,&ino,4); memcpy(p+4,&length,2); p[6]=(char)f->dir->types[f->position]; p[7]=(char)n;
            memcpy(p+8,name,n); written+=length; ++f->position;
        }
        result=(int64_t)written;
    }
    pthread_mutex_unlock(&lock); return result;
}
static int64_t path_op(const char *guest,int op,int mode) {
    (void)mode; if (game_path(guest)) return -EROFS;
    char path[1024]; wchar_t wide[2048]; int e=translate(guest,path,sizeof(path)); if (!e) e=wide_path(path,wide);
    if (e) return -e;
    BOOL ok=op==0 ? CreateDirectoryW(wide,NULL) : op==1 ? RemoveDirectoryW(wide) : DeleteFileW(wide);
    return ok ? 0 : -win_error(GetLastError());
}
static int64_t do_rename(const char *from,const char *to) {
    if (game_path(from) || game_path(to)) return -EROFS;
    char a[1024],b[1024]; wchar_t wa[2048],wb[2048]; int e=translate(from,a,sizeof(a));
    if (!e) e=translate(to,b,sizeof(b)); if (!e) e=wide_path(a,wa); if (!e) e=wide_path(b,wb);
    if (e) return -e;
    return MoveFileExW(wa,wb,MOVEFILE_REPLACE_EXISTING) ? 0 : -win_error(GetLastError());
}
static int64_t do_ftruncate(int fd,int64_t length) {
    if (length<0) return -EINVAL;
    pthread_mutex_lock(&lock); File *f=get(fd); int e=EBADF;
    if (f) {
        FILE_END_OF_FILE_INFO info={.EndOfFile={.QuadPart=length}};
        e=SetFileInformationByHandle(f->host,FileEndOfFileInfo,&info,sizeof(info)) ? 0 : win_error(GetLastError());
        if (!e) f->dirty=1;
    }
    pthread_mutex_unlock(&lock); return -e;
}
static int64_t do_truncate(const char *guest,int64_t length) {
    int64_t fd=do_open(guest,1,0); if (fd<0) return fd;
    int64_t result=do_ftruncate((int)fd,length); do_close((int)fd); return result;
}
static int64_t do_fsync(int fd) {
    pthread_mutex_lock(&lock); File *f=get(fd); int e=f ? (FlushFileBuffers(f->host) ? 0 : win_error(GetLastError())) : EBADF;
    pthread_mutex_unlock(&lock); return -e;
}
static int64_t do_access(const char *guest,int mode) {
    GuestStat stat; int64_t result=do_stat(guest,&stat); if (result) return result;
    if (mode&2) { if (game_path(guest)) return -EROFS; if (!(stat.mode&0222)) return -EACCES; }
    return 0;
}
