/*
    Smooth Swipe - macOS-like virtual desktop switching for KWin

    Forked from KWin's Slide effect.

    SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2008 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2026 Carlo Esposito <carlo@aploi.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// own
#include "smoothswipe.h"
#include "core/output.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"

// KConfigSkeleton
#include "smoothswipeconfig.h"

#include <algorithm>
#include <cmath>

namespace KWin
{

// Release velocities are capped so a spurious input event can never fling the view
// across the whole grid.
static constexpr qreal s_maxReleaseSpeed = 10.0; // desktops per second

// Spring constants used while the fingers are down: the anchor follows the gesture
// position and the view eases towards it. Stiff enough that the lag is imperceptible
// (~18 ms time constant), soft enough to filter touchpad jitter and the misalignment
// between input event timing and display refresh.
static constexpr qreal s_trackingStiffness = 3000.0;
static constexpr qreal s_trackingDampingRatio = 1.0;

SmoothSwipeEffectScreen::SmoothSwipeEffectScreen(SmoothSwipeEffect *parent, LogicalOutput *screen)
    : m_parent(parent)
    , m_screen(screen)
{
    reconfigure();
}

SmoothSwipeEffect::SmoothSwipeEffect()
{
    SmoothSwipeConfig::instance(effects->config());
    reconfigure(ReconfigureAll);

    connect(effects, &EffectsHandler::desktopChanged,
            this, &SmoothSwipeEffect::desktopChanged);
    connect(effects, &EffectsHandler::desktopChanging,
            this, &SmoothSwipeEffect::desktopChanging);
    connect(effects, QOverload<>::of(&EffectsHandler::desktopChangingCancelled),
            this, &SmoothSwipeEffect::desktopChangingCancelled);
    connect(effects, &EffectsHandler::windowAdded,
            this, &SmoothSwipeEffect::windowAdded);
    connect(effects, &EffectsHandler::windowDeleted,
            this, &SmoothSwipeEffect::windowDeleted);
    connect(effects, &EffectsHandler::desktopAdded,
            this, &SmoothSwipeEffect::finishedSwitching);
    connect(effects, &EffectsHandler::desktopRemoved,
            this, &SmoothSwipeEffect::finishedSwitching);
    connect(effects, &EffectsHandler::screenAdded,
            this, &SmoothSwipeEffect::finishedSwitching);
    connect(effects, &EffectsHandler::screenRemoved, this, [this](LogicalOutput *screen) {
        m_effectScreens.remove(screen);
        finishedSwitching();
    });
    connect(effects, &EffectsHandler::currentActivityAboutToChange, this, [this]() {
        m_switchingActivity = true;
    });
    connect(effects, &EffectsHandler::currentActivityChanged, this, [this]() {
        m_switchingActivity = false;
    });
}

SmoothSwipeEffect::~SmoothSwipeEffect()
{
    finishedSwitching();
}

SmoothSwipeEffectScreen::~SmoothSwipeEffectScreen()
{
    finishedSwitching();
}

bool SmoothSwipeEffect::supported()
{
    return effects->animationsSupported();
}

void SmoothSwipeEffect::reconfigure(ReconfigureFlags)
{
    SmoothSwipeConfig::self()->read();

    for (SmoothSwipeEffectScreen &effectScreen : m_effectScreens) {
        effectScreen.reconfigure();
    }

    m_hGap = SmoothSwipeConfig::horizontalGap();
    m_vGap = SmoothSwipeConfig::verticalGap();
    m_slideBackground = SmoothSwipeConfig::slideBackground();
    m_velocityTransfer = SmoothSwipeConfig::velocityTransfer() / 100.0;
}

void SmoothSwipeEffectScreen::reconfigure()
{
    const qreal springConstant = SmoothSwipeConfig::springStiffness() / effects->animationTimeFactor();
    const qreal dampingRatio = SmoothSwipeConfig::dampingRatio();

    m_motionX = SpringMotion(springConstant, dampingRatio);
    m_motionY = SpringMotion(springConstant, dampingRatio);
}

void SmoothSwipeEffectScreen::setSpringParams(qreal springConstant, qreal dampingRatio)
{
    SpringMotion x(springConstant, dampingRatio);
    x.setPosition(m_motionX.position());
    x.setVelocity(m_motionX.velocity());
    x.setAnchor(m_motionX.anchor());
    m_motionX = x;

    SpringMotion y(springConstant, dampingRatio);
    y.setPosition(m_motionY.position());
    y.setVelocity(m_motionY.velocity());
    y.setAnchor(m_motionY.anchor());
    m_motionY = y;
}

QPointF SmoothSwipeEffectScreen::springPosition() const
{
    const QSize virtualSpaceSize = effects->virtualScreenSize();
    return QPointF(m_motionX.position() / virtualSpaceSize.width(),
                   m_motionY.position() / virtualSpaceSize.height());
}

void SmoothSwipeEffect::prePaintScreen(ScreenPrePaintData &data)
{
    if (data.screen) {
        if (SmoothSwipeEffectScreen *effectScreen = getEffectScreen(data.screen)) {
            effectScreen->prePaintScreen(data);
        }
    }

    effects->prePaintScreen(data);
}

void SmoothSwipeEffectScreen::prePaintScreen(ScreenPrePaintData &data)
{
    if (m_state == State::Inactive) {
        return;
    }
    const QList<VirtualDesktop *> desktops = effects->desktops();
    const int w = effects->desktopGridWidth();
    const int h = effects->desktopGridHeight();

    switch (m_state) {
    case State::Inactive:
        Q_UNREACHABLE();

    case State::ActiveGesture: {
        // The anchor follows the finger; the spring below eases the rendered
        // position towards it, filtering jitter and event/frame misalignment.
        const QSize virtualSpaceSize = effects->virtualScreenSize();
        m_motionX.setAnchor(m_gesturePos.x() * virtualSpaceSize.width());
        m_motionY.setAnchor(m_gesturePos.y() * virtualSpaceSize.height());
        [[fallthrough]];
    }
    case State::ActiveAnimation: {
        const std::chrono::milliseconds timeDelta = m_clock.tick(data.view);

        m_motionX.advance(timeDelta);
        m_motionY.advance(timeDelta);

        m_paintCtx.position = springPosition();
        break;
    }
    }

    // Clipping
    m_paintCtx.visibleDesktops.clear();
    m_paintCtx.visibleDesktops.reserve(4); // 4 - maximum number of visible desktops
    bool includedX = false, includedY = false;
    for (VirtualDesktop *desktop : desktops) {
        const QPoint coords = effects->desktopGridCoords(desktop);
        if (coords.x() % w == (int)(m_paintCtx.position.x()) % w) {
            includedX = true;
        } else if (coords.x() % w == ((int)(m_paintCtx.position.x()) + 1) % w) {
            includedX = true;
        }
        if (coords.y() % h == (int)(m_paintCtx.position.y()) % h) {
            includedY = true;
        } else if (coords.y() % h == ((int)(m_paintCtx.position.y()) + 1) % h) {
            includedY = true;
        }

        if (includedX && includedY) {
            m_paintCtx.visibleDesktops << desktop;
        }
    }

    data.mask |= Effect::PAINT_SCREEN_TRANSFORMED;
}

bool SmoothSwipeEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const Region &deviceRegion, LogicalOutput *screen)
{
    if (SmoothSwipeEffectScreen *effectScreen = getEffectScreen(screen)) {
        effectScreen->paintScreen();
    }
    return effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
}

void SmoothSwipeEffectScreen::paintScreen()
{
    m_paintCtx.wrap = effects->optionRollOverDesktops();
}

QPoint SmoothSwipeEffectScreen::getDrawCoords(QPointF pos, LogicalOutput *screen)
{
    QPoint c = QPoint();
    c.setX(pos.x() * (screen->geometry().width() + m_parent->horizontalGap()));
    c.setY(pos.y() * (screen->geometry().height() + m_parent->verticalGap()));
    return c;
}

/**
 * Decide whether given window @p w should be transformed/translated.
 * @returns @c true if given window @p w should be transformed, otherwise @c false
 */
