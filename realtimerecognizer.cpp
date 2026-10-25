#include "realtimerecognizer.h"

#include <QAudioDeviceInfo>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace {
const int kSampleRate = 16000;
const int kFrameSamples = 480;                         // 30 мс
const int kPreRollFrames = 10;                         // 300 мс звука до начала речи
const int kStartFrames = 3;                            // 90 мс голоса, чтобы начать фразу
const int kEndSilenceFrames = 23;                      // ~700 мс тишины завершают фразу
const int kKeepTailFrames = 7;                         // оставить ~200 мс тишины в конце
const int kMaxSegmentSamples = kSampleRate * 12;       // фраза длиннее 12 с режется принудительно
const int kMinSegmentSamples = kSampleRate * 2 / 5;    // короче 0.4 с — щелчки и шум
const int kMinWhisperSamples = kSampleRate * 6 / 5;    // whisper плохо работает с аудио < 1 с
const int kInterimIntervalMs = 1000;
const int kInterimMinSamples = kSampleRate;
const float kMinThreshold = 0.004f;                    // ~ -48 dBFS
const int kServerStartTimeoutMs = 60000;
const int kRequestTimeoutMs = 60000;
}

RealtimeRecognizer::RealtimeRecognizer(QObject *parent)
    : QObject(parent)
    , m_language("ru")
    , m_threads(4)
    , m_compactFinals(false)
    , m_audioInput(nullptr)
    , m_audioDevice(nullptr)
    , m_resamplePos(0.0)
    , m_prevSample(0.0f)
    , m_noiseFloor(kMinThreshold)
    , m_framesSeen(0)
    , m_inSpeech(false)
    , m_speechRun(0)
    , m_silenceRun(0)
    , m_segmentId(0)
    , m_levelCounter(0)
    , m_levelPeak(0.0f)
    , m_backend(NoBackend)
    , m_serverProcess(nullptr)
    , m_serverPort(0)
    , m_probeTimer(new QTimer(this))
    , m_network(new QNetworkAccessManager(this))
    , m_cliProcess(nullptr)
    , m_busy(false)
    , m_running(false)
    , m_stopping(false)
    , m_recordedSamples(0)
{
    m_network->setProxy(QNetworkProxy::NoProxy);
    connect(m_network, &QNetworkAccessManager::finished,
            this, &RealtimeRecognizer::onReplyFinished);

    m_probeTimer->setInterval(250);
    connect(m_probeTimer, &QTimer::timeout, this, &RealtimeRecognizer::onServerProbe);
}

RealtimeRecognizer::~RealtimeRecognizer()
{
    if (m_audioInput) {
        m_audioInput->stop();
    }
    closeRecording();
    stopServer();

    if (m_cliProcess) {
        m_cliProcess->disconnect(this);
        m_cliProcess->kill();
        m_cliProcess->waitForFinished(2000);
    }
    if (!m_cliTempFile.isEmpty()) {
        QFile::remove(m_cliTempFile);
    }
}

void RealtimeRecognizer::setWhisperPath(const QString &path)
{
    m_whisperPath = path;
}

void RealtimeRecognizer::setModelPath(const QString &path)
{
    m_modelPath = path;
}

void RealtimeRecognizer::setLanguage(const QString &lang)
{
    m_language = lang;
}

void RealtimeRecognizer::setAudioDevice(const QString &deviceName)
{
    m_deviceName = deviceName;
}

void RealtimeRecognizer::setThreads(int threads)
{
    m_threads = qMax(1, threads);
}

void RealtimeRecognizer::setCompactFinals(bool enabled)
{
    m_compactFinals = enabled;
}

void RealtimeRecognizer::setRecordingFile(const QString &wavPath)
{
    m_recordingPath = wavPath;
}

bool RealtimeRecognizer::isRunning() const
{
    return m_running;
}

bool RealtimeRecognizer::isServerMode() const
{
    return m_backend == ServerBackend || m_backend == ServerStarting;
}

