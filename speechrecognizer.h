#ifndef SPEECHRECOGNIZER_H
#define SPEECHRECOGNIZER_H

#include <QObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QAudioRecorder>
#include <QUrl>

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

    void startRecording();
    void stopRecording();
    void recognizeFromFile(const QString &audioFile);

    void setWhisperPath(const QString &path);
    void setModelPath(const QString &path);
    void setLanguage(const QString &lang);
    void setAudioDevice(const QString &deviceName);
    void setAudioSettings(int sampleRate, int bitRate, int channels);
    void setPerformanceMode(PerformanceMode mode);

    QString getWhisperPath() const;
    QString getModelPath() const;
    QString getLanguage() const;
    QString getAudioDevice() const;
    PerformanceMode getPerformanceMode() const;
    
    QStringList getAvailableAudioDevices() const;

signals:
    void recognitionComplete(const QString &text);
    void error(const QString &errorMsg);
    void recordingStarted();
    void recordingStopped();

private slots:
    void onRecordingFinished();
    void onWhisperFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onWhisperError(QProcess::ProcessError error);

private:
    void applyAudioSettings();
    QStringList getRecorderAudioInputs() const;
    QString getAudioInputDescription(const QString &inputId) const;
    QString resolveAudioInputId(const QString &deviceName) const;
    int recommendedWhisperThreads() const;
    QString effectiveModelPath() const;
    QString preferredModelNameForMode() const;

private:
    QAudioRecorder *m_audioRecorder;
    QProcess *m_whisperProcess;
    QString m_tempAudioFile;
    QString m_whisperPath;
    QString m_modelPath;
    QString m_language;
    QString m_audioDeviceName;
    PerformanceMode m_performanceMode;
    
    // Настраиваемые параметры аудио
    int m_sampleRate;
    int m_bitRate;
    int m_channelCount;
    QElapsedTimer m_recognitionTimer;
};

#endif // SPEECHRECOGNIZER_H