bool SmoothSwipeEffectScreen::isTranslated(const EffectWindow *w) const
{
    if (m_state == State::Inactive) {
        return false;
    }
    if (w->isOnAllDesktops()) {
        if (w->isDesktop()) {
            return m_parent->slideBackground();
        }
        return false;
    } else if (w == m_movingWindow) {
        return false;
    }
    return true;
}

/**
 * Will a window be painted during this frame?
 */
bool SmoothSwipeEffectScreen::willBePainted(const EffectWindow *w) const
{
    if (m_state == State::Inactive) {
        return true;
    }
    if (w->isOnAllDesktops()) {
        return true;
    }
    if (w == m_movingWindow) {
        return true;
    }
    for (VirtualDesktop *desktop : std::as_const(m_paintCtx.visibleDesktops)) {
        if (w->isOnDesktop(desktop)) {
            return true;
        }
    }
    return false;
}

void SmoothSwipeEffect::prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data)
{
    data.setTransformed();
    effects->prePaintWindow(view, w, data);
}

bool SmoothSwipeEffect::paintWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceGeometry, WindowPaintData &data)
{
    if (SmoothSwipeEffectScreen *effectScreen = getEffectScreen(w->screen())) {
        return effectScreen->paintWindow(renderTarget, viewport, w, mask, deviceGeometry, data);
    } else {
        return effects->paintWindow(renderTarget, viewport, w, mask, deviceGeometry, data);
    }
}

