/* Windows host backend, included only by runtime_memory.c after its VMA helpers.
 * Placeholders remove the ordinary 64 KiB view-alignment restriction. Each
 * mapped PS4 page has its own view so unmapping/replacing 16 KiB never exposes
 * neighboring live guest pages to a temporary unmap. Windows 10 1803+ required. */
#ifndef BB_MEMORY_WINDOWS_BACKEND_H
#define BB_MEMORY_WINDOWS_BACKEND_H

static HANDLE win_section;
typedef struct { uintptr_t start,end; } WinArena;
static WinArena *win_arenas;
static size_t win_arena_count,win_arena_capacity;
static DWORD win_protection(int prot) {
    if (prot & PROT_EXEC) return prot & PROT_WRITE ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    if (prot & PROT_WRITE) return PAGE_READWRITE;
    return prot & PROT_READ ? PAGE_READONLY : PAGE_NOACCESS;
}
static uintptr_t win_owned_end(uintptr_t address) {
    for (size_t i=0;i<win_arena_count;++i)
        if (win_arenas[i].start<=address && address<win_arenas[i].end) return win_arenas[i].end;
    return 0;
}
/* The Windows heap/DLL allocator may already occupy parts of the 40-bit
   window. Reserve only needed chunks, never replace unrelated host memory. */
static int win_reserve_range(uintptr_t address,uint64_t size) {
    if (address<LOW_MIN || address>=USER_MAX || size>USER_MAX-address) return -1;
    uintptr_t end=address+size;
    for (uintptr_t at=address;at<end;) {
        uintptr_t owned=win_owned_end(at);
        if (owned) { at=owned; continue; }
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)at,&m,sizeof(m)) || m.State!=MEM_FREE) return -1;
        uintptr_t base=at&~(uintptr_t)65535;
        if (base<(uintptr_t)m.BaseAddress) return -1;
        uintptr_t available=(uintptr_t)m.BaseAddress+m.RegionSize;
        uintptr_t wanted=align_up(end,65536);
        uintptr_t chunk=base+UINT64_C(64)*1024*1024;
        if (wanted<chunk) wanted=chunk;
        if (wanted>available) wanted=available;
        if (wanted>USER_MAX) wanted=USER_MAX;
        if (win_arena_count==win_arena_capacity) {
            size_t capacity=win_arena_capacity ? win_arena_capacity*2 : 16;
            WinArena *next=realloc(win_arenas,capacity*sizeof(*next));
            if (!next) return -1;
            win_arenas=next; win_arena_capacity=capacity;
        }
        void *p=VirtualAlloc2(NULL,(void *)base,wanted-base,MEM_RESERVE|MEM_RESERVE_PLACEHOLDER,PAGE_NOACCESS,NULL,0);
        if (!p) return -1;
        win_arenas[win_arena_count++]=(WinArena){base,wanted};
        at=wanted;
    }
    return 0;
}
/* Suggest a new hint after a foreign host allocation, or 0 when clear. */
static uintptr_t win_free_hint(uintptr_t address,uint64_t size) {
    for (uintptr_t at=address,end=address+size;at<end;) {
        uintptr_t owned=win_owned_end(at);
        if (owned) { at=owned; continue; }
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)at,&m,sizeof(m))) return USER_MAX;
        uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;
        if (m.State!=MEM_FREE) return align_up(next,65536);
        at=next;
    }
    return 0;
}
static int win_pool(void) {
    if (win_section) return 0;
    uint64_t size=POOL_SIZE+FLEX_SPAN;
    HANDLE section=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_EXECUTE_READWRITE|SEC_RESERVE,
                                     (DWORD)(size>>32),(DWORD)size,NULL);
    if (!section) { fprintf(stderr,"Windows memory: backing section failed (%lu)\n",GetLastError()); return -1; }
    void *view=MapViewOfFile(section,FILE_MAP_READ|FILE_MAP_WRITE,0,0,0);
    if (!view) { fprintf(stderr,"Windows memory: backing view failed (%lu)\n",GetLastError()); CloseHandle(section); return -1; }
    win_section=section;
    backing_base=view;
    return 0;
}
/* Split a reserved placeholder until [address,address+size) is exactly one
   placeholder. No memory is committed by this operation. */
