#pragma once

#include <QSet>
#include <QtGlobal>
#include <QObject>
#include <functional>

namespace lt {

// X11 keysym identifiers for the gesture (Qt-independent numeric ids;
// the X11 backend feeds keysyms, the Wayland portal backend evdev
// keycodes mapped to these ids - any stable id works).
constexpr quint32 KeyCtrlL   = 0xFFE3;
constexpr quint32 KeyCtrlR   = 0xFFE4;
constexpr quint32 KeyC       = 0x0043;
constexpr quint32 KeyNativeC = 54;   // X11 keycode of the physical C key

/*!
 * Layout-independent "double Ctrl+C" state machine, decoupled from any
 * input source: feed it key events and it emits activated() once per
 * gesture. The clock is injectable, which makes every branch unit-testable.
 *
 * A "press" is: trigger key (physical C by native code, or c/C/с/С keysyms)
 * pressed while Ctrl is down (tracked key state, with a modifier-state
 * probe fallback for Wayland sessions that sync only modifier state).
 * Two presses within intervalMs() => activated(). Auto-repeat (press
 * without release) collapses into one press.
 */
class DoublePressDetector : public QObject
{
    Q_OBJECT
public:
    enum Type { Press, Release };
    using Clock = std::function<qint64()>;          // ms since epoch
    using ModifierProbe = std::function<bool()>;    // Ctrl currently down?

    explicit DoublePressDetector(QObject *parent = nullptr);

    void setClock(Clock clock);
    void setIntervalMs(qint64 ms);
    qint64 intervalMs() const { return m_intervalMs; }
    void setModifierProbe(ModifierProbe probe);

    void addTriggerKey(quint32 keyId);
    void addTriggerNativeCode(quint32 code);
    void addModifierKey(quint32 keyId);

    void keyEvent(Type type, quint32 keyId, quint32 nativeCode = 0);
    void reset();

Q_SIGNALS:
    void activated();

private:
    bool isTrigger(quint32 keyId, quint32 nativeCode) const;

    QSet<quint32> m_triggerKeys;
    QSet<quint32> m_triggerNative;
    QSet<quint32> m_modifierKeys;
    Clock m_clock;
    ModifierProbe m_probe;
    qint64 m_intervalMs = 500;

    bool m_ctrlDown = false;
    bool m_cDown = false;
    qint64 m_lastPressMs = 0;
};

} // namespace lt
