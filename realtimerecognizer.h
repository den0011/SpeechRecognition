#ifndef REALTIMERECOGNIZER_H
#define REALTIMERECOGNIZER_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioInput>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

// Захват звука с микрофона и распознавание речи "на лету":
// звук с микрофона читается напрямую (QAudioInput), делится на фразы по паузам (энергетический VAD),
// каждая фраза сразу отправляется в whisper. Пока фраза не закончена, периодически
// выдаётся промежуточный вариант текста.
// Если рядом с whisper-cli лежит whisper-server, модель загружается один раз и остаётся в памяти —
// это даёт задержку в доли секунды вместо перезагрузки модели на каждую фразу.
class RealtimeRecognizer : public QObject
{
    Q_OBJECT
public:
    explicit RealtimeRecognizer(QObject *parent = nullptr);
    ~RealtimeRecognizer();

    void setWhisperPath(const QString &path);
    void setModelPath(const QString &path);
    void setLanguage(const QString &lang);
    void setAudioDevice(const QString &deviceName);
    void setThreads(int threads);
    void setCompactFinals(bool enabled);
    void setBeamSize(int beamSize);
    void setVadModel(const QString &path);
    void setLiveTranscription(bool enabled);
    bool isLive() const;
    QString recordingFile() const;
    void setRecordingFile(const QString &wavPath);

    bool isRunning() const;
    bool isServerMode() const;

    void start();
    void stop();
    void shutdownServer();

signals:
    void started();
    void stopped();
    void finished();
    void partialText(const QString &text);
    void finalText(const QString &text);
    void levelChanged(int percent);
    void noSignal();
    void error(const QString &errorMsg);
    void log(const QString &message, const QString &level);

private slots:
    void onAudioReady();
    void onAudioStateChanged(QAudio::State state);
    void onServerProbe();
    void onServerFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onReplyFinished(QNetworkReply *reply);
    void onCliFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    enum Backend { NoBackend, ServerStarting, ServerBackend, CliBackend };

    struct Job {
        QVector<float> audio;
        int segmentId;
        bool isFinal;
    };

    void startServer();
    void stopServer();
    void switchToCli(const QString &reason);
    QString serverExecutablePath() const;

    void convertAndAppend(const char *data, qint64 size);
    void processFrame(const float *frame);
    void beginSegment();
    void finalizeSegment();
    void maybeRequestInterim();
    int audioContextFor(const Job &job) const;

    void dispatchNext();
    void sendToServer(const Job &job);
    void sendToCli(const Job &job);
    void handleResult(const QString &rawText);
    void checkFinished();

    QString cleanupText(const QString &text) const;
    QString promptContext() const;
    static QByteArray makeWav(const QVector<float> &samples);
    void writeRecording(const float *samples, int count);
    void closeRecording();

private:
    QString m_whisperPath;
    QString m_modelPath;
    QString m_language;
    QString m_deviceName;
    int m_threads;
    bool m_compactFinals;
    int m_beamSize;
    QString m_vadModel;
    bool m_live;
    int m_silentFrames;
    bool m_noSignalReported;

    // Захват звука
    QAudioInput *m_audioInput;
    QIODevice *m_audioDevice;
    QAudioFormat m_format;
    QByteArray m_byteRemainder;
    double m_resamplePos;
    float m_prevSample;
    QVector<float> m_frameBuf;

    // Детектор речи
    float m_noiseFloor;
    int m_framesSeen;
    bool m_inSpeech;
    int m_speechRun;
    int m_silenceRun;
    QVector<float> m_preRoll;
    QVector<float> m_segment;
    int m_segmentId;
    QElapsedTimer m_interimTimer;
    int m_levelCounter;
    float m_levelPeak;

    // Распознавание
    Backend m_backend;
    QProcess *m_serverProcess;
    QString m_serverModel;
    QString m_serverLogTail;
    QString m_serverVad;
    int m_serverPort;
    QTimer *m_probeTimer;
    QElapsedTimer m_serverStartTimer;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QProcess *m_cliProcess;
    QString m_cliTempFile;
    QQueue<Job> m_queue;
    bool m_busy;
    Job m_current;
    QElapsedTimer m_jobTimer;
    QStringList m_finalHistory;

    bool m_running;
    bool m_stopping;

    // Сохранение записи сессии
    QString m_recordingPath;
    QFile m_recordingFile;
    qint64 m_recordedSamples;
};

#endif // REALTIMERECOGNIZER_H