// ---------------------------------------------------------------------------
// Запуск / остановка
// ---------------------------------------------------------------------------

void RealtimeRecognizer::start()
{
    if (m_running) {
        return;
    }

    if (!QFile::exists(m_whisperPath)) {
        emit error("Whisper не найден: " + m_whisperPath);
        return;
    }
    if (!QFile::exists(m_modelPath)) {
        emit error("Файл модели не найден: " + m_modelPath);
        return;
    }

    // Выбор устройства. Имена из QAudioRecorder и QAudioDeviceInfo на Windows могут
    // отличаться (WinMM обрезает имя до 31 символа), поэтому сравниваем и по префиксу.
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    if (devices.isEmpty()) {
        emit error("Не найдено ни одного устройства записи звука. Подключите микрофон.");
        return;
    }

    QAudioDeviceInfo device = QAudioDeviceInfo::defaultInputDevice();
    bool found = false;
    if (!m_deviceName.isEmpty()) {
        for (const QAudioDeviceInfo &info : devices) {
            if (info.deviceName() == m_deviceName) {
                device = info;
                found = true;
                break;
            }
        }
        if (!found) {
            for (const QAudioDeviceInfo &info : devices) {
                const QString name = info.deviceName();
                if (!name.isEmpty() && (m_deviceName.startsWith(name) || name.startsWith(m_deviceName))) {
                    device = info;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            emit log("Микрофон \"" + m_deviceName + "\" не найден, используется устройство по умолчанию", "WARNING");
        }
    }

    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    if (!device.isFormatSupported(format)) {
        format = device.nearestFormat(format);
    }

    const bool supported =
        (format.sampleSize() == 16 && format.sampleType() == QAudioFormat::SignedInt) ||
        (format.sampleSize() == 8 && format.sampleType() == QAudioFormat::UnSignedInt) ||
        (format.sampleSize() == 32 && (format.sampleType() == QAudioFormat::Float ||
                                       format.sampleType() == QAudioFormat::SignedInt));
    if (!supported || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        emit error("Микрофон не поддерживает подходящий формат звука");
        return;
    }
    m_format = format;

    emit log(QString("Реальное время: %1, %2 Гц, %3 бит, %4 кан.")
             .arg(device.deviceName())
             .arg(format.sampleRate())
             .arg(format.sampleSize())
             .arg(format.channelCount()), "INFO");

    // Сброс состояния
    m_byteRemainder.clear();
    m_resamplePos = 0.0;
    m_prevSample = 0.0f;
    m_frameBuf.clear();
    m_noiseFloor = kMinThreshold;
    m_framesSeen = 0;
    m_inSpeech = false;
    m_speechRun = 0;
    m_silenceRun = 0;
    m_preRoll.clear();
    m_segment.clear();
    m_levelCounter = 0;
    m_levelPeak = 0.0f;
    m_finalHistory.clear();
    m_stopping = false;

    // Распознавание: сервер с моделью в памяти, иначе whisper-cli на каждую фразу
    if (isServerMode() && m_serverModel != m_modelPath) {
        stopServer();
        m_backend = NoBackend;
    }
    if (m_backend == NoBackend || m_backend == CliBackend) {
        if (!serverExecutablePath().isEmpty()) {
            startServer();
        } else {
            m_backend = CliBackend;
            emit log("whisper-server не найден рядом с whisper-cli — фразы будут распознаваться медленнее. "
                     "Положите whisper-server.exe в папку с whisper-cli.exe для быстрого режима.", "WARNING");
        }
    }

    // Файл записи всей сессии
    m_recordedSamples = 0;
    if (!m_recordingPath.isEmpty()) {
        m_recordingFile.setFileName(m_recordingPath);
        if (m_recordingFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_recordingFile.write(QByteArray(44, '\0'));
        } else {
            emit log("Не удалось создать файл записи: " + m_recordingPath, "WARNING");
        }
    }

    if (m_audioInput) {
        m_audioInput->disconnect(this);
        m_audioInput->deleteLater();
    }
    m_audioInput = new QAudioInput(device, format, this);
    m_audioInput->setBufferSize(format.bytesForDuration(200000));
    connect(m_audioInput, &QAudioInput::stateChanged,
            this, &RealtimeRecognizer::onAudioStateChanged);

    m_audioDevice = m_audioInput->start();
    if (!m_audioDevice) {
        closeRecording();
        emit error("Не удалось открыть микрофон для записи");
        return;
    }
    connect(m_audioDevice, &QIODevice::readyRead, this, &RealtimeRecognizer::onAudioReady);

    m_running = true;
    emit started();
}

void RealtimeRecognizer::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;

    if (m_audioInput) {
        if (m_audioDevice) {
            onAudioReady();
            m_audioDevice->disconnect(this);
            m_audioDevice = nullptr;
        }
        m_audioInput->disconnect(this);
        m_audioInput->stop();
    }

    if (m_inSpeech) {
        finalizeSegment();
    }
    m_preRoll.clear();

    closeRecording();
    emit levelChanged(0);

    m_stopping = true;
    emit stopped();
    dispatchNext();
    checkFinished();
}

void RealtimeRecognizer::shutdownServer()
{
    if (m_running) {
        return;
    }
    stopServer();
    m_backend = NoBackend;
}

void RealtimeRecognizer::checkFinished()
{
    if (m_stopping && !m_busy && m_queue.isEmpty()) {
        m_stopping = false;
        emit partialText(QString());
        emit finished();
    }
}

// ---------------------------------------------------------------------------
// whisper-server
// ---------------------------------------------------------------------------

QString RealtimeRecognizer::serverExecutablePath() const
{
    const QDir dir = QFileInfo(m_whisperPath).absoluteDir();
#ifdef Q_OS_WIN
    const QStringList names = { "whisper-server.exe", "server.exe" };
#else
    const QStringList names = { "whisper-server", "server" };
#endif
    for (const QString &name : names) {
        const QString path = dir.filePath(name);
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
}

void RealtimeRecognizer::startServer()
{
    stopServer();

    QTcpServer portFinder;
    if (!portFinder.listen(QHostAddress::LocalHost, 0)) {
        switchToCli("не удалось найти свободный порт");
        return;
    }
    m_serverPort = portFinder.serverPort();
    portFinder.close();

    m_serverProcess = new QProcess(this);
    m_serverProcess->setProcessChannelMode(QProcess::MergedChannels);
    m_serverProcess->setWorkingDirectory(QFileInfo(m_whisperPath).absolutePath());
    // Вывод сервера нужно вычитывать, иначе он заблокируется на переполненном канале
    connect(m_serverProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        m_serverLogTail += QString::fromLocal8Bit(m_serverProcess->readAllStandardOutput());
        if (m_serverLogTail.size() > 4000) {
            m_serverLogTail = m_serverLogTail.right(2000);
        }
    });
    connect(m_serverProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RealtimeRecognizer::onServerFinished);

    QStringList args;
    args << "-m" << m_modelPath
         << "-l" << m_language
         << "-t" << QString::number(m_threads)
         << "--host" << "127.0.0.1"
         << "--port" << QString::number(m_serverPort);

    m_serverLogTail.clear();
    m_serverModel = m_modelPath;
    m_backend = ServerStarting;
    m_serverStartTimer.start();
    emit log("Загрузка модели в whisper-server (порт " + QString::number(m_serverPort) + ")...", "INFO");

    m_serverProcess->start(serverExecutablePath(), args);
    m_probeTimer->start();
}

void RealtimeRecognizer::stopServer()
{
    m_probeTimer->stop();
    if (!m_serverProcess) {
        return;
    }
    m_serverProcess->disconnect(this);
    if (m_serverProcess->state() != QProcess::NotRunning) {
        m_serverProcess->kill();
        m_serverProcess->waitForFinished(2000);
    }
    m_serverProcess->deleteLater();
    m_serverProcess = nullptr;
}

void RealtimeRecognizer::onServerProbe()
{
    if (m_backend != ServerStarting || !m_serverProcess) {
        m_probeTimer->stop();
        return;
    }

    if (m_serverProcess->state() == QProcess::NotRunning) {
        return; // обработает onServerFinished
    }

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(m_serverPort));
    if (socket.waitForConnected(100)) {
        socket.abort();
        m_probeTimer->stop();
        m_backend = ServerBackend;
        emit log(QString("whisper-server готов за %1 мс — модель в памяти").arg(m_serverStartTimer.elapsed()),
                 "SUCCESS");
        dispatchNext();
        return;
    }

    if (m_serverStartTimer.elapsed() > kServerStartTimeoutMs) {
        switchToCli("сервер не запустился за отведённое время");
    }
}

