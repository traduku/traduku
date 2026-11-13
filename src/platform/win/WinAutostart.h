/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include "interfaces/iautostart.h"

#include <QList>
#include <QString>
#include <functional>
#include <memory>

namespace lt {

/*!
 * Login autostart on Windows: a value in
 * `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
 *
 * It is the twin of the XDG entry, including the interesting half - agreeing
 * with an entry the app does not own:
 *  - a machine-wide (HKLM) Run value means the app comes back at login
 *    whatever the HKCU value says. `isEnabled()` reports the truth about
 *    "does it start", and the menu tick cannot silently lie about it, which is
 *    exactly the XdgAutostart lesson (the tick used to read "off" while the
 *    packaged /etc/xdg entry kept starting the app).
 *  - a value under our own name that is not our executable belongs to somebody
 *    else. Disabling then refuses to delete it: our off-switch is not a licence
 *    to remove a third party's login item.
 *
 * The store is injected for the assertions that matter and cannot be made
 * against a real registry from a test: "reading the state did not open the key
 * for writing" (QSettings creates a key it merely reads) and "the foreign
 * value is still there afterwards". tests/integration/tst_winautostart_registry
 * covers the real QSettings path on Windows.
 */

/// Where one Run value lives.
struct RunKey
{
    QString keyPath;     ///< "HKEY_CURRENT_USER\\...\\CurrentVersion\\Run"
    QString valueName;   ///< "traduku" - the product's name, desktopBaseName()
};

/// The user's own entry: what "Launch on Login" writes.
RunKey autostartRunKey(const QString &valueName = QStringLiteral("traduku"));
/// The administrator's entry: read, reported, never written.
RunKey machineRunKey(const QString &valueName = QStringLiteral("traduku"));
/// A scratch key of the same shape, for --selftest to exercise this class on
/// the real code path without touching anybody's login sequence.
RunKey selfTestRunKey(const QString &valueName, const QString &sandboxId);

/// What the class needs from the registry. QSettings in NativeFormat is the
/// production implementation (WinRegistryStore); a test injects a table.
class IRunKeyStore
{
public:
    virtual ~IRunKeyStore() = default;
    virtual bool contains(const QString &value) const = 0;
    virtual QString value(const QString &value) const = 0;
    virtual void setValue(const QString &value, const QString &text) = 0;
    virtual void remove(const QString &value) = 0;

    /// Why the last write failed, "" when it did not.
    ///
    /// Without it, setEnabled(true) on a read-only hive (a policy-controlled
    /// machine, a hive the user cannot write) returns true and the app never
    /// comes back at login: the switch lied. Any store that can fail - and the
    /// registry can - fills this in, and WinAutostart passes it on.
    virtual QString errorText() const { return {}; }
};

/// Hands out stores. `readOnly` must be honoured: a store opened for writing
/// creates the registry key, and then `traduku --autostart=status` would leave
/// a key behind on every start - the bug tst_xdgautostart caught on Linux.
using RunKeyStoreFactory =
    std::function<std::unique_ptr<IRunKeyStore>(const RunKey &key, bool readOnly)>;

class WinAutostart : public IAutostart
{
public:
    WinAutostart(const QString &exePath, const RunKey &userKey,
                 const QList<RunKey> &foreignKeys = {},
                 RunKeyStoreFactory factory = {});

    bool isEnabled() const override;
    bool setEnabled(bool on) override;
    QString backendName() const override;

    /// "on (HKCU Run)", "off (no Run entry)", "on - machine-wide entry ...",
    /// "on, but the entry is not this executable ...". Printed by
    /// --backend-info and by `traduku --autostart=status`.
    QString diagnostic() const override;
    /// Why the last setEnabled() said no; empty when it did what was asked.
    QString errorText() const { return m_error; }

    /// The command line this app would put in the value, quoted because
    /// "C:\Program Files\..." without quotes is two arguments to the Run
    /// parser and never launches.
    QString expectedCommand() const;

private:
    enum class Ownership { Absent, Ours, Foreign };

    Ownership classify(const RunKey &key) const;

    QString m_exePath;
    RunKey m_userKey;
    QList<RunKey> m_foreignKeys;
    RunKeyStoreFactory m_factory;
    mutable QString m_error;
};

} // namespace lt
