#include "speechrecognizer.h"

#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QDateTime>

SpeechRecognizer::SpeechRecognizer(QObject *parent)
    : QObject(parent)
    , m_audioRecorder(new QAudioRecorder(this))
    , m_whisperProcess(nullptr)
    , m_language("ru")
    , m_audioDeviceName("")
    , m_sampleRate(16000)
    , m_bitRate(256000)
    , m_channelCount(1)
{
    m_whisperPath = "./whisper.cpp/whisper-cli.exe";
    m_modelPath = "./whisper.cpp/models/ggml-base.bin";

    // Применяем настройки по умолчанию
    applyAudioSettings();

    // Устанавливаем устройство ввода по умолчанию
    QAudioDeviceInfo defaultDevice = QAudioDeviceInfo::defaultInputDevice();
    if (!defaultDevice.isNull()) {
        m_audioRecorder->setAudioInput(defaultDevice.deviceName());
        m_audioDeviceName = defaultDevice.deviceName();
        qDebug() << "Default audio device:" << m_audioDeviceName;
    }

    connect(m_audioRecorder, &QAudioRecorder::stateChanged,
            this, [this](QMediaRecorder::State state) {
        qDebug() << "Audio recorder state changed:" << state;
        if (state == QMediaRecorder::StoppedState) {
            onRecordingFinished();
        }
    });
    
    connect(m_audioRecorder, QOverload<QMediaRecorder::Error>::of(&QAudioRecorder::error),
            this, [this](QMediaRecorder::Error error) {
        qDebug() << "Audio recorder error:" << error << m_audioRecorder->errorString();
    });
}

SpeechRecognizer::~SpeechRecognizer()
{
    if (m_audioRecorder->state() == QMediaRecorder::RecordingState) {
        m_audioRecorder->stop();
    }

    if (m_whisperProcess) {
        m_whisperProcess->kill();
        m_whisperProcess->waitForFinished();
    }
}

void SpeechRecognizer::setWhisperPath(const QString &path)
{
    m_whisperPath = path;
}

void SpeechRecognizer::setModelPath(const QString &path)
{
    m_modelPath = path;
}

void SpeechRecognizer::setLanguage(const QString &lang)
{
    m_language = lang;
}

void SpeechRecognizer::setAudioDevice(const QString &deviceName)
{
    if (!deviceName.isEmpty()) {
        m_audioDeviceName = deviceName;
        m_audioRecorder->setAudioInput(deviceName);
        qDebug() << "Audio device set to:" << deviceName;
    }
}

void SpeechRecognizer::setAudioSettings(int sampleRate, int bitRate, int channels)
{
    m_sampleRate = sampleRate;
    m_bitRate = bitRate;
    m_channelCount = channels;
    
    applyAudioSettings();
    
    qDebug() << "Audio settings updated:"
             << "Sample rate:" << m_sampleRate
             << "Bit rate:" << m_bitRate
             << "Channels:" << m_channelCount;
}

void SpeechRecognizer::applyAudioSettings()
{
    QAudioEncoderSettings audioSettings;
    audioSettings.setCodec("audio/pcm");
    audioSettings.setSampleRate(m_sampleRate);
    audioSettings.setChannelCount(m_channelCount);
    audioSettings.setBitRate(m_bitRate);
    audioSettings.setQuality(QMultimedia::HighQuality);

    m_audioRecorder->setEncodingSettings(audioSettings);
    m_audioRecorder->setContainerFormat("audio/x-wav");
}

QString SpeechRecognizer::getWhisperPath() const
{
    return m_whisperPath;
}

QString SpeechRecognizer::getModelPath() const
{
    return m_modelPath;
}

QString SpeechRecognizer::getLanguage() const
{
    return m_language;
}

QString SpeechRecognizer::getAudioDevice() const
{
    return m_audioDeviceName;
}

