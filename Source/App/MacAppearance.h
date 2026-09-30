#pragma once

namespace rf::app
{
   #if defined (__APPLE__)
    /** Forces the dark Aqua appearance so the native title bar and system panels match the
        black UI regardless of the user's light/dark system setting. */
    void useDarkAppearance();
   #else
    inline void useDarkAppearance() {}
   #endif
}
