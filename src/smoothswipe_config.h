/*
    Smooth Swipe - macOS-like virtual desktop switching for KWin

    SPDX-FileCopyrightText: 2026 Carlo Esposito <carlo@aploi.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ui_smoothswipe_config.h"
#include <KCModule>

namespace KWin
{

class SmoothSwipeEffectConfig : public KCModule
{
    Q_OBJECT

public:
    explicit SmoothSwipeEffectConfig(QObject *parent, const KPluginMetaData &data);
    ~SmoothSwipeEffectConfig() override;

    void save() override;

private:
    ::Ui::SmoothSwipeEffectConfig m_ui;
};

} // namespace KWin
