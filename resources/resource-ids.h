#pragma once

#define IDI_APP                101

// v0.2: Lucide icon font bundled as RT_RCDATA so the toolbar glyphs
// look the same regardless of which Windows version the user is on
// (don't depend on Segoe MDL2 Assets installation / version).
#define IDR_LUCIDE_FONT        200
#define RT_LUCIDE_FONT_TYPE    256  // arbitrary custom RT_; not RT_FONT
                                    // (Win32 RT_FONT requires PE FNT, not TTF)
