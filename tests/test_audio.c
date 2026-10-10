/* Production audio and decoder exports; dummy SDL device, no game or speakers. */
#include "runtime.h"
#include "runtime_clock.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int32_t (ABI *AudioInit)(void);
typedef int32_t (ABI *AudioOpen)(int32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t);
typedef int32_t (ABI *AudioOutput)(int32_t,const void *);
typedef int32_t (ABI *AudioClose)(int32_t);
typedef int32_t (ABI *AudioVolume)(int32_t,int32_t,const int32_t *);
#define AUDIO(type,nid) ((type)runtime_audio_resolve(nid))

static void decoder_lifecycle(void) {
    typedef int32_t (ABI *Init)(int64_t,uint32_t *);
    typedef int32_t (ABI *Register)(uint32_t,uint32_t,int64_t);
    typedef int32_t (ABI *Finalize)(uint32_t);
    Init init=(Init)runtime_ajm_resolve("dl+4eHSzUu4#N#O");
    Register reg=(Register)runtime_ajm_resolve("Q3dyFuwGn64#N#O");
    Finalize done=(Finalize)runtime_ajm_resolve("MHur6qCsUus#N#O");
    assert(init && reg && done && !runtime_ajm_resolve("dl+4eHSzUu4#q#q"));
    struct { uint32_t id,canary; } c={0,0xabcdef};
    assert((uint32_t)init(0,NULL)==0x80930005);
    assert(init(0,&c.id)==0 && c.id && c.canary==0xabcdef);
    assert(reg(c.id,1,0)==0 && done(c.id)==0);
    assert((uint32_t)done(c.id)==0x80930002);
}

int main(int argc,char **argv) {
    int silent=argc>1 && !strcmp(argv[1],"--silent");
    assert(SDL_setenv_unsafe("SDL_AUDIODRIVER","dummy",1)==0);
    assert(SDL_setenv_unsafe("BB_AUDIO",silent ? "none" : "sdl",1)==0);
#ifdef _WIN32
    /* SDL's prebuilt DLL may use a different CRT environment from this EXE. */
    assert(_putenv_s("BB_AUDIO",silent ? "none" : "sdl")==0);
#endif
    AudioInit init=AUDIO(AudioInit,"JfEPXVxhFqA#G#H");
    AudioOpen open=AUDIO(AudioOpen,"ekNvsT22rsY#G#H");
    AudioOutput output=AUDIO(AudioOutput,"QOQtbeDqsT4#G#H");
    AudioClose close=AUDIO(AudioClose,"s1--uE9mBFw#G#H");
    AudioVolume volume=AUDIO(AudioVolume,"b+uAV89IlxE#G#H");
    assert(init && open && output && close && volume);
    assert(!runtime_audio_resolve("JfEPXVxhFqA#q#q"));
    assert((uint32_t)open(1,0,0,256,48000,1)==0x8026000f);
    assert(init()==0);
    assert((uint32_t)open(1,0,0,1,48000,1)==0x80260006);
    assert((uint32_t)open(1,0,0,256,44100,1)==0x80260008);
    int32_t handle=open(1,0,0,256,48000,4); /* float, stereo */
    assert(handle>=0);
    if (!silent) assert(SDL_WasInit(SDL_INIT_AUDIO) && !strcmp(SDL_GetCurrentAudioDriver(),"dummy"));
    float samples[512]={0}; int32_t gains[8]={32768,32768};
    assert(volume(handle,3,gains)==0);
    gains[0]=-1; assert((uint32_t)volume(handle,1,gains)==0x80260009);
    uint64_t start=runtime_monotonic_ns();
    for (int i=0;i<32;++i) assert(output(handle,samples)==512);
    /* 31 paced intervals at 256/48000 seconds (~165 ms); broad upper bound
       accommodates a loaded test machine without accepting an unpaced loop. */
    uint64_t elapsed=runtime_monotonic_ns()-start;
    assert(elapsed>=UINT64_C(120000000) && elapsed<UINT64_C(5000000000));
    assert(output(handle,NULL)==0 && close(handle)==0);
    assert((uint32_t)output(handle,samples)==0x80260001);
    decoder_lifecycle(); SDL_Quit();
    printf("PASS: audio SysV exports, %s pacing and AJM lifecycle\n",silent ? "silent" : "dummy-device");
    return 0;
}