void RealtimeRecognizer::onServerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    QString details = m_serverLogTail.trimmed();
    if (details.size() > 300) {
        details = "..." + details.right(300);
    }
    switchToCli(QString("whisper-server завершился (код %1)%2")
                .arg(exitCode)
                .arg(details.isEmpty() ? QString() : ": " + details));
}

void RealtimeRecognizer::switchToCli(const QString &reason)
{
    stopServer();
    m_backend = CliBackend;
    emit log("Переход на whisper-cli: " + reason, "WARNING");
    if (!m_busy) {
        dispatchNext();
    }
}

// ---------------------------------------------------------------------------
// Захват и преобразование звука в 16 кГц моно float
// ---------------------------------------------------------------------------

void RealtimeRecognizer::onAudioReady()
{
    if (!m_audioDevice) {
        return;
    }
    const QByteArray data = m_audioDevice->readAll();
    if (!data.isEmpty()) {
        convertAndAppend(data.constData(), data.size());
    }
}

void RealtimeRecognizer::onAudioStateChanged(QAudio::State state)
{
    if (state == QAudio::StoppedState && m_running && m_audioInput &&
        m_audioInput->error() != QAudio::NoError) {
        const QAudio::Error err = m_audioInput->error();
        stop();
        emit error("Ошибка захвата звука с микрофона (код " + QString::number(err) + ")");
    }
}