bool SmoothSwipeEffectScreen::paintWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceGeometry, WindowPaintData &data)
{
    if (!willBePainted(w)) {
        return true;
    }

    if (!isTranslated(w)) {
        return effects->paintWindow(renderTarget, viewport, w, mask, deviceGeometry, data);
    }

    const int gridWidth = effects->desktopGridWidth();
    const int gridHeight = effects->desktopGridHeight();

    QPointF drawPosition = forcePositivePosition(m_paintCtx.position);
    drawPosition = m_paintCtx.wrap ? constrainToDrawableRange(drawPosition) : drawPosition;

    // If we're wrapping, draw the desktop in the second position.
    const bool wrappingX = drawPosition.x() > gridWidth - 1;
    const bool wrappingY = drawPosition.y() > gridHeight - 1;

    const auto screens = effects->screens();

    for (VirtualDesktop *desktop : std::as_const(m_paintCtx.visibleDesktops)) {
        if (!w->isOnDesktop(desktop)) {
            continue;
        }
        QPointF desktopTranslation = QPointF(effects->desktopGridCoords(desktop)) - drawPosition;
        // Decide if that first desktop should be drawn at 0 or the higher position used for wrapping.
        if (effects->desktopGridCoords(desktop).x() == 0 && wrappingX) {
            desktopTranslation = QPointF(desktopTranslation.x() + gridWidth, desktopTranslation.y());
        }
        if (effects->desktopGridCoords(desktop).y() == 0 && wrappingY) {
            desktopTranslation = QPointF(desktopTranslation.x(), desktopTranslation.y() + gridHeight);
        }

        for (LogicalOutput *screen : screens) {
            QPoint drawTranslation = getDrawCoords(desktopTranslation, screen);
            data += drawTranslation;

            const Rect screenArea = screen->geometry();
            const Rect logicalDamage = screenArea.translated(drawTranslation).intersected(screenArea);

            if (!effects->paintWindow(
                renderTarget, viewport, w, mask,
                // Only paint the region that intersects the current screen and desktop.
                deviceGeometry.intersected(viewport.mapToDeviceCoordinatesAligned(logicalDamage)),
                data)) {
                return false;
            }

            // Undo the translation for the next screen. I know, it hurts me too.
            data += QPoint(-drawTranslation.x(), -drawTranslation.y());
        }
    }
    return true;
}

void SmoothSwipeEffect::postPaintScreen()
{
    bool allInactive = true;
    for (SmoothSwipeEffectScreen &effectScreen : m_effectScreens) {
        effectScreen.postPaintScreen();
        if (effectScreen.isActive()) {
            allInactive = false;
        }
    }
    if (allInactive) {
        finishedSwitching();
    }

    effects->addRepaintFull();
    effects->postPaintScreen();
}

void SmoothSwipeEffectScreen::postPaintScreen()
{
    if (m_state == State::ActiveAnimation && !m_motionX.isMoving() && !m_motionY.isMoving()) {
        finishedSwitching();
    }
}

/*
 * Negative desktop positions aren't allowed.
 */
QPointF SmoothSwipeEffectScreen::forcePositivePosition(QPointF p) const
{
    if (p.x() < 0) {
        p.setX(p.x() + std::ceil(-p.x() / effects->desktopGridWidth()) * effects->desktopGridWidth());
    }
    if (p.y() < 0) {
        p.setY(p.y() + std::ceil(-p.y() / effects->desktopGridHeight()) * effects->desktopGridHeight());
    }
    return p;
}

bool SmoothSwipeEffectScreen::shouldElevate(const EffectWindow *w) const
{
    // Static docks(i.e. this effect doesn't slide docks) should be elevated
    // so they can properly animate themselves when an user enters or leaves
    // a virtual desktop with a window in fullscreen mode.
    return w->isDock();
}

