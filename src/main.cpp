/*
    Smooth Swipe - macOS-like virtual desktop switching for KWin

    SPDX-FileCopyrightText: 2026 Carlo Esposito <carlo@aploi.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "smoothswipe.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(SmoothSwipeEffect,
                              "metadata.json",
                              return SmoothSwipeEffect::supported();)

} // namespace KWin

#include "main.moc"
