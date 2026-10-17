/*
    Smooth Swipe - macOS-like virtual desktop switching for KWin

    Forked from KWin's Slide effect.

    SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2008 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2026 Carlo Esposito <carlo@aploi.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

// kwineffects
#include "effect/effect.h"
#include "effect/effectwindow.h"
#include "effect/timeline.h"

#include "springmotion.h"

namespace KWin
{

/*
 * How it works (inherited from Slide):
 *
 * This effect doesn't change the current desktop, it only receives changes from the
 * VirtualDesktopManager. The two visually relevant inputs are desktopChanged() and
 * desktopChanging().
 *
 * While a gesture is in progress, desktopChanging() drives the draw position 1:1 with
 * the fingers. When the fingers are lifted, desktopChanged() (or
 * desktopChangingCancelled()) starts a spring animation towards the target desktop.
 *
 * What Smooth Swipe adds over Slide:
 *
 * 1. One continuous spring simulation across the whole interaction. While the fingers
 *    are down, the spring's anchor follows the gesture position and is advanced every
 *    frame with a very stiff tracking spring — filtering touchpad jitter and the
 *    misalignment between input events and display refresh. On release, the anchor
 *    retargets to the destination desktop and the spring switches to the configured
 *    release constants, keeping position and velocity. Stock Slide instead hard-locks
 *    to raw gesture events and starts the release animation from zero velocity.
 * 2. Configurable spring stiffness / damping ratio / momentum transfer.
 *
 * Positions use desktops as the unit (1.2 = desktop index 1, shifted 0.2 towards 2).
 * The spring simulation runs in virtual-screen pixels.
 */

class SmoothSwipeEffect;

class SmoothSwipeEffectScreen
{
public:
    SmoothSwipeEffectScreen(SmoothSwipeEffect *parent, LogicalOutput *screen);
    ~SmoothSwipeEffectScreen();
    void reconfigure();

    void prePaintScreen(ScreenPrePaintData &data);
    void paintScreen();
    void postPaintScreen();

    bool paintWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceGeometry, WindowPaintData &data);

    bool isActive() const;
    void desktopChanged(VirtualDesktop *old, VirtualDesktop *current, EffectWindow *with);
    void desktopChanging(VirtualDesktop *old, QPointF desktopOffset, EffectWindow *with);
    void desktopChangingCancelled();
    void windowAdded(EffectWindow *w);
    void windowDeleted(EffectWindow *w);
    void finishedSwitching();

private:
    QPoint getDrawCoords(QPointF pos, LogicalOutput *screen);
    bool isTranslated(const EffectWindow *w) const;
    bool willBePainted(const EffectWindow *w) const;
    bool shouldElevate(const EffectWindow *w) const;
    QPointF moveInsideDesktopGrid(QPointF p);
    QPointF constrainToDrawableRange(QPointF p);
    QPointF forcePositivePosition(QPointF p) const;
    void optimizePath(); // Find the best path to target desktop

    // Swap the springs' constants while preserving position, velocity and anchor.
    void setSpringParams(qreal springConstant, qreal dampingRatio);
    // Current spring position in desktop units.
    QPointF springPosition() const;

    void startAnimation(const QPointF &oldPos, VirtualDesktop *current, EffectWindow *movingWindow = nullptr);
    void prepareSwitching();

private:
    enum class State {
        Inactive,
        ActiveAnimation,
        ActiveGesture,
    };

    State m_state = State::Inactive;
    SpringMotion m_motionX;
    SpringMotion m_motionY;

    // When not in a gesture, these two control the animation path (desktops as unit).
    QPointF m_startPos;
    QPointF m_endPos;

    QPointF m_gesturePos;

    EffectWindow *m_movingWindow = nullptr;
    AnimationClock m_clock;

    struct
    {
        QPointF position;
        bool wrap;
        QList<VirtualDesktop *> visibleDesktops;
    } m_paintCtx;

    struct WindowData
    {
        EffectWindowVisibleRef visibilityRef;
    };

    QList<EffectWindow *> m_elevatedWindows;
    QHash<EffectWindow *, WindowData> m_windowData;
    SmoothSwipeEffect *m_parent;
    LogicalOutput *m_screen;
};

class SmoothSwipeEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int horizontalGap READ horizontalGap)
    Q_PROPERTY(int verticalGap READ verticalGap)
    Q_PROPERTY(bool slideBackground READ slideBackground)

public:
    SmoothSwipeEffect();
    ~SmoothSwipeEffect() override;

    void reconfigure(ReconfigureFlags) override;

    void prePaintScreen(ScreenPrePaintData &data) override;
    bool paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const Region &deviceRegion, LogicalOutput *screen) override;
    void postPaintScreen() override;

    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override;
    bool paintWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceGeometry, WindowPaintData &data) override;

    bool isActive() const override;
    int requestedEffectChainPosition() const override;

    static bool supported();

    int horizontalGap() const;
    int verticalGap() const;
    bool slideBackground() const;
    qreal velocityTransfer() const;

private Q_SLOTS:
    void desktopChanged(VirtualDesktop *old, VirtualDesktop *current, EffectWindow *with, LogicalOutput *output);
    void desktopChanging(VirtualDesktop *old, QPointF desktopOffset, EffectWindow *with, LogicalOutput *output);
    void desktopChangingCancelled();
    void windowAdded(EffectWindow *w);
    void windowDeleted(EffectWindow *w);

private:
    void finishedSwitching();
    SmoothSwipeEffectScreen *getEffectScreen(LogicalOutput *screen);

private:
    int m_hGap;
    int m_vGap;
    bool m_slideBackground;
    qreal m_velocityTransfer; // fraction of finger velocity carried into the release spring

    bool m_switchingActivity = false;
    QHash<LogicalOutput *, SmoothSwipeEffectScreen> m_effectScreens;
};

inline int SmoothSwipeEffect::horizontalGap() const
{
    return m_hGap;
}

inline int SmoothSwipeEffect::verticalGap() const
{
    return m_vGap;
}

inline bool SmoothSwipeEffect::slideBackground() const
{
    return m_slideBackground;
}

inline qreal SmoothSwipeEffect::velocityTransfer() const
{
    return m_velocityTransfer;
}

inline bool SmoothSwipeEffect::isActive() const
{
    return std::ranges::any_of(m_effectScreens, &SmoothSwipeEffectScreen::isActive);
}

inline bool SmoothSwipeEffectScreen::isActive() const
{
    return m_state != State::Inactive;
}

inline int SmoothSwipeEffect::requestedEffectChainPosition() const
{
    return 50;
}

} // namespace KWin
