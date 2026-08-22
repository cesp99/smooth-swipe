/*
    Smooth Swipe - macOS-like virtual desktop switching for KWin

    SPDX-FileCopyrightText: 2026 Carlo Esposito <carlo@aploi.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "smoothswipe_config.h"

// KConfigSkeleton
#include "smoothswipeconfig.h"

#include <kwineffects_interface.h>

#include <KPluginFactory>

K_PLUGIN_CLASS(KWin::SmoothSwipeEffectConfig)

namespace KWin
{

SmoothSwipeEffectConfig::SmoothSwipeEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    m_ui.setupUi(widget());
    SmoothSwipeConfig::instance(QStringLiteral("kwinrc"));
    addConfig(SmoothSwipeConfig::self(), widget());
}

SmoothSwipeEffectConfig::~SmoothSwipeEffectConfig()
{
}

void SmoothSwipeEffectConfig::save()
{
    KCModule::save();

    OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                         QStringLiteral("/Effects"),
                                         QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("smoothswipe"));
}

} // namespace KWin

#include "smoothswipe_config.moc"

#include "moc_smoothswipe_config.cpp"
