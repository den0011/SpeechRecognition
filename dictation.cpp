#include "dictation.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <vector>
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#endif

namespace {
const int kHotkeyId = 0x5352; // "SR"

struct ShortcutDef {
    const char *name;
    unsigned modifiers;
    unsigned key;
};

#ifdef Q_OS_WIN
const ShortcutDef kShortcuts[] = {
    { "Ctrl+Alt+D",       MOD_CONTROL | MOD_ALT,   'D' },
    { "Ctrl+Alt+R",       MOD_CONTROL | MOD_ALT,   'R' },
    { "Ctrl+Alt+Space",   MOD_CONTROL | MOD_ALT,   VK_SPACE },
    { "Ctrl+Shift+Space", MOD_CONTROL | MOD_SHIFT, VK_SPACE },
    { "F9",               0,                       VK_F9 },
    { "Pause",            0,                       VK_PAUSE },
};
#endif
}

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
    , m_windowId(0)
    , m_registered(false)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterShortcut();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

QStringList GlobalHotkey::availableShortcuts()
{
    QStringList names;
#ifdef Q_OS_WIN
    for (const ShortcutDef &def : kShortcuts) {
        names << QString::fromLatin1(def.name);
    }
#endif
    return names;
}

QString GlobalHotkey::shortcut() const
{
    return m_registered ? m_shortcut : QString();
}

bool GlobalHotkey::setShortcut(const QString &shortcut, quintptr windowId)
{
    unregisterShortcut();
    m_shortcut = shortcut;
    m_windowId = windowId;
    if (shortcut.isEmpty()) {
        return true;
    }

#ifdef Q_OS_WIN
    for (const ShortcutDef &def : kShortcuts) {
        if (shortcut == QLatin1String(def.name)) {
            m_registered = RegisterHotKey(reinterpret_cast<HWND>(windowId), kHotkeyId,
                                          def.modifiers | MOD_NOREPEAT, def.key);
            return m_registered;
        }
    }
#endif
    return false;
}

void GlobalHotkey::unregisterShortcut()
{
#ifdef Q_OS_WIN
    if (m_registered) {
        UnregisterHotKey(reinterpret_cast<HWND>(m_windowId), kHotkeyId);
    }
#endif
    m_registered = false;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (m_registered && eventType == "windows_generic_MSG") {
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == static_cast<WPARAM>(kHotkeyId)) {
            emit activated();
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}

namespace Dictation {

bool typeIntoForegroundWindow(const QString &text)
{
#ifdef Q_OS_WIN
    HWND foreground = GetForegroundWindow();
    if (!foreground || text.isEmpty()) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (processId == GetCurrentProcessId()) {
        return false; // текст и так появится в окне программы
    }

    // KEYEVENTF_UNICODE вводит любые символы (в т.ч. кириллицу) независимо от раскладки
    std::vector<INPUT> inputs;
    inputs.reserve(static_cast<size_t>(text.size()) * 2);
    for (const QChar ch : text) {
        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        if (ch == QLatin1Char('\n')) {
            down.ki.wVk = VK_RETURN;
        } else {
            down.ki.wScan = ch.unicode();
            down.ki.dwFlags = KEYEVENTF_UNICODE;
        }
        INPUT up = down;
        up.ki.dwFlags |= KEYEVENTF_KEYUP;
        inputs.push_back(down);
        inputs.push_back(up);
    }
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    return sent == inputs.size();
#else
    Q_UNUSED(text);
    return false;
#endif
}

void playSignal(bool start)
{
#ifdef Q_OS_WIN
    MessageBeep(start ? MB_ICONASTERISK : MB_OK);
#else
    Q_UNUSED(start);
#endif
}

} // namespace Dictation
