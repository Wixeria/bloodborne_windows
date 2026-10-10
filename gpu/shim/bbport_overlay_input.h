// Window input does not depend on Vulkan presentation types.
#pragma once
union SDL_Event;
struct SDL_Window;
namespace BbOverlay {
bool HandleEvent(const SDL_Event& event);
void UpdateTextInput(SDL_Window* window);
}
