#ifndef SPEECHRECOGNIZER_H
#define SPEECHRECOGNIZER_H

#include <QObject>
#include <QElapsedTimer>
#include <QProcess>

// Хранит настройки распознавания и распознаёт готовые аудиофайлы через whisper-cli.
// Запись с микрофона выполняет RealtimeRecognizer.
class SpeechRecognizer : public QObject
{
    Q_OBJECT
public:
    enum PerformanceMode {
        FastMode,
        BalancedMode,
        AccurateMode
    };

    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer();

    void recognizeFromFile(const QString &audioFile);

    void setWhisperPath(const QString &path);
    void setModelPath(const QString &path);
    void setLanguage(const QString &lang);
    void setAudioDevice(const QString &deviceName);
    void setPerformanceMode(PerformanceMode mode);

    QString getWhisperPath() const;
    QString getModelPath() const;
    QString getLanguage() const;
    QString getAudioDevice() const;
    PerformanceMode getPerformanceMode() const;

    QStringList getAvailableAudioDevices() const;

    int recommendedWhisperThreads() const;
    int beamSize() const;

    static QString resolvePath(const QString &path);

signals:
    void recognitionComplete(const QString &text);
    void error(const QString &errorMsg);

private slots:
    void onWhisperFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onWhisperError(QProcess::ProcessError error);

private:
    QProcess *m_whisperProcess;
    QString m_whisperPath;
    QString m_modelPath;
    QString m_language;
    QString m_audioDeviceName;
    PerformanceMode m_performanceMode;
    QElapsedTimer m_recognitionTimer;
};

#endif // SPEECHRECOGNIZER_H