/*
 * This function is called when the desktop changes.
 * Called AFTER the gesture is released.
 * Sets up animation to round off to the new current desktop.
 */
void SmoothSwipeEffectScreen::startAnimation(const QPointF &oldPos, VirtualDesktop *current, EffectWindow *movingWindow)
{
    const bool wasInactive = m_state == State::Inactive;
    const bool wasGesture = m_state == State::ActiveGesture;
    if (wasInactive) {
        prepareSwitching();
        m_clock.reset();
    }

    m_state = State::ActiveAnimation;
    m_movingWindow = movingWindow;

    m_startPos = oldPos;
    m_endPos = effects->desktopGridCoords(current);
    if (effects->optionRollOverDesktops()) {
        optimizePath();
    }

    const QSize virtualSpaceSize = effects->virtualScreenSize();
    m_motionX.setAnchor(m_endPos.x() * virtualSpaceSize.width());
    m_motionX.setPosition(m_startPos.x() * virtualSpaceSize.width());
    m_motionY.setAnchor(m_endPos.y() * virtualSpaceSize.height());
    m_motionY.setPosition(m_startPos.y() * virtualSpaceSize.height());

    if (wasGesture) {
        // Momentum carry-over: the tracking spring's velocity is the filtered finger
        // velocity, so the release continues the gesture. Scale it by the configured
        // transfer amount and cap it against spurious input.
        const qreal transfer = m_parent->velocityTransfer();
        const qreal maxVx = s_maxReleaseSpeed * virtualSpaceSize.width();
        const qreal maxVy = s_maxReleaseSpeed * virtualSpaceSize.height();
        m_motionX.setVelocity(std::clamp(m_motionX.velocity() * transfer, -maxVx, maxVx));
        m_motionY.setVelocity(std::clamp(m_motionY.velocity() * transfer, -maxVy, maxVy));
    } else if (wasInactive) {
        // Fresh keyboard-driven switch: don't reuse a stale velocity from whatever
        // animation ran last.
        m_motionX.setVelocity(0);
        m_motionY.setVelocity(0);
    }
    // Otherwise (retarget while already animating) the spring keeps its current
    // velocity, so chained switches stay continuous.

    // Switch to the configured release spring constants (no-op if already animating).
    setSpringParams(SmoothSwipeConfig::springStiffness() / effects->animationTimeFactor(),
                    SmoothSwipeConfig::dampingRatio());

    effects->addRepaint(m_screen->geometry());
}

void SmoothSwipeEffectScreen::prepareSwitching()
{
    const auto windows = effects->stackingOrder();
    m_windowData.reserve(windows.count());

    for (EffectWindow *w : windows) {
        if (w->screen() != m_screen) {
            continue;
        }
        m_windowData[w] = WindowData{
            .visibilityRef = EffectWindowVisibleRef(w, EffectWindow::PAINT_DISABLED_BY_DESKTOP),
        };

        if (shouldElevate(w)) {
            effects->setElevatedWindow(w, true);
            m_elevatedWindows << w;
        }
    }
}

void SmoothSwipeEffect::finishedSwitching()
{
    for (SmoothSwipeEffectScreen &effectScreen : m_effectScreens) {
        effectScreen.finishedSwitching();
    }
    m_effectScreens.clear();
    const QList<EffectWindow *> windows = effects->stackingOrder();
    for (EffectWindow *w : windows) {
        w->setData(WindowForceBackgroundContrastRole, QVariant());
        w->setData(WindowForceBlurRole, QVariant());
    }
    effects->setActiveFullScreenEffect(nullptr);
}

void SmoothSwipeEffectScreen::finishedSwitching()
{
    if (m_state == State::Inactive) {
        return;
    }

    for (EffectWindow *w : std::as_const(m_elevatedWindows)) {
        effects->setElevatedWindow(w, false);
    }
    m_elevatedWindows.clear();

    m_windowData.clear();
    m_movingWindow = nullptr;
    m_state = State::Inactive;
}

void SmoothSwipeEffect::desktopChanged(VirtualDesktop *old, VirtualDesktop *current, EffectWindow *with, LogicalOutput *screen)
{
    if (m_switchingActivity || (effects->hasActiveFullScreenEffect() && effects->activeFullScreenEffect() != this)) {
        return;
    }
    if (!isActive()) {
        const QList<EffectWindow *> windows = effects->stackingOrder();
        for (EffectWindow *w : windows) {
            w->setData(WindowForceBackgroundContrastRole, QVariant(true));
            w->setData(WindowForceBlurRole, QVariant(true));
        }
    }

    auto effectScreenResult = m_effectScreens.tryEmplace(screen, this, screen);
    effectScreenResult.iterator->desktopChanged(old, current, with);
    effects->setActiveFullScreenEffect(this);
}

