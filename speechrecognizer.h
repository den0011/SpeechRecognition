#ifndef SPEECHRECOGNIZER_H
#define SPEECHRECOGNIZER_H

#include <QObject>
#include <QProcess>
#include <QAudioRecorder>
#include <QAudioDeviceInfo>
#include <QUrl>

class SpeechRecognizer : public QObject
{
    Q_OBJECT
public:
    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer();

    void startRecording();
    void stopRecording();
    void recognizeFromFile(const QString &audioFile);

    void setWhisperPath(const QString &path);
    void setModelPath(const QString &path);
    void setLanguage(const QString &lang);
    void setAudioDevice(const QString &deviceName);

    QString getWhisperPath() const;
    QString getModelPath() const;
    QString getLanguage() const;
    QString getAudioDevice() const;
    
    // Получить список доступных аудио устройств
    QStringList getAvailableAudioDevices() const;

signals:
    void recognitionComplete(const QString &text);
    void error(const QString &errorMsg);
    void recordingStarted();
    void recordingStopped();
    void processingProgress(int percentage);

private slots:
    void onRecordingFinished();
    void onWhisperFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onWhisperError(QProcess::ProcessError error);

private:
    QAudioRecorder *m_audioRecorder;
    QProcess *m_whisperProcess;
    QString m_tempAudioFile;
    QString m_whisperPath;
    QString m_modelPath;
    QString m_language;
    QString m_audioDeviceName;
};

#endif // SPEECHRECOGNIZER_H
