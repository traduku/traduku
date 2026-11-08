#pragma once

#include "interfaces/iinstancetoken.h"

namespace lt::win {

/*!
 * The single-instance claim Windows actually guarantees: a named mutex.
 *
 * Named pipes were the first candidate and were rejected by a test
 * (tst_singleinstance::theDefaultTokenIsExclusive) - Qt creates them with
 * PIPE_UNLIMITED_INSTANCES, so a second Traduku happily listens beside the
 * first and both believe they own the session. A mutex, by contrast, answers
 * ERROR_ALREADY_EXISTS to the second creator, is per-session (so two users
 * logged in at once are not each other's "already running"), and is released
 * by the kernel when the process dies - which is the property the previous
 * design was described as having and did not.
 *
 * The name is prefixed `Local\\`: the global namespace would let a service, or
 * another user's session, hold Traduku's name and lock the app out of starting
 * for somebody who is not running it at all.
 */
class InstanceToken final : public IInstanceToken
{
public:
    explicit InstanceToken(QString name);
    ~InstanceToken() override;

    bool acquire() override;
    void release() override;
    QString errorText() const override { return m_error; }
    QString description() const override { return m_name; }

private:
    QString m_name;
    void *m_handle = nullptr;   // HANDLE, kept out of this header
    QString m_error;
};

} // namespace lt::win