void SmoothSwipeEffectScreen::desktopChanged(VirtualDesktop *old, VirtualDesktop *current, EffectWindow *with)
{
    QPointF previousPos;
    switch (m_state) {
    case State::Inactive:
        previousPos = effects->desktopGridCoords(old);
        break;

    case State::ActiveAnimation:
    case State::ActiveGesture:
        // Continue from the rendered spring position, not the raw gesture position,
        // so the hand-off into the release animation is seamless.
        previousPos = springPosition();
        break;
    }

    startAnimation(previousPos, current, with);
}

void SmoothSwipeEffect::desktopChanging(VirtualDesktop *old, QPointF desktopOffset, EffectWindow *with, LogicalOutput *output)
{
    if (effects->hasActiveFullScreenEffect() && effects->activeFullScreenEffect() != this) {
        return;
    }
    if (!isActive()) {
        const QList<EffectWindow *> windows = effects->stackingOrder();
        for (EffectWindow *w : windows) {
            w->setData(WindowForceBackgroundContrastRole, QVariant(true));
            w->setData(WindowForceBlurRole, QVariant(true));
        }
    }

    auto effectScreenResult = m_effectScreens.tryEmplace(output, this, output);
    effectScreenResult.iterator->desktopChanging(old, desktopOffset, with);
    effects->setActiveFullScreenEffect(this);
}

void SmoothSwipeEffectScreen::desktopChanging(VirtualDesktop *old, QPointF desktopOffset, EffectWindow *with)
{
    const bool wasInactive = m_state == State::Inactive;
    if (wasInactive) {
        prepareSwitching();
        m_clock.reset();
    }

    m_state = State::ActiveGesture;
    m_movingWindow = with;

    // Find desktop position based on animationDelta
    QPoint gridPos = effects->desktopGridCoords(old);
    m_gesturePos.setX(gridPos.x() + desktopOffset.x());
    m_gesturePos.setY(gridPos.y() + desktopOffset.y());

    if (effects->optionRollOverDesktops()) {
        m_gesturePos = forcePositivePosition(m_gesturePos);
    } else {
        m_gesturePos = moveInsideDesktopGrid(m_gesturePos);
    }

    // Follow the finger through a stiff spring; anchors update in prePaintScreen().
    setSpringParams(s_trackingStiffness, s_trackingDampingRatio);
    if (wasInactive) {
        // Start the spring exactly at the finger so there's no catch-up wobble.
        const QSize virtualSpaceSize = effects->virtualScreenSize();
        m_motionX.setPosition(m_gesturePos.x() * virtualSpaceSize.width());
        m_motionX.setVelocity(0);
        m_motionY.setPosition(m_gesturePos.y() * virtualSpaceSize.height());
        m_motionY.setVelocity(0);
    }
    // Grabbing mid-animation keeps the spring's position and velocity: the view is
    // caught where it visually is and eases to the finger from there.

    effects->addRepaint(m_screen->geometry());
}

void SmoothSwipeEffect::desktopChangingCancelled()
{
    // If the fingers have been lifted and the current desktop didn't change, start animation
    // to move back to the original virtual desktop.
    if (effects->activeFullScreenEffect() == this) {
        for (SmoothSwipeEffectScreen &effectScreen : m_effectScreens) {
            effectScreen.desktopChangingCancelled();
        }
    }
}

void SmoothSwipeEffectScreen::desktopChangingCancelled()
{
    if (m_state != State::Inactive) {
        startAnimation(springPosition(), effects->currentDesktop(m_screen), nullptr);
    }
}

QPointF SmoothSwipeEffectScreen::moveInsideDesktopGrid(QPointF p)
{
    if (p.x() < 0) {
        p.setX(0);
    }
    if (p.y() < 0) {
        p.setY(0);
    }
    if (p.x() > effects->desktopGridWidth() - 1) {
        p.setX(effects->desktopGridWidth() - 1);
    }
    if (p.y() > effects->desktopGridHeight() - 1) {
        p.setY(effects->desktopGridHeight() - 1);
    }
    return p;
}

void SmoothSwipeEffect::windowAdded(EffectWindow *w)
{
    if (SmoothSwipeEffectScreen *effectScreen = getEffectScreen(w->screen())) {
        effectScreen->windowAdded(w);
    }
    if (isActive()) {
        w->setData(WindowForceBackgroundContrastRole, QVariant(true));
        w->setData(WindowForceBlurRole, QVariant(true));
    }
}

