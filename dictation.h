#ifndef DICTATION_H
#define DICTATION_H

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QStringList>

// Глобальная горячая клавиша: срабатывает, даже когда окно программы не активно.
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    // Пустая строка — отключить. Возвращает false, если сочетание занято другой программой.
    bool setShortcut(const QString &shortcut, quintptr windowId);
    QString shortcut() const;

    static QStringList availableShortcuts();

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override;

signals:
    void activated();

private:
    void unregisterShortcut();

    QString m_shortcut;
    quintptr m_windowId;
    bool m_registered;
};

namespace Dictation {
// Набирает текст в активном окне другой программы (как будто его ввели с клавиатуры).
// Возвращает false, если активно окно этой программы или ввод не удался.
bool typeIntoForegroundWindow(const QString &text);
void playSignal(bool start);
}

#endif // DICTATION_H
