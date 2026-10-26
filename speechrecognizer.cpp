#include "speechrecognizer.h"
#include "audiodevices.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

QString SpeechRecognizer::resolvePath(const QString &path)
{
    // Относительные пути считаются от папки программы, а не от текущей рабочей папки,
    // иначе при запуске из ярлыка whisper не находится
    if (path.isEmpty() || QFileInfo(path).isAbsolute()) {
        return path;
    }
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(path));
}

int SpeechRecognizer::recommendedWhisperThreads() const
{
    const int idealThreads = QThread::idealThreadCount();
    const int cappedThreads = (m_performanceMode == AccurateMode) ? 4 : 8;

    if (idealThreads <= 2) {
        return qMax(1, idealThreads);
    }

    if (idealThreads <= 4) {
        return idealThreads - 1;
    }

    return qMin(idealThreads, cappedThreads);
}

int SpeechRecognizer::beamSize() const
{
    // Режим «Точно» — поиск по 5 вариантам (beam search): точнее, примерно на 25% медленнее
    return m_performanceMode == AccurateMode ? 5 : 0;
}

SpeechRecognizer::SpeechRecognizer(QObject *parent)
    : QObject(parent)
    , m_whisperProcess(nullptr)
    , m_language("ru")
    , m_performanceMode(BalancedMode)
{
    m_whisperPath = resolvePath("whisper.cpp/whisper-cli.exe");
    m_modelPath = resolvePath("whisper.cpp/models/ggml-base.bin");
}

SpeechRecognizer::~SpeechRecognizer()
{
    if (m_whisperProcess) {
        m_whisperProcess->kill();
        m_whisperProcess->waitForFinished();
    }
}

void SpeechRecognizer::setWhisperPath(const QString &path)
{
    m_whisperPath = resolvePath(path);
}

void SpeechRecognizer::setModelPath(const QString &path)
{
    m_modelPath = resolvePath(path);
}

void SpeechRecognizer::setLanguage(const QString &lang)
{
    m_language = lang;
}

void SpeechRecognizer::setAudioDevice(const QString &deviceName)
{
    const QStringList devices = AudioDevices::inputNames();
    const int index = AudioDevices::matchName(devices, deviceName);
    m_audioDeviceName = index >= 0 ? devices.at(index) : deviceName;
}

void SpeechRecognizer::setPerformanceMode(PerformanceMode mode)
{
    m_performanceMode = mode;
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

SpeechRecognizer::PerformanceMode SpeechRecognizer::getPerformanceMode() const
{
    return m_performanceMode;
}

QStringList SpeechRecognizer::getAvailableAudioDevices() const
{
    const QStringList devices = AudioDevices::inputNames();
    qDebug() << "Available audio devices:" << devices;
    return devices;
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
    arguments << "-t" << QString::number(recommendedWhisperThreads());
    if (beamSize() > 0) {
        arguments << "-bs" << QString::number(beamSize());
    }
    arguments << "--no-timestamps";
    arguments << "-nt";

    m_recognitionTimer.restart();
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
    const qint64 elapsedMs = m_recognitionTimer.isValid() ? m_recognitionTimer.elapsed() : -1;

    qDebug() << "Whisper exit code:" << exitCode;
    if (elapsedMs >= 0) {
        qDebug() << "Whisper processing time:" << elapsedMs << "ms";
    }
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
