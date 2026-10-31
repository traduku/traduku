#pragma once

#include <QObject>
#include <QString>

namespace lt {

/// Desktop notifications (Plasma via KNotification, GNOME via
/// libnotify/portal, ...). Sound playback is separate (core SoundPlayer):
/// it must work on any desktop.
class INotifier : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~INotifier() override { QObject::disconnect(); }

    /// ms <= 0 => server default timeout.
    virtual void notify(const QString &title, const QString &body,
                        const QString &iconName = QString(),
                        int ms = 0) = 0;
    virtual QString backendName() const = 0;
};

} // namespace lt
