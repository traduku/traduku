/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include "WinAutostart.h"

namespace lt {

/*!
 * The production `IRunKeyStore`: QSettings in NativeFormat, which on Windows
 * is the registry.
 *
 * `readOnly` is the whole reason this is a separate file. QSettings creates a
 * key it is only asked to read if it is opened ReadWrite, so a plain
 * `traduku --autostart=status` - run by every installer check, and by users
 * following a troubleshooting section - would leave a registry key behind and
 * report "on" for a state that never existed. tst_winautostart asserts the
 * flag is honoured; tst_winautostart_registry asserts the key really is absent
 * afterwards, in the hive, not in a mock.
 */
RunKeyStoreFactory registryStoreFactory();

/// What WinAutostart uses when no factory is injected. Same function, two
/// names, so "the default" is a thing a test can compare against rather than a
//  guess about which of two call sites is live.
inline RunKeyStoreFactory defaultRunKeyStoreFactory()
{
    return registryStoreFactory();
}

} // namespace lt