void RealtimeRecognizer::convertAndAppend(const char *data, qint64 size)
{
    const int channels = m_format.channelCount();
    const int bytesPerSample = m_format.sampleSize() / 8;
    const int bytesPerFrame = channels * bytesPerSample;

    QByteArray buffer = m_byteRemainder;
    buffer.append(data, static_cast<int>(size));
    const int frames = buffer.size() / bytesPerFrame;
    m_byteRemainder = buffer.mid(frames * bytesPerFrame);

    const uchar *p = reinterpret_cast<const uchar *>(buffer.constData());
    QVector<float> mono(frames);
    for (int i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) {
            const uchar *s = p + (i * channels + c) * bytesPerSample;
            float v = 0.0f;
            if (bytesPerSample == 2) {
                v = qFromLittleEndian<qint16>(s) / 32768.0f;
            } else if (bytesPerSample == 1) {
                v = (static_cast<int>(*s) - 128) / 128.0f;
            } else if (m_format.sampleType() == QAudioFormat::Float) {
                const quint32 bits = qFromLittleEndian<quint32>(s);
                std::memcpy(&v, &bits, sizeof(v));
            } else {
                v = static_cast<float>(qFromLittleEndian<qint32>(s) / 2147483648.0);
            }
            sum += v;
        }
        mono[i] = sum / channels;
    }

    auto pushSample = [this](float v) {
        m_frameBuf.append(v);
        if (m_frameBuf.size() == kFrameSamples) {
            processFrame(m_frameBuf.constData());
            m_frameBuf.clear();
        }
    };

    if (m_format.sampleRate() == kSampleRate) {
        for (float v : mono) {
            pushSample(v);
        }
        return;
    }

    // Линейная интерполяция; позиция m_resamplePos отсчитывается от начала текущего блока,
    // индекс -1 соответствует последнему отсчёту предыдущего блока (m_prevSample)
    const double step = static_cast<double>(m_format.sampleRate()) / kSampleRate;
    const int n = mono.size();
    if (n == 0) {
        return;
    }
    while (m_resamplePos < n - 1) {
        const int i = static_cast<int>(std::floor(m_resamplePos));
        const float frac = static_cast<float>(m_resamplePos - i);
        const float s0 = (i < 0) ? m_prevSample : mono[i];
        const float s1 = mono[i + 1];
        pushSample(s0 + (s1 - s0) * frac);
        m_resamplePos += step;
    }
    m_resamplePos -= n;
    m_prevSample = mono[n - 1];
}