QStringList SpeechRecognizer::getAvailableAudioDevices() const
{
    QStringList devices;
    QList<QAudioDeviceInfo> audioDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    
    for (const QAudioDeviceInfo &deviceInfo : audioDevices) {
        devices.append(deviceInfo.deviceName());
    }
    
    qDebug() << "Available audio devices:" << devices;
    return devices;
}

void SpeechRecognizer::startRecording()
{
    QAudioDeviceInfo deviceInfo;
    QList<QAudioDeviceInfo> audioDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    
    if (audioDevices.isEmpty()) {
        emit error("Не найдено ни одного устройства записи звука. Подключите микрофон.");
        return;
    }
    
    bool deviceFound = false;
    for (const QAudioDeviceInfo &device : audioDevices) {
        if (device.deviceName() == m_audioDeviceName) {
            deviceInfo = device;
            deviceFound = true;
            break;
        }
    }
    
    if (!deviceFound) {
        deviceInfo = QAudioDeviceInfo::defaultInputDevice();
        m_audioDeviceName = deviceInfo.deviceName();
        qDebug() << "Selected device not found, using default:" << m_audioDeviceName;
    }
    
    // Применяем текущие настройки аудио
    applyAudioSettings();
    
    // Проверяем поддержку формата
    QAudioFormat format;
    format.setSampleRate(m_sampleRate);
    format.setChannelCount(m_channelCount);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    
    if (!deviceInfo.isFormatSupported(format)) {
        qDebug() << "Requested format not supported, using nearest format";
        format = deviceInfo.nearestFormat(format);
    }
    
    QString appDir = QCoreApplication::applicationDirPath();
    QString recordingsDir = appDir + "/recordings";
    
    QDir dir;
    if (!dir.exists(recordingsDir)) {
        if (!dir.mkpath(recordingsDir)) {
            qDebug() << "Failed to create recordings directory, using temp";
            recordingsDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        } else {
            qDebug() << "Created recordings directory:" << recordingsDir;
        }
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_tempAudioFile = recordingsDir + "/recording_" + timestamp + ".wav";
    
    QFile::remove(m_tempAudioFile);

    m_audioRecorder->setOutputLocation(QUrl::fromLocalFile(m_tempAudioFile));
    m_audioRecorder->record();

    emit recordingStarted();
    qDebug() << "Recording started to:" << m_tempAudioFile;
    qDebug() << "Using device:" << m_audioDeviceName;
    qDebug() << "Settings:" << m_sampleRate << "Hz," << m_bitRate << "bps," << m_channelCount << "ch";
}

void SpeechRecognizer::stopRecording()
{
    if (m_audioRecorder->state() == QMediaRecorder::RecordingState) {
        m_audioRecorder->stop();
        emit recordingStopped();
        qDebug() << "Recording stopped";
    }
}

void SpeechRecognizer::onRecordingFinished()
{
    qDebug() << "Recording finished, audio file:" << m_tempAudioFile;
    
    QFile audioFile(m_tempAudioFile);
    if (!audioFile.exists()) {
        emit error("Аудио файл не был создан. Проверьте права доступа к микрофону.");
        return;
    }
    
    qint64 fileSize = audioFile.size();
    qDebug() << "Audio file size:" << fileSize << "bytes";
    
    if (fileSize < 1000) {
        emit error("Аудио файл слишком мал. Возможно, микрофон не записывает звук.");
        return;
    }
    
    recognizeFromFile(m_tempAudioFile);
}

void SpeechRecognizer::recognizeFromFile(const QString &audioFile)
{
    if (!QFile::exists(audioFile)) {
        emit error("Аудио файл не найден: " + audioFile);
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

    if (m_whisperProcess) {
        m_whisperProcess->deleteLater();
    }

    m_whisperProcess = new QProcess(this);

    connect(m_whisperProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SpeechRecognizer::onWhisperFinished);
    connect(m_whisperProcess, &QProcess::errorOccurred,
            this, &SpeechRecognizer::onWhisperError);

    QStringList arguments;
    arguments << "-m" << m_modelPath;
    arguments << "-f" << audioFile;
    arguments << "-l" << m_language;
    arguments << "--no-timestamps";
    arguments << "-nt";

    qDebug() << "Running:" << m_whisperPath << arguments;

    m_whisperProcess->start(m_whisperPath, arguments);
    
    if (!m_whisperProcess->waitForStarted(3000)) {
        emit error("Не удалось запустить Whisper. Проверьте путь к исполняемому файлу.");
    }
}

void SpeechRecognizer::onWhisperFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QString output = m_whisperProcess->readAllStandardOutput();
    QString errorOutput = m_whisperProcess->readAllStandardError();
    
    qDebug() << "Whisper exit code:" << exitCode;
    qDebug() << "Whisper stdout:" << output;
    qDebug() << "Whisper stderr:" << errorOutput;

    bool isDeprecatedWarning = (exitCode == 1 && 
                                output.contains("deprecated") && 
                                output.contains("whisper-cli"));

    if (exitStatus == QProcess::NormalExit && (exitCode == 0 || isDeprecatedWarning)) {
        if (isDeprecatedWarning) {
            qDebug() << "Note: Using deprecated main.exe, recommend switching to whisper-cli.exe";
        }
        
        QStringList lines = output.split('\n', QString::SkipEmptyParts);
        QString recognizedText;

        for (const QString &line : lines) {
            QString trimmedLine = line.trimmed();
            if (!trimmedLine.contains("[") && 
                !trimmedLine.startsWith("whisper_") &&
                !trimmedLine.contains("Processing") &&
                !trimmedLine.contains("WARNING") &&
                !trimmedLine.contains("deprecated") &&
                !trimmedLine.contains("Please use") &&
                !trimmedLine.contains("See https") &&
                !trimmedLine.isEmpty()) {
                recognizedText += trimmedLine + " ";
            }
        }

        recognizedText = recognizedText.trimmed();

        if (!recognizedText.isEmpty()) {
            emit recognitionComplete(recognizedText);
        } else {
            if (isDeprecatedWarning) {
                emit error("Текст не распознан. Whisper работает, но не обнаружил речи.\n"
                          "Примечание: Рекомендуется использовать whisper-cli.exe вместо main.exe");
            } else {
                emit error("Текст не распознан. Возможно, в записи нет речи или она слишком тихая.");
            }
        }
    } else if (!isDeprecatedWarning) {
        QString errorMsg = "Ошибка Whisper (код: " + QString::number(exitCode) + ")";
        
        if (!errorOutput.isEmpty()) {
            errorMsg += "\nДетали: " + errorOutput;
        }
        
        if (errorOutput.contains("Could not open")) {
            errorMsg = "Не удалось открыть аудио файл. Возможно, формат не поддерживается.";
        } else if (errorOutput.contains("model")) {
            errorMsg = "Ошибка загрузки модели. Проверьте правильность файла модели.";
        }
        
        emit error(errorMsg);
    }
}

void SpeechRecognizer::onWhisperError(QProcess::ProcessError error)
{
    QString errorMsg;
    switch (error) {
        case QProcess::FailedToStart:
            errorMsg = "Не удалось запустить Whisper. Проверьте:\n"
                      "1. Правильность пути к исполняемому файлу\n"
                      "2. Используйте whisper-cli.exe вместо main.exe\n"
                      "3. Права на выполнение (Linux/Mac: chmod +x)";
            break;
        case QProcess::Crashed:
            errorMsg = "Процесс Whisper завершился аварийно";
            break;
        case QProcess::Timedout:
            errorMsg = "Превышено время ожидания Whisper";
            break;
        case QProcess::ReadError:
            errorMsg = "Ошибка чтения вывода Whisper";
            break;
        case QProcess::WriteError:
            errorMsg = "Ошибка записи в Whisper";
            break;
        default:
            errorMsg = "Неизвестная ошибка процесса Whisper: " + QString::number(error);
    }

    emit this->error(errorMsg);
}
