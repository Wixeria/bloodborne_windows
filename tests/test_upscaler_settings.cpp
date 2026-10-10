// Real settings load and capability fallback, without a GPU or game files.
#include <cassert>
#include <cstdlib>
#include <cstring>
#include "bbport_settings.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    static_assert(BbSettings::UpscalerTaa == 4); // preserve existing stored preset IDs
    auto& settings = BbSettings::Get();
    BbSettings::Load();
    const bool supported = std::strcmp(argv[1], "supported") == 0;
    const int requested = settings.upscaler;
    assert(std::strcmp(BbSettings::UpscalerName(requested), std::getenv("BB_UPSCALER")) == 0);
    assert(settings.preset == BbSettings::Quality);
    BbSettings::ConfigureUpscalerSupport(false, false, supported);
    assert(settings.dlss_supported == supported);
    if (requested == BbSettings::UpscalerDlss && supported) {
        assert(settings.upscaler == BbSettings::UpscalerDlss);
        assert(BbSettings::IsFrameUpscaler(settings.upscaler));
    } else if (requested == BbSettings::UpscalerDlss || requested == BbSettings::UpscalerFsr411) {
        assert(settings.upscaler == BbSettings::UpscalerFsr3);
        assert(settings.fsr4_problem != nullptr);
    } else {
        assert(settings.upscaler == requested);
    }
}
