#include "kern_donor.hpp"
// Internal 20A2314a donor; enabled only by the explicit DP1 bring-up path.
static const char *pathIntelTGL[] { "//Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics" };
static const char *pathIntelTGLFb[] { "//Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer" };
KernelPatcher::KextInfo kextIntelTGL { "com.apple.driver.AppleIntelTGLGraphics", pathIntelTGL, arrsize(pathIntelTGL), {}, {}, KernelPatcher::KextInfo::Unloaded };
KernelPatcher::KextInfo kextIntelTGLFb { "com.apple.driver.AppleIntelTGLGraphicsFramebuffer", pathIntelTGLFb, arrsize(pathIntelTGLFb), {}, {}, KernelPatcher::KextInfo::Unloaded };

static const char *pathAppleIntelTGLCompat[] { "//Library/Extensions/AppleIntelTGLCompat.kext/Contents/MacOS/AppleIntelTGLCompat" };
KernelPatcher::KextInfo kextAppleIntelTGLCompat { "org.b00t0x.AppleIntelTGLCompat", pathAppleIntelTGLCompat, arrsize(pathAppleIntelTGLCompat), {}, {}, KernelPatcher::KextInfo::Unloaded };

