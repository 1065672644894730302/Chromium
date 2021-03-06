// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_LAYOUT_H_
#define UI_BASE_LAYOUT_H_
#pragma once

#include "ui/base/ui_export.h"

namespace ui {

enum DisplayLayout {
  // Layout optimized for ASH.  This enum value should go away as soon as
  // LAYOUT_DESKTOP and LAYOUT_ASH are the same.
  LAYOUT_ASH,

  // The typical layout for e.g. Windows, Mac and Linux.
  LAYOUT_DESKTOP,

  // Layout optimized for touch.  Used e.g. for Windows 8 Metro mode.
  LAYOUT_TOUCH,
};

// Returns the display layout that should be used.  This could be used
// e.g. to tweak hard-coded padding that's layout specific, or choose
// the .pak file of theme resources to load.
UI_EXPORT DisplayLayout GetDisplayLayout();

// Supported UI scale factors for the platform. This is used as an index
// into the array |kScaleFactorScales| which maps the enum value to a float.
enum ScaleFactor {
  SCALE_FACTOR_100P = 0,

  // The scale factor used for unscaled binary data, the 1x (default) scale
  // factor data packs.
  SCALE_FACTOR_NONE = SCALE_FACTOR_100P,

  SCALE_FACTOR_200P,
};

// Returns the float scale value for |scale_factor|.
UI_EXPORT float GetScaleFactorScale(ScaleFactor scale_factor);

}  // namespace ui

#endif  // UI_BASE_LAYOUT_H_