// ---------------------------------------------------------------------------
// Детектор речи и нарезка фраз
// ---------------------------------------------------------------------------

void RealtimeRecognizer::processFrame(const float *frame)
{
    writeRecording(frame, kFrameSamples);

    double energy = 0.0;
    for (int i = 0; i < kFrameSamples; ++i) {
        energy += frame[i] * frame[i];
    }
    const float rms = static_cast<float>(std::sqrt(energy / kFrameSamples));

    // Индикатор уровня ~10 раз в секунду, шкала -60..0 dBFS
    m_levelPeak = qMax(m_levelPeak, rms);
    if (++m_levelCounter >= 3) {
        const float db = 20.0f * std::log10(qMax(m_levelPeak, 1e-6f));
        emit levelChanged(qBound(0, static_cast<int>((db + 60.0f) * 100.0f / 60.0f), 100));
        m_levelCounter = 0;
        m_levelPeak = 0.0f;
    }

    // Первые ~300 мс — оценка фонового шума
    ++m_framesSeen;
    if (m_framesSeen <= 10) {
        m_noiseFloor += (rms - m_noiseFloor) / m_framesSeen;
    }

    const float threshold = qMax(m_noiseFloor * 3.0f, kMinThreshold);

    if (!m_inSpeech) {
        const bool voiced = rms > threshold;
        if (m_framesSeen > 10) {
            // Пороговый уровень подстраивается под шум; при постоянном громком фоне — медленно растёт
            m_noiseFloor += (rms - m_noiseFloor) * (voiced ? 0.002f : 0.03f);
            m_noiseFloor = qMax(m_noiseFloor, kMinThreshold / 3.0f);
        }

        for (int i = 0; i < kFrameSamples; ++i) {
            m_preRoll.append(frame[i]);
        }
        if (m_preRoll.size() > kPreRollFrames * kFrameSamples) {
            m_preRoll.remove(0, m_preRoll.size() - kPreRollFrames * kFrameSamples);
        }

        m_speechRun = voiced ? m_speechRun + 1 : 0;
        if (m_speechRun >= kStartFrames && m_framesSeen > 10) {
            beginSegment();
        }
        return;
    }

    // Внутри фразы порог ниже (гистерезис), чтобы не обрывать тихие окончания слов
    const bool voiced = rms > threshold * 0.7f;
    for (int i = 0; i < kFrameSamples; ++i) {
        m_segment.append(frame[i]);
    }
    m_silenceRun = voiced ? 0 : m_silenceRun + 1;

    if (m_silenceRun >= kEndSilenceFrames || m_segment.size() >= kMaxSegmentSamples) {
        finalizeSegment();
        return;
    }

    maybeRequestInterim();
}

void RealtimeRecognizer::beginSegment()
{
    m_inSpeech = true;
    m_segment = m_preRoll;
    m_preRoll.clear();
    m_silenceRun = 0;
    m_interimTimer.start();
}

