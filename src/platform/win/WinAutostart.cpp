/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinAutostart.h"

#include "WinRegistryStore.h"

#include <QDir>

namespace lt {

namespace {

const char *const kUserRun =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char *const kMachineRun =
    "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

/// The Run value as the registry stores it: quoted, because the Run parser
/// splits on spaces and "C:\Program Files\Traduku\traduku.exe" without quotes
/// asks it to run a program called "C:\Program" with an argument nobody asked
/// for. Silent, and the app simply never appears at login.
QString quote(const QString &command)
{
    return QLatin1Char('"') + command + QLatin1Char('"');
}

QString unquote(QString command)
{
    command = command.trimmed();
    if (command.startsWith(QLatin1Char('"')) && command.endsWith(QLatin1Char('"'))
        && command.size() >= 2) {
        command = command.mid(1, command.size() - 2);
    }
    return command.trimmed();
}

} // namespace

RunKey autostartRunKey(const QString &valueName)
{
    return RunKey{QString::fromLatin1(kUserRun), valueName};
}

RunKey machineRunKey(const QString &valueName)
{
    return RunKey{QString::fromLatin1(kMachineRun), valueName};
}

RunKey selfTestRunKey(const QString &valueName, const QString &sandboxId)
{
    // Deliberately not under CurrentVersion: a selftest key must be
    // unambiguously ours, greppable, and impossible to confuse with the entry
    // that decides whether the user's app starts tomorrow morning.
    return RunKey{QStringLiteral("HKEY_CURRENT_USER\\Software\\Traduku\\SelfTest\\%1\\Run")
                      .arg(sandboxId),
                  valueName};
}

WinAutostart::WinAutostart(const QString &exePath, const RunKey &userKey,
                          const QList<RunKey> &foreignKeys,
                          RunKeyStoreFactory factory)
    : m_exePath(exePath)
    , m_userKey(userKey)
    , m_foreignKeys(foreignKeys)
    , m_factory(factory ? std::move(factory) : defaultRunKeyStoreFactory())
{
}

QString WinAutostart::expectedCommand() const
{
    return quote(QDir::toNativeSeparators(m_exePath));
}

WinAutostart::Ownership WinAutostart::classify(const RunKey &key) const
{
    std::unique_ptr<IRunKeyStore> store = m_factory(key, true);
    if (!store || !store->contains(key.valueName)) {
        return Ownership::Absent;
    }
    const QString found = unquote(store->value(key.valueName));
    const QString mine = unquote(expectedCommand());
    // Windows paths are case-insensitive, and the app may have been moved
    // between the time the entry was written and now: "ours" means the same
    // file, spelled the way a filesystem spells it.
    if (found.compare(QDir::toNativeSeparators(mine), Qt::CaseInsensitive) == 0) {
        return Ownership::Ours;
    }
    return Ownership::Foreign;
}

bool WinAutostart::isEnabled() const
{
    if (classify(m_userKey) != Ownership::Absent) {
        return true;
    }
    for (const RunKey &fk : m_foreignKeys) {
        if (classify(fk) != Ownership::Absent) {
            return true;      // it starts at login: saying "off" would be a lie
        }
    }
    return false;
}

bool WinAutostart::setEnabled(bool on)
{
    m_error.clear();
    if (on) {
        std::unique_ptr<IRunKeyStore> store = m_factory(m_userKey, false);
        if (!store) {
            m_error = QStringLiteral("there is no registry to write on this "
                                     "platform - autostart cannot be turned on");
            return false;
        }
        store->setValue(m_userKey.valueName, expectedCommand());
        // The store knows whether the hive let it through. Reporting success on
        // a refused write is how "I enabled autostart in the menu" becomes an
        // argument with a user's own login.
        if (!store->errorText().isEmpty()) {
            m_error = store->errorText();
            return false;
        }
        return true;
    }

    // Off. Two cases refuse, and both of them are the same product rule: the
    // switch may stop what Traduku started, nothing else.
    for (const RunKey &fk : m_foreignKeys) {
        if (classify(fk) != Ownership::Absent) {
            m_error = QStringLiteral("%1 has a machine-wide Run entry that "
                                     "Traduku did not write and will not delete "
                                     "(an administrator put it there). The app "
                                     "will keep starting at login until that "
                                     "entry is removed").arg(fk.keyPath);
            return false;
        }
    }
    if (classify(m_userKey) == Ownership::Foreign) {
        m_error = QStringLiteral("%1\\%2 is not this executable - Traduku will "
                                 "not delete an entry it did not write")
                      .arg(m_userKey.keyPath, m_userKey.valueName);
        return false;
    }
    std::unique_ptr<IRunKeyStore> store = m_factory(m_userKey, false);
    if (!store) {
        m_error = QStringLiteral("there is no registry to write on this platform");
        return false;
    }
    store->remove(m_userKey.valueName);
    if (!store->errorText().isEmpty()) {
        m_error = store->errorText();
        return false;
    }
    return true;
}

QString WinAutostart::backendName() const
{
    return QStringLiteral("registry (%1)").arg(m_userKey.keyPath);
}

QString WinAutostart::diagnostic() const
{
    for (const RunKey &fk : m_foreignKeys) {
        if (classify(fk) != Ownership::Absent) {
            return QStringLiteral("on - machine-wide entry in %1, written by an "
                                  "administrator: Traduku neither writes nor "
                                  "deletes it").arg(fk.keyPath);
        }
    }
    switch (classify(m_userKey)) {
    case Ownership::Ours:
        return QStringLiteral("on (%1=%2)").arg(m_userKey.keyPath, expectedCommand());
    case Ownership::Foreign: {
        std::unique_ptr<IRunKeyStore> store = m_factory(m_userKey, true);
        const QString cmd = store ? store->value(m_userKey.valueName) : QString();
        return QStringLiteral("on, but the entry is not this executable: %1=%2 - "
                              "written by something else, and Traduku will not "
                              "delete it").arg(m_userKey.keyPath, cmd);
    }
    case Ownership::Absent:
        break;
    }
    return QStringLiteral("off (no %1 value under %2)")
        .arg(m_userKey.valueName, m_userKey.keyPath);
}

} // namespace lt
