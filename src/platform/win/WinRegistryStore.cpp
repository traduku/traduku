/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinRegistryStore.h"

#include <QDebug>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace lt {

namespace {

#ifdef Q_OS_WIN

QString win32ErrorText(quint32 code)
{
    LPWSTR text = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    QString out;
    if (n && text) {
        out = QString::fromUtf16(reinterpret_cast<const ushort *>(text),
                                 static_cast<int>(n)).trimmed();
    }
    if (text) {
        LocalFree(text);
    }
    return out.isEmpty() ? QStringLiteral("Win32 error") : out;
}

/*!
 * The Run value, through Reg* rather than QSettings.
 *
 * Measured on the Qt this project builds with (6.8.3, and checked because it
 * decides this file): QSettings has no read-only mode and no absolute-key
 * mode since Qt 6 removed `NoScope`, so "ask for the value without creating
 * the key" cannot be expressed through it at all. RegOpenKeyExW with KEY_READ
 * expresses it exactly, and gives back a Win32 error string instead of a
 * Status enum - which is the second thing `IRunKeyStore::errorText()` needs.
 *
 * Not a preference over Qt: the property the store interface promises is a
 * property the API has to be able to keep.
 */
class Win32RunKeyStore : public IRunKeyStore
{
public:
    Win32RunKeyStore(const RunKey &key, bool readOnly)
        : m_readOnly(readOnly)
    {
        QString rest;
        if (key.keyPath.startsWith(QStringLiteral("HKEY_CURRENT_USER"), Qt::CaseInsensitive)) {
            m_root = HKEY_CURRENT_USER;
            rest = key.keyPath.mid(QStringLiteral("HKEY_CURRENT_USER").length());
        } else if (key.keyPath.startsWith(QStringLiteral("HKEY_LOCAL_MACHINE"),
                                          Qt::CaseInsensitive)) {
            m_root = HKEY_LOCAL_MACHINE;
            rest = key.keyPath.mid(QStringLiteral("HKEY_LOCAL_MACHINE").length());
        } else {
            m_error = QStringLiteral("%1 is not a HKCU or HKLM key").arg(key.keyPath);
            return;
        }
        while (rest.startsWith(QLatin1Char('\\'))) {
            rest.remove(0, 1);
        }
        m_subKey = rest;
        m_value = key.valueName;
    }

    bool contains(const QString &value) const override
    {
        QString unused;
        return readValue(value, &unused);
    }

    QString value(const QString &value) const override
    {
        QString out;
        readValue(value, &out);
        return out;
    }

    void setValue(const QString &value, const QString &text) override
    {
        HKEY key = open(/*write=*/true);
        if (!key) {
            return;
        }
        // REG_SZ carries its terminator: other readers (explorer's Run
        // handling, reg.exe, everything the user will inspect) expect the NUL
        // to be part of the data, and a value written without one has been
        // read back as "our string plus whatever was in the buffer".
        QString withNul = text;
        withNul.append(QChar(0));
        const LONG r = RegSetValueExW(key, reinterpret_cast<LPCWSTR>(value.utf16()), 0,
                                      REG_SZ,
                                      reinterpret_cast<const BYTE *>(withNul.utf16()),
                                      static_cast<DWORD>(withNul.size() * sizeof(wchar_t)));
        RegCloseKey(key);
        if (r != ERROR_SUCCESS) {
            m_error = QStringLiteral("RegSetValueExW(%1) failed: %2")
                          .arg(value, win32ErrorText(static_cast<quint32>(r)));
            qWarning("traduku: %s", qUtf8Printable(m_error));
        }
    }

    void remove(const QString &value) override
    {
        HKEY key = open(/*write=*/true);
        if (!key) {
            return;
        }
        const LONG r = RegDeleteValueW(key, reinterpret_cast<LPCWSTR>(value.utf16()));
        RegCloseKey(key);
        // A value that was not there is the state we asked for, not a failure.
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
            m_error = QStringLiteral("RegDeleteValueW(%1) failed: %2")
                          .arg(value, win32ErrorText(static_cast<quint32>(r)));
            qWarning("traduku: %s", qUtf8Printable(m_error));
        }
    }

    QString errorText() const override { return m_error; }

private:
    HKEY open(bool write) const
    {
        if (m_root == nullptr) {
            return nullptr;      // unparsable key path; m_error already says why
        }
        HKEY key = nullptr;
        const REGSAM access = (write ? KEY_SET_VALUE : KEY_READ)
                            | KEY_WOW64_64KEY;      // the 64-bit view, explicitly
        LONG r = RegOpenKeyExW(m_root, reinterpret_cast<LPCWSTR>(m_subKey.utf16()), 0,
                               access, &key);
        if (r == ERROR_FILE_NOT_FOUND && write) {
            r = RegCreateKeyExW(m_root, reinterpret_cast<LPCWSTR>(m_subKey.utf16()), 0,
                                nullptr, REG_OPTION_NON_VOLATILE, access, nullptr, &key,
                                nullptr);
        }
        if (r != ERROR_SUCCESS) {
            key = nullptr;
            m_error = QStringLiteral("%1 (%2): %3")
                          .arg(m_subKey, write ? QStringLiteral("write") : QStringLiteral("read"),
                               win32ErrorText(static_cast<quint32>(r)));
        }
        return key;
    }

    /// true = the value exists (and `out` carries it, empty string included:
    /// an empty Run value is a real state, so "is it there" and "what is it"
    /// have to be different answers).
    bool readValue(const QString &value, QString *out) const
    {
        HKEY key = open(/*write=*/false);
        if (!key) {
            return false;
        }
        wchar_t buffer[1024];
        DWORD cb = sizeof(buffer);
        DWORD type = 0;
        const LONG r = RegQueryValueExW(key, reinterpret_cast<LPCWSTR>(value.utf16()),
                                        nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buffer), &cb);
        RegCloseKey(key);
        if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
            return false;
        }
        if (out) {
            QString read = QString::fromUtf16(
                reinterpret_cast<const char16_t *>(buffer),
                static_cast<qsizetype>(cb / sizeof(wchar_t)));
            // The terminator is data, not spacing: trimmed() will not remove
            // it, and a Run value ending in U+0000 does not match the path we
            // wrote - so "our own entry" would read as somebody else's.
            while (!read.isEmpty() && read.at(read.size() - 1) == QChar(0)) {
                read.chop(1);
            }
            *out = read.trimmed();
        }
        return true;
    }

    bool m_readOnly = true;
    HKEY m_root = nullptr;
    QString m_subKey;
    QString m_value;
    mutable QString m_error;
};

#endif  // Q_OS_WIN

} // namespace

RunKeyStoreFactory registryStoreFactory()
{
    return [](const RunKey &key, bool readOnly) {
#ifdef Q_OS_WIN
        return std::unique_ptr<IRunKeyStore>(new Win32RunKeyStore(key, readOnly));
#else
        // There is no registry to open. Returning no store is the honest
        // answer, and WinAutostart turns "no store" into a refusal with a
        // reason rather than a silent success.
        Q_UNUSED(key)
        Q_UNUSED(readOnly)
        return std::unique_ptr<IRunKeyStore>(nullptr);
#endif
    };
}

} // namespace lt