void RealtimeRecognizer::finalizeSegment()
{
    QVector<float> audio = m_segment;

    const int trimFrames = m_silenceRun - kKeepTailFrames;
    if (trimFrames > 0) {
        audio.resize(qMax(0, audio.size() - trimFrames * kFrameSamples));
    }

    m_inSpeech = false;
    m_speechRun = 0;
    m_silenceRun = 0;
    m_segment.clear();
    const int id = m_segmentId++;

    if (audio.size() < kMinSegmentSamples) {
        emit partialText(QString());
        return;
    }

    Job job;
    job.audio = audio;
    job.segmentId = id;
    job.isFinal = true;
    m_queue.enqueue(job);

    // Промежуточный результат больше не нужен — не задерживаем им финальный
    if (m_busy && !m_current.isFinal && m_reply) {
        m_reply->abort();
    }
    dispatchNext();
}

int RealtimeRecognizer::audioContextFor(const Job &job) const
{
    // Энкодер whisper всегда обрабатывает 30-секундное окно (1500 позиций, 50 на секунду).
    // Для коротких фраз окно можно сократить — распознавание в 3-4 раза быстрее,
    // но точность немного ниже, поэтому для финального текста по умолчанию полное окно.
    if (job.isFinal && !m_compactFinals) {
        return 0;
    }
    const int seconds = qMax(job.audio.size(), kMinWhisperSamples) / kSampleRate + 2;
    const int ctx = ((seconds * 50 + 63) / 64) * 64;
    return ctx >= 1500 ? 0 : qMax(ctx, 384);
}

void RealtimeRecognizer::maybeRequestInterim()
{
    // Промежуточный текст только при сервере: whisper-cli слишком долго загружает модель
    if (m_backend != ServerBackend || m_busy || !m_queue.isEmpty()) {
        return;
    }
    // Если пошла тишина — фраза, скорее всего, заканчивается: сервер должен быть свободен для финала
    if (m_silenceRun > 4) {
        return;
    }
    if (m_segment.size() < kInterimMinSamples || m_interimTimer.elapsed() < kInterimIntervalMs) {
        return;
    }

    m_interimTimer.restart();
    Job job;
    job.audio = m_segment;
    job.segmentId = m_segmentId;
    job.isFinal = false;
    m_busy = true;
    m_current = job;
    m_jobTimer.start();
    sendToServer(job);
}

// ---------------------------------------------------------------------------
// Отправка фраз в whisper
// ---------------------------------------------------------------------------

void RealtimeRecognizer::dispatchNext()
{
    if (m_busy || m_queue.isEmpty()) {
        return;
    }
    if (m_backend != ServerBackend && m_backend != CliBackend) {
        return; // ждём запуска сервера
    }

    m_current = m_queue.dequeue();
    m_busy = true;
    m_jobTimer.start();

    if (m_backend == ServerBackend) {
        sendToServer(m_current);
    } else {
        sendToCli(m_current);
    }
}

QString RealtimeRecognizer::promptContext() const
{
    QString context = m_finalHistory.join(' ');
    if (context.size() > 200) {
        context = context.right(200);
    }
    return context;
}

void RealtimeRecognizer::sendToServer(const Job &job)
{
    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addField = [multiPart](const QString &name, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"%1\"").arg(name));
        part.setBody(value);
        multiPart->append(part);
    };

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, "audio/wav");
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       "form-data; name=\"file\"; filename=\"audio.wav\"");
    filePart.setBody(makeWav(job.audio));
    multiPart->append(filePart);

    addField("temperature", "0.0");
    const int audioCtx = audioContextFor(job);
    if (audioCtx > 0) {
        addField("audio_ctx", QByteArray::number(audioCtx));
    }
    addField("response_format", "json");
    addField("language", m_language.toUtf8());
    const QString prompt = promptContext();
    if (!prompt.isEmpty()) {
        addField("prompt", prompt.toUtf8());
    }

    QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/inference").arg(m_serverPort)));
    QNetworkReply *reply = m_network->post(request, multiPart);
    multiPart->setParent(reply);
    m_reply = reply;

    QPointer<QNetworkReply> guard(reply);
    QTimer::singleShot(kRequestTimeoutMs, this, [guard]() {
        if (guard && guard->isRunning()) {
            guard->abort();
        }
    });
}

