#pragma once

#include "interfaces/iglobalshortcut.h"

class QAction;

namespace lt {

/// Plasma system-wide shortcuts through KGlobalAccel. Emits triggered()
/// once per press; ShortcutMonitor (core) pairs them into the gesture.
class KdeGlobalShortcut : public IGlobalShortcut
{
    Q_OBJECT
public:
    explicit KdeGlobalShortcut(QObject *parent = nullptr);

    bool registerShortcut(const QString &seq) override;
    void unregister() override;
    QString backendName() const override { return QStringLiteral("kglobalaccel"); }

private:
    QAction *m_action = nullptr;
};

} // namespace lt
