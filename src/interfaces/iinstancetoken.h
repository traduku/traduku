/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include <QString>

#include <functional>
#include <memory>

namespace lt {

/*!
 * The "am I the only one?" question, as an object rather than a call.
 *
 * Why this is an interface and not a couple of lines in SingleInstance:
 * "the mechanism refuses a second claim" turned out to be a property of the OS
 * and not of the code. Qt's QLocalServer on Windows creates named pipes with
 * PIPE_UNLIMITED_INSTANCES, so a second Traduku happily listens beside the
 * first and both believe they own the session - the bug this class exists to
 * prevent (two trays, two gesture hooks, one user wondering why Ctrl+C
 * sometimes translates twice). Measured in
 * tst_singleinstance::theDefaultTokenIsExclusive.
 *
 * So Windows hands out a kernel object that really is exclusive (a named
 * mutex, platform/win/WinInstanceToken.cpp), Unix keeps the socket bind, and
 * SingleInstance drives whichever it was given - which is also what lets the
 * refusal and the wake-up be tested without a second process, a second user or
 * a second OS.
 */
class IInstanceToken
{
public:
    virtual ~IInstanceToken() = default;

    /// false = somebody else holds it; errorText() says what was measured.
    virtual bool acquire() = 0;
    virtual void release() = 0;

    virtual QString errorText() const { return {}; }
    /// What was claimed, for the message: "traduku-Dmytro", never
    /// "the mutex" / "000001F3" - an error naming the thing the user can see.
    virtual QString description() const = 0;
};

/// Hands out the mechanism a claim is made with. Injected, so the refusal and
/// the wake-up are testable without a second process, a second user or a
/// second OS.
using InstanceTokenFactory =
    std::function<std::unique_ptr<IInstanceToken>(const QString &name)>;

} // namespace lt