void RealtimeRecognizer::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!m_busy) {
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    if (reply->error() == QNetworkReply::NoError && status == 200) {
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (obj.contains("error")) {
            emit log("whisper-server: " + obj.value("error").toString(), "ERROR");
            handleResult(QString());
        } else {
            handleResult(obj.value("text").toString());
        }
        return;
    }

    m_busy = false;

    if (status == 503) {
        // Новые версии сервера отвечают 503, пока модель загружается
        if (m_current.isFinal) {
            m_queue.prepend(m_current);
        }
        QTimer::singleShot(300, this, [this]() { dispatchNext(); });
        return;
    }

    if (m_current.isFinal && m_backend != ServerBackend) {
        // Сервер упал во время запроса — повторим через whisper-cli
        m_queue.prepend(m_current);
    } else if (m_current.isFinal) {
        emit log("Ошибка запроса к whisper-server: " + reply->errorString(), "ERROR");
    }

    dispatchNext();
    checkFinished();
}

void RealtimeRecognizer::sendToCli(const Job &job)
{
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_cliTempFile = QDir(tempDir).filePath(QString("speechrec_rt_%1_%2.wav")
                                           .arg(QCoreApplication::applicationPid())
                                           .arg(job.segmentId));
    QFile file(m_cliTempFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit log("Не удалось создать временный файл: " + m_cliTempFile, "ERROR");
        m_busy = false;
        dispatchNext();
        checkFinished();
        return;
    }
    file.write(makeWav(job.audio));
    file.close();

    if (m_cliProcess) {
        m_cliProcess->deleteLater();
    }
    m_cliProcess = new QProcess(this);
    connect(m_cliProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RealtimeRecognizer::onCliFinished);
    connect(m_cliProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            emit error("Не удалось запустить Whisper: " + m_whisperPath);
            QFile::remove(m_cliTempFile);
            m_busy = false;
            m_queue.clear();
            checkFinished();
        }
    });

    QStringList args;
    args << "-m" << m_modelPath
         << "-f" << m_cliTempFile
         << "-l" << m_language
         << "-t" << QString::number(m_threads)
         << "-nt" << "-np";
    const int audioCtx = audioContextFor(job);
    if (audioCtx > 0) {
        args << "-ac" << QString::number(audioCtx);
    }
    const QString prompt = promptContext();
    if (!prompt.isEmpty()) {
        args << "--prompt" << prompt;
    }

    m_cliProcess->start(m_whisperPath, args);
}

void RealtimeRecognizer::onCliFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const QString output = QString::fromUtf8(m_cliProcess->readAllStandardOutput());
    const QString errors = QString::fromLocal8Bit(m_cliProcess->readAllStandardError());
    QFile::remove(m_cliTempFile);

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        emit log(QString("whisper-cli завершился с кодом %1: %2").arg(exitCode).arg(errors.right(300)), "ERROR");
        handleResult(QString());
        return;
    }

    QStringList parts;
    for (const QString &line : output.split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith("whisper_")) {
            parts << trimmed;
        }
    }
    handleResult(parts.join(' '));
}

void RealtimeRecognizer::handleResult(const QString &rawText)
{
    const QString text = cleanupText(rawText);
    const Job job = m_current;
    m_busy = false;

    if (job.isFinal) {
        emit log(QString("Фраза %1 с распознана за %2 мс")
                 .arg(job.audio.size() / double(kSampleRate), 0, 'f', 1)
                 .arg(m_jobTimer.elapsed()), "INFO");
        if (!text.isEmpty()) {
            m_finalHistory << text;
            while (m_finalHistory.size() > 5) {
                m_finalHistory.removeFirst();
            }
            emit finalText(text);
        }
        if (!m_inSpeech) {
            emit partialText(QString());
        }
    } else if (m_inSpeech && job.segmentId == m_segmentId && !text.isEmpty()) {
        emit partialText(text);
    }

    dispatchNext();
    checkFinished();
}

