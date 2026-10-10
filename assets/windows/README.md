The supplied Bloodborne rune is embedded into the launcher and game executables
as Windows icon resource 1. The original JPEG is preserved as bloodborne-logo.jpg;
the ICO retains its black background and white mark.

The checked-in ICO includes 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel images.
Builds use this asset directly, without requiring ImageMagick or loose icon files.
To regenerate after replacing the source image:

```powershell
magick assets/windows/bloodborne-logo.jpg -define icon:auto-resize=256,128,64,48,40,32,24,20,16 assets/windows/bloodborne.ico
```