void SmoothSwipeEffectScreen::windowAdded(EffectWindow *w)
{
    if (m_state == State::Inactive) {
        return;
    }
    if (shouldElevate(w)) {
        effects->setElevatedWindow(w, true);
        m_elevatedWindows << w;
    }

    m_windowData[w] = WindowData{
        .visibilityRef = EffectWindowVisibleRef(w, EffectWindow::PAINT_DISABLED_BY_DESKTOP),
    };
}

void SmoothSwipeEffect::windowDeleted(EffectWindow *w)
{
    // It's necessary to delete it in all screens, because the window may have changed screens since it was added. This happens e.g. with the desktop change OSD
    // window if the effect is triggered on both screens consecutively: the window is added on the first screen and deleted on the second screen.
    for (SmoothSwipeEffectScreen &effectScreen : m_effectScreens) {
        effectScreen.windowDeleted(w);
    }
}

void SmoothSwipeEffectScreen::windowDeleted(EffectWindow *w)
{
    if (m_state == State::Inactive) {
        return;
    }
    if (w == m_movingWindow) {
        m_movingWindow = nullptr;
    }
    m_elevatedWindows.removeAll(w);
    m_windowData.remove(w);
}

/*
 * Find the fastest path between two desktops.
 * This function decides when it's better to wrap around the grid or not.
 * Only call if wrapping is enabled.
 */
void SmoothSwipeEffectScreen::optimizePath()
{
    int w = effects->desktopGridWidth();
    int h = effects->desktopGridHeight();

    // Keep coordinates as low as possible
    if (m_startPos.x() >= w && m_endPos.x() >= w) {
        m_startPos.setX(fmod(m_startPos.x(), w));
        m_endPos.setX(fmod(m_endPos.x(), w));
    }
    if (m_startPos.y() >= h && m_endPos.y() >= h) {
        m_startPos.setY(fmod(m_startPos.y(), h));
        m_endPos.setY(fmod(m_endPos.y(), h));
    }

    // Is there is a shorter possible route?
    // If the x distance to be traveled is more than half the grid width, it's faster to wrap.
    // To avoid negative coordinates, take the lower coordinate and raise.
    if (std::abs((m_startPos.x() - m_endPos.x())) > w / 2.0) {
        if (m_startPos.x() < m_endPos.x()) {
            while (m_startPos.x() < m_endPos.x()) {
                m_startPos.setX(m_startPos.x() + w);
            }
        } else {
            while (m_endPos.x() < m_startPos.x()) {
                m_endPos.setX(m_endPos.x() + w);
            }
        }
        // Keep coordinates as low as possible
        if (m_startPos.x() >= w && m_endPos.x() >= w) {
            m_startPos.setX(fmod(m_startPos.x(), w));
            m_endPos.setX(fmod(m_endPos.x(), w));
        }
    }

    // Same for y
    if (std::abs((m_endPos.y() - m_startPos.y())) > (double)h / (double)2) {
        if (m_startPos.y() < m_endPos.y()) {
            while (m_startPos.y() < m_endPos.y()) {
                m_startPos.setY(m_startPos.y() + h);
            }
        } else {
            while (m_endPos.y() < m_startPos.y()) {
                m_endPos.setY(m_endPos.y() + h);
            }
        }
        // Keep coordinates as low as possible
        if (m_startPos.y() >= h && m_endPos.y() >= h) {
            m_startPos.setY(fmod(m_startPos.y(), h));
            m_endPos.setY(fmod(m_endPos.y(), h));
        }
    }
}

/*
 * Takes the point and uses modulus to keep draw position within [0, desktopGridWidth]
 * The render loop will draw the first desktop (0) after the last one (at position desktopGridWidth) for the wrap animation.
 */
QPointF SmoothSwipeEffectScreen::constrainToDrawableRange(QPointF p)
{
    p.setX(fmod(p.x(), effects->desktopGridWidth()));
    p.setY(fmod(p.y(), effects->desktopGridHeight()));
    return p;
}

SmoothSwipeEffectScreen *SmoothSwipeEffect::getEffectScreen(LogicalOutput *screen)
{
    auto it = m_effectScreens.find(screen);
    if (it == m_effectScreens.end()) {
        return nullptr;
    }
    return &(*it);
}

} // namespace KWin

#include "moc_smoothswipe.cpp"
