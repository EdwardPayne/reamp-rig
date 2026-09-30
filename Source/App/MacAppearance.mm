#include "MacAppearance.h"

#import <AppKit/AppKit.h>

namespace rf::app
{
    void useDarkAppearance()
    {
        [NSApp setAppearance: [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua]];
    }
}
