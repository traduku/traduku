/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinHotkeyBackend.h"

#include "core/doublepressdetector.h"

#include <QMetaObject>
#include <QtGlobal>

namespace lt {

WinHotkeyBackend::WinHotkeyBackend(HookInstaller installer,
                                   HookUninstaller uninstaller, QObject *parent)
    : IHotkeyBackend(parent)
    , m_detector(std::make_unique<DoublePressDetector>())
    , m_policy(m_detector.get())
    , m_installer(std::move(installer))
    , m_uninstaller(std::move(uninstaller))
{
    // One listener, one meaning: the detector is the same state machine the
    // evdev and X11 backends feed, so "two presses make a gesture" cannot be
    // spelled differently on a second OS.
    connect(m_detector.get(), &DoublePressDetector::activated,
            this, &IHotkeyBackend::activated);
}

WinHotkeyBackend::~WinHotkeyBackend()
{
    stop();
}

void WinHotkeyBackend::setIntervalMs(qint64 ms)
{
    m_policy.setIntervalMs(ms);
}

qint64 WinHotkeyBackend::intervalMs() const
{
    return m_policy.intervalMs();
}

quint64 WinHotkeyBackend::realEvents() const
{
    return m_policy.realCount();
}

quint64 WinHotkeyBackend::injectedEvents() const
{
    return m_policy.injectedCount();
}

bool WinHotkeyBackend::start()
{
    m_error.clear();
    if (m_running) {
        // Asking twice must not install twice. Two hooks on the same thread
        // means the same key delivered twice, which the user experiences as
        // "it sometimes translates on a single Ctrl+C" - and the second hook
        // is invisible to every log we keep.
        return true;
    }
    if (!m_installer) {
        m_error = QStringLiteral("no keyboard observer was given to this build");
        return false;
    }
    const std::function<void(const WinKeyTransition &)> deliver =
        [this](const WinKeyTransition &t) {
            // Queued, never inline: the system is inside our hook callback
            // right now, and translation means HTTP.
            QMetaObject::invokeMethod(this, [this, t]() { onTransition(t); },
                                      Qt::QueuedConnection);
        };
    if (!m_installer(deliver, &m_error)) {
        if (m_error.isEmpty()) {
            m_error = QStringLiteral("the keyboard observer refused to install "
                                     "and gave no reason");
        }
        return false;
    }
    m_running = true;
    m_policy.resetCounters();
    return true;
}

void WinHotkeyBackend::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    if (m_uninstaller) {
        m_uninstaller();
    }
    // A stopped backend answers nothing, even if a queued transition was
    // already on its way: quitting from the tray must not translate once more
    // on the way out. The counters survive - --backend-info and the tests ask
    // "how many keys did you actually see" after the run.
    m_policy.resetGesture();
}

void WinHotkeyBackend::onTransition(const WinKeyTransition &t)
{
    if (!m_running) {
        return;
    }
    m_policy.feed(t);
}

} // namespace lt
