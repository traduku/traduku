#include "WinInstanceToken.h"

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace lt::win {

namespace {

QString lastErrorText(const wchar_t *what, DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
                        | FORMAT_MESSAGE_FROM_SYSTEM
                        | FORMAT_MESSAGE_IGNORE_INSERTS;
    FormatMessageW(flags, nullptr, code, 0,
                   reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    const QString text = buffer ? QString::fromWCharArray(buffer).trimmed()
                                : QStringLiteral("error %1").arg(code);
    LocalFree(buffer);
    return QStringLiteral("%1 failed: %2 (Win32 error %3)")
        .arg(QString::fromUtf16(reinterpret_cast<const char16_t *>(what)),
                 text).arg(code);
}

} // namespace

InstanceToken::InstanceToken(QString name)
    : m_name(std::move(name))
{
}

InstanceToken::~InstanceToken()
{
    release();
}

bool InstanceToken::acquire()
{
    if (m_handle) {
        return true;   // already ours; CreateMutex would only bump a refcount
    }
    // Local\: per-session. Global\: would let a service or another user's
    // session hold the name and deny this user their own first instance.
    const QString full = QStringLiteral("Local\\") + m_name;
    const std::wstring wide = full.toStdWString();
    HANDLE h = CreateMutexW(nullptr, TRUE, wide.c_str());
    const DWORD created = GetLastError();
    if (!h) {
        m_error = lastErrorText(L"CreateMutexW", created);
        return false;
    }
    if (created == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        m_error = QStringLiteral("token %1 is already claimed").arg(m_name);
        return false;
    }
    m_handle = h;
    m_error.clear();
    return true;
}

void InstanceToken::release()
{
    if (m_handle) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
}

} // namespace lt::win