static int win_isolate(uintptr_t address, uint64_t size) {
    MEMORY_BASIC_INFORMATION m;
    if (address<LOW_MIN || address>=USER_MAX || size>USER_MAX-address ||
        !VirtualQuery((void *)address,&m,sizeof(m)) || m.State!=MEM_RESERVE || m.Type!=MEM_PRIVATE)
        return -1;
    uintptr_t base=(uintptr_t)m.AllocationBase;
    if (base<address && !VirtualFree((void *)base,address-base,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) return -1;
    if (!VirtualQuery((void *)address,&m,sizeof(m))) return -1;
    while (m.RegionSize<size) {
        MEMORY_BASIC_INFORMATION next;
        uintptr_t adjacent=address+m.RegionSize;
        if (!VirtualQuery((void *)adjacent,&next,sizeof(next)) || next.State!=MEM_RESERVE ||
            next.Type!=MEM_PRIVATE || (uintptr_t)next.AllocationBase!=adjacent ||
            !VirtualFree((void *)address,m.RegionSize+next.RegionSize,MEM_RELEASE|MEM_COALESCE_PLACEHOLDERS) ||
            !VirtualQuery((void *)address,&m,sizeof(m))) return -1;
    }
    if (m.RegionSize>size && !VirtualFree((void *)address,size,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) return -1;
    return 0;
}
static int win_placeholder(uintptr_t address) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((void *)address,&m,sizeof(m))) return -1;
    if (m.Type==MEM_MAPPED) {
        if ((uintptr_t)m.AllocationBase!=address ||
            !UnmapViewOfFile2(GetCurrentProcess(),(void *)address,MEM_PRESERVE_PLACEHOLDER)) return -1;
    }
    return win_isolate(address,PAGE);
}
static int win_map_page(uintptr_t address, uint64_t phys, DWORD prot) {
    /* The initial view protection bounds later VirtualProtect upgrades. Allow
       execution at creation, then apply the guest's exact access immediately;
       this permits code pages to transition from writable to executable. */
    void *p=MapViewOfFile3(win_section,GetCurrentProcess(),(void *)address,phys,PAGE,
                         MEM_REPLACE_PLACEHOLDER,PAGE_EXECUTE_READWRITE,NULL,0);
    if (!p) return -1;
    DWORD old;
    if (!VirtualProtect(p,PAGE,prot,&old)) {
        UnmapViewOfFile2(GetCurrentProcess(),p,MEM_PRESERVE_PLACEHOLDER);
        return -1;
    }
    if ((prot==PAGE_EXECUTE_READ || prot==PAGE_EXECUTE_READWRITE) &&
        !FlushInstructionCache(GetCurrentProcess(),p,PAGE)) {
        UnmapViewOfFile2(GetCurrentProcess(),p,MEM_PRESERVE_PLACEHOLDER);
        return -1;
    }
    return 0;
}
typedef struct { uint64_t phys; DWORD protection[4]; int mapped; } WinPrevious;
static int host_map(uintptr_t address, uint64_t size, int prot, int kind, uint64_t phys) {
    if (win_reserve_range(address,size)) return -1;
    if (kind!=KIND_RESERVED &&
        (win_pool() || !VirtualAlloc(backing_base+phys,size,MEM_COMMIT,PAGE_READWRITE))) return -1;
    uint64_t pages=size/PAGE;
    WinPrevious *previous=calloc((size_t)pages,sizeof(*previous));
    if (!previous) return -1;
    /* Snapshot actual 4 KiB protections too: GPU tracking can temporarily
       restrict access more than the guest VMA's recorded protection. */
    for (uint64_t i=0;i<pages;++i) {
        uintptr_t at=address+i*PAGE;
        size_t v=vma_index(at);
        if (v<vma_count && vmas[v].start<=at && vmas[v].kind!=KIND_RESERVED) {
            previous[i].mapped=1;
            previous[i].phys=vmas[v].phys+at-vmas[v].start;
        }
        for (int j=0;j<4;++j) {
            MEMORY_BASIC_INFORMATION m;
            if (!VirtualQuery((void *)(at+(uintptr_t)j*4096),&m,sizeof(m)) ||
                (m.State==MEM_COMMIT && m.Type!=MEM_MAPPED)) { free(previous); return -1; }
            previous[i].protection[j]=m.Protect;
        }
    }
    uint64_t done=0;
    for (;done<pages;++done) {
        uintptr_t at=address+done*PAGE;
        if (win_placeholder(at) || (kind!=KIND_RESERVED && win_map_page(at,phys+done*PAGE,win_protection(host_prot(prot))))) break;
    }
    if (done<pages) {
        /* Restore all touched views, including a page whose old view was
           removed but whose replacement failed. Metadata is still unchanged. */
        for (uint64_t i=0;i<=done;++i) {
            uintptr_t at=address+i*PAGE;
            if (win_placeholder(at) || (previous[i].mapped && win_map_page(at,previous[i].phys,PAGE_READWRITE))) {
                fputs("STOP: Windows guest memory rollback failed\n",stderr); abort();
            }
            if (previous[i].mapped) for (int j=0;j<4;++j) {
                DWORD old;
                if (!VirtualProtect((void *)(at+(uintptr_t)j*4096),4096,previous[i].protection[j],&old)) {
                    fputs("STOP: Windows guest protection rollback failed\n",stderr); abort();
                }
            }
        }
    }
    free(previous);
    return done==pages ? 0 : -1;
}
static int host_unmap(uintptr_t address, uint64_t size) {
    for (uint64_t i=0;i<size;i+=PAGE) if (win_placeholder(address+i)) return -1;
    return 0;
}
static int host_protect(uintptr_t address, uint64_t size, int prot) {
    /* VirtualProtect cannot span separately allocated mapped views. GPU
       restrictions can start/end on 4 KiB boundaries within a PS4 page. */
    for (uint64_t done=0;done<size;) {
        uintptr_t at=address+done;
        uint64_t n=PAGE-(at%PAGE);
        if (n>size-done) n=size-done;
        DWORD old;
        if (!VirtualProtect((void *)at,n,win_protection(prot),&old)) return -1;
        done+=n;
    }
    if ((prot & PROT_EXEC) && !FlushInstructionCache(GetCurrentProcess(),(void *)address,(SIZE_T)size)) return -1;
    return 0;
}
static void discard_backing(uint64_t phys, uint64_t size) {
    /* SEC_RESERVE pages cannot be decommitted individually. Zero only pages
       already committed, retaining sparse commitment for untouched ranges. */
    for (uint64_t done=0;done<size;) {
        MEMORY_BASIC_INFORMATION m;
        unsigned char *at=backing_base+phys+done;
        if (!VirtualQuery(at,&m,sizeof(m))) { fputs("STOP: Windows backing query failed\n",stderr); abort(); }
        uint64_t n=(uintptr_t)m.BaseAddress+m.RegionSize-(uintptr_t)at;
        if (n>size-done) n=size-done;
        if (m.State==MEM_COMMIT) memset(at,0,(size_t)n);
        done+=n;
    }
}
#endif
