#pragma once

#include "interfaces/inotifier.h"

namespace lt {

/// Plasma notifications through KNotification (freedesktop Notifications
/// over DBus). A GNOME build swaps in a libnotify/portal implementation.
class KdeNotifier : public INotifier
{
    Q_OBJECT
public:
    using INotifier::INotifier;

    void notify(const QString &title, const QString &body,
                const QString &iconName = QString(), int ms = 0) override;
    QString backendName() const override { return QStringLiteral("knotification"); }
};

} // namespace lt
