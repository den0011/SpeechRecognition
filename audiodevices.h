#ifndef AUDIODEVICES_H
#define AUDIODEVICES_H

#include <QAudioDeviceInfo>
#include <QStringList>

// Единый список устройств записи для всей программы.
// Разные API Qt на Windows называют одно устройство по-разному (WinMM обрезает имя
// до 31 символа), поэтому имена сопоставляются сначала точно, потом по префиксу.
namespace AudioDevices {

inline QStringList inputNames()
{
    QStringList names;
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for (const QAudioDeviceInfo &device : devices) {
        const QString name = device.deviceName().trimmed();
        if (!name.isEmpty() && !names.contains(name)) {
            names << name;
        }
    }
    return names;
}

inline int matchName(const QStringList &candidates, const QString &name)
{
    if (name.isEmpty()) {
        return -1;
    }
    const int exact = candidates.indexOf(name);
    if (exact >= 0) {
        return exact;
    }
    for (int i = 0; i < candidates.size(); ++i) {
        const QString &candidate = candidates.at(i);
        if (!candidate.isEmpty() && (name.startsWith(candidate) || candidate.startsWith(name))) {
            return i;
        }
    }
    return -1;
}

inline QAudioDeviceInfo findInput(const QString &name, bool *found = nullptr)
{
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    QStringList names;
    for (const QAudioDeviceInfo &device : devices) {
        names << device.deviceName().trimmed();
    }
    const int index = matchName(names, name);
    if (found) {
        *found = index >= 0;
    }
    return index >= 0 ? devices.at(index) : QAudioDeviceInfo::defaultInputDevice();
}

} // namespace AudioDevices

#endif // AUDIODEVICES_H