QString RealtimeRecognizer::cleanupText(const QString &text) const
{
    QString result = text;
    // Служебные пометки whisper: [BLANK_AUDIO], (музыка), *шум*, ♪
    result.remove(QRegularExpression("\\[[^\\]]*\\]"));
    result.remove(QRegularExpression("\\([^\\)]*\\)"));
    result.remove(QRegularExpression("\\*[^\\*]*\\*"));
    result.remove(QChar(0x266A));
    result = result.simplified();

    // Типичные "галлюцинации" whisper на тишине и шуме
    const QString lower = result.toLower();
    if (lower.contains("субтитр") || lower.contains("dimatorzok") || lower.contains("amara.org")) {
        return QString();
    }
    QString bare = lower;
    bare.remove(QRegularExpression("[\\p{P}\\s]+"));
    static const QStringList junk = {
        "продолжениеследует", "спасибозапросмотр", "thankyouforwatching",
        "thanksforwatching", "ссылканаканал"
    };
    if (bare.isEmpty() || junk.contains(bare)) {
        return QString();
    }
    return result;
}

// ---------------------------------------------------------------------------
// WAV
// ---------------------------------------------------------------------------

static void writeWavHeader(uchar *h, quint32 dataBytes)
{
    std::memcpy(h, "RIFF", 4);
    qToLittleEndian<quint32>(36 + dataBytes, h + 4);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, h + 16);
    qToLittleEndian<quint16>(1, h + 20);                    // PCM
    qToLittleEndian<quint16>(1, h + 22);                    // моно
    qToLittleEndian<quint32>(kSampleRate, h + 24);
    qToLittleEndian<quint32>(kSampleRate * 2, h + 28);
    qToLittleEndian<quint16>(2, h + 32);
    qToLittleEndian<quint16>(16, h + 34);
    std::memcpy(h + 36, "data", 4);
    qToLittleEndian<quint32>(dataBytes, h + 40);
}

static inline qint16 toPcm16(float v)
{
    return static_cast<qint16>(qBound(-32768, static_cast<int>(std::lround(v * 32767.0f)), 32767));
}

QByteArray RealtimeRecognizer::makeWav(const QVector<float> &samples)
{
    const int count = qMax(samples.size(), kMinWhisperSamples);
    QByteArray wav(44 + count * 2, '\0');
    uchar *p = reinterpret_cast<uchar *>(wav.data());
    writeWavHeader(p, static_cast<quint32>(count * 2));
    for (int i = 0; i < samples.size(); ++i) {
        qToLittleEndian<qint16>(toPcm16(samples[i]), p + 44 + i * 2);
    }
    return wav;
}

void RealtimeRecognizer::writeRecording(const float *samples, int count)
{
    if (!m_recordingFile.isOpen()) {
        return;
    }
    QByteArray pcm(count * 2, '\0');
    uchar *p = reinterpret_cast<uchar *>(pcm.data());
    for (int i = 0; i < count; ++i) {
        qToLittleEndian<qint16>(toPcm16(samples[i]), p + i * 2);
    }
    m_recordingFile.write(pcm);
    m_recordedSamples += count;
}

void RealtimeRecognizer::closeRecording()
{
    if (!m_recordingFile.isOpen()) {
        return;
    }
    uchar header[44];
    writeWavHeader(header, static_cast<quint32>(m_recordedSamples * 2));
    m_recordingFile.seek(0);
    m_recordingFile.write(reinterpret_cast<const char *>(header), 44);
    m_recordingFile.close();
    emit log("Запись сохранена: " + m_recordingFile.fileName(), "INFO");
}
