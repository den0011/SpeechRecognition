#include "downloaddialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
const char *kReleasesApi = "https://api.github.com/repos/ggml-org/whisper.cpp/releases?per_page=30";
const char *kModelsBaseUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";
const char *kVadUrl = "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin";
const char *kVadFile = "ggml-silero-v6.2.0.bin";
const qint64 kVadSize = 885098;
const char *kUserAgent = "SpeechRecognition-Qt";
const char *kVariantFile = "variant.txt";
}

DownloadDialog::DownloadDialog(const QString &currentWhisperPath, const QString &currentModelPath,
                               QWidget *parent)
    : QDialog(parent)
    , m_process(nullptr)
    , m_step(Idle)
    , m_expectedSize(0)
    , m_lastSize(0)
    , m_lastTick(0)
    , m_assetSize(0)
    , m_whisperPath(currentWhisperPath)
    , m_modelPath(currentModelPath)
    , m_cancelled(false)
{
    setWindowTitle("Загрузка Whisper и моделей");
    setMinimumSize(700, 520);

    // Размеры нужны только для индикатора прогресса
    m_models = {
        { "ggml-tiny.bin",                "tiny — 75 МБ, самая быстрая, качество низкое",                   77691713LL },
        { "ggml-base-q5_1.bin",           "base-q5_1 — 57 МБ, качество как у base, файл в 2.5 раза меньше", 59707625LL },
        { "ggml-base.bin",                "base — 142 МБ, рекомендуется для реального времени",             147951465LL },
        { "ggml-small-q5_1.bin",          "small-q5_1 — 181 МБ, точнее base, нужен быстрый процессор",      190085487LL },
        { "ggml-small.bin",               "small — 466 МБ, точнее base, медленнее",                         487601967LL },
        { "ggml-medium-q5_0.bin",         "medium-q5_0 — 514 МБ, высокая точность, для видеокарты",         539212467LL },
        { "ggml-large-v3-turbo-q5_0.bin", "large-v3-turbo-q5_0 — 547 МБ, лучшее качество, для видеокарты",  574041195LL },
        { "ggml-medium.bin",              "medium — 1.5 ГБ, высокая точность, очень медленно на CPU",       1533763059LL },
        { "ggml-large-v3-turbo.bin",      "large-v3-turbo — 1.6 ГБ, лучшее качество, для видеокарты",       1624555275LL },
    };

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        "Программа скачает необходимые компоненты с официальных страниц проекта:<br>"
        "• whisper.cpp — <a href='https://github.com/ggml-org/whisper.cpp/releases'>github.com/ggml-org/whisper.cpp</a><br>"
        "• модели — <a href='https://huggingface.co/ggerganov/whisper.cpp'>huggingface.co/ggerganov/whisper.cpp</a><br><br>"
        "Папка установки: <b>" + QDir::toNativeSeparators(installDirectory()) + "</b>");
    intro->setOpenExternalLinks(true);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;

    m_binCheck = new QCheckBox("whisper-cli + whisper-server");
    m_variantCombo = new QComboBox;
    const bool win32 = QSysInfo::buildCpuArchitecture() == "i386";
    m_variantCombo->addItem("Процессор (CPU) — ~9 МБ, работает везде", "cpu");
    m_variantCombo->addItem("Процессор + OpenBLAS — ~21 МБ, обычно быстрее обычной", "blas");
    if (!win32) {
        m_variantCombo->addItem("Видеокарта NVIDIA, CUDA 12 — ~650 МБ, в разы быстрее", "cuda12");
        m_variantCombo->addItem("Видеокарта NVIDIA, CUDA 11.8 — ~270 МБ, для старых драйверов", "cuda11");
    }
    m_binStatus = new QLabel;
    m_binStatus->setWordWrap(true);
    auto *binBox = new QVBoxLayout;
    auto *binRow = new QHBoxLayout;
    binRow->addWidget(m_binCheck);
    binRow->addWidget(m_variantCombo, 1);
    binBox->addLayout(binRow);
    binBox->addWidget(m_binStatus);
    if (hasNvidiaGpu() && !win32) {
        auto *gpuHint = new QLabel("💡 Обнаружена видеокарта NVIDIA — сборка CUDA распознаёт в разы быстрее");
        gpuHint->setWordWrap(true);
        binBox->addWidget(gpuHint);
    }
    form->addRow("Программа:", binBox);

    m_modelCombo = new QComboBox;
    m_modelCombo->addItem("Не скачивать модель", QString());
    for (const ModelInfo &model : m_models) {
        m_modelCombo->addItem(model.title, model.file);
    }
    m_modelStatus = new QLabel;
    auto *modelBox = new QVBoxLayout;
    modelBox->addWidget(m_modelCombo);
    modelBox->addWidget(m_modelStatus);
    form->addRow("Модель:", modelBox);

    m_vadCheck = new QCheckBox("Фильтр шума Silero VAD (0.9 МБ)");
    m_vadCheck->setToolTip("Отсекает шум и музыку, уменьшает «выдуманный» текст. Включается в настройках.");
    form->addRow("Шумоподавление:", m_vadCheck);
    layout->addLayout(form);

    m_stepLabel = new QLabel;
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    layout->addWidget(m_stepLabel);
    layout->addWidget(m_progress);

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setStyleSheet("background-color: #f5f5f5; color: #333;");
    layout->addWidget(m_log, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_startButton = new QPushButton("⬇ Скачать");
    m_closeButton = new QPushButton("Закрыть");
    buttons->addWidget(m_startButton);
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);

    connect(m_startButton, &QPushButton::clicked, this, &DownloadDialog::onStartClicked);
    connect(m_closeButton, &QPushButton::clicked, this, &DownloadDialog::onCancelClicked);
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshState(); });
    connect(m_variantCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshState(); });
    connect(m_binCheck, &QCheckBox::toggled, this, [this]() { refreshState(); });
    connect(&m_progressTimer, &QTimer::timeout, this, &DownloadDialog::onProgressTick);
    m_progressTimer.setInterval(300);

    // Начальное состояние: отмечаем то, чего не хватает
    const bool haveBinaries = !findInstalledWhisper().isEmpty() || QFile::exists(currentWhisperPath);
    m_binCheck->setChecked(!haveBinaries);
    const int variantIndex = m_variantCombo->findData(installedVariant());
    if (variantIndex >= 0) {
        m_variantCombo->setCurrentIndex(variantIndex);
    }
    m_modelCombo->setCurrentIndex(QFile::exists(currentModelPath) ? 0 : m_modelCombo->findData("ggml-base.bin"));
    m_vadCheck->setChecked(!QFile::exists(vadModelPath()));

    refreshState();
}

DownloadDialog::~DownloadDialog()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

QString DownloadDialog::whisperPath() const
{
    return m_whisperPath;
}

QString DownloadDialog::modelPath() const
{
    return m_modelPath;
}

QString DownloadDialog::installDirectory()
{
    // Рядом с программой; если туда нельзя писать (Program Files) — в профиль пользователя
    const QString appDir = QCoreApplication::applicationDirPath() + "/whisper.cpp";
    if (QDir().mkpath(appDir)) {
        QFile probe(appDir + "/.write_test");
        if (probe.open(QIODevice::WriteOnly)) {
            probe.close();
            probe.remove();
            return appDir;
        }
    }
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/whisper.cpp";
    QDir().mkpath(fallback);
    return fallback;
}

QString DownloadDialog::findInstalledWhisper()
{
    QDirIterator it(installDirectory() + "/bin", QStringList() << "whisper-cli.exe" << "whisper-cli",
                    QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

QString DownloadDialog::findInstalledModel()
{
    // Предпочтение — base, затем любая другая модель распознавания (не VAD)
    const QDir modelsDir(installDirectory() + "/models");
    const QStringList preferred = { "ggml-base.bin", "ggml-base-q5_1.bin", "ggml-small-q5_1.bin", "ggml-small.bin" };
    for (const QString &name : preferred) {
        if (modelsDir.exists(name)) {
            return modelsDir.filePath(name);
        }
    }
    for (const QString &name : modelsDir.entryList(QStringList() << "ggml-*.bin", QDir::Files)) {
        if (!name.contains("silero")) {
            return modelsDir.filePath(name);
        }
    }
    return QString();
}

QString DownloadDialog::vadModelPath()
{
    return installDirectory() + "/models/" + kVadFile;
}

QString DownloadDialog::installedVariant()
{
    QFile file(installDirectory() + "/bin/" + kVariantFile);
    if (file.open(QIODevice::ReadOnly)) {
        return QString::fromUtf8(file.readAll()).trimmed();
    }
    return QString();
}

bool DownloadDialog::hasNvidiaGpu()
{
#ifdef Q_OS_WIN
    // nvidia-smi ставится вместе с драйвером NVIDIA
    return QFile::exists("C:/Windows/System32/nvidia-smi.exe") ||
           !QStandardPaths::findExecutable("nvidia-smi").isEmpty();
#else
    return !QStandardPaths::findExecutable("nvidia-smi").isEmpty();
#endif
}

QString DownloadDialog::selectedVariant() const
{
    return m_variantCombo->currentData().toString();
}

bool DownloadDialog::assetMatches(const QString &assetName) const
{
    const QString name = assetName.toLower();
    const bool win32 = QSysInfo::buildCpuArchitecture() == "i386";
    const QString variant = selectedVariant();
    if (variant == "cpu") {
        return name == (win32 ? "whisper-bin-win32.zip" : "whisper-bin-x64.zip");
    }
    if (variant == "blas") {
        return name == (win32 ? "whisper-blas-bin-win32.zip" : "whisper-blas-bin-x64.zip");
    }
    // Имена CUDA-сборок менялись: whisper-cublas-12.4.0-bin-x64.zip, whisper-bin-win-cuda-12.4.0-x64.zip
    const QString cudaMajor = variant == "cuda12" ? "12" : "11";
    return name.endsWith(".zip") && name.contains("x64") && !name.contains("arm64") &&
           (name.contains("cuda-" + cudaMajor) || name.contains("cublas-" + cudaMajor));
}

QString DownloadDialog::formatSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " ГБ";
    }
    return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " МБ";
}

QString DownloadDialog::selectedModelFile() const
{
    return m_modelCombo->currentData().toString();
}

qint64 DownloadDialog::selectedModelSize() const
{
    const QString file = selectedModelFile();
    for (const ModelInfo &model : m_models) {
        if (model.file == file) {
            return model.size;
        }
    }
    return 0;
}

void DownloadDialog::refreshState()
{
    m_variantCombo->setEnabled(m_binCheck->isChecked() && m_step == Idle);

    const QString installedCli = findInstalledWhisper();
    if (!installedCli.isEmpty()) {
        const int index = m_variantCombo->findData(installedVariant());
        const QString variantName = index >= 0 ? m_variantCombo->itemText(index).section(" — ", 0, 0) : QString();
        m_binStatus->setText("✓ Установлена" + (variantName.isEmpty() ? QString() : " (" + variantName + ")")
                             + ": " + QDir::toNativeSeparators(installedCli));
    } else if (QFile::exists(m_whisperPath)) {
        m_binStatus->setText("✓ Используется: " + QDir::toNativeSeparators(m_whisperPath));
    } else {
        m_binStatus->setText("❌ Не найдена");
    }

    const QString file = selectedModelFile();
    if (file.isEmpty()) {
        m_modelStatus->setText(QFile::exists(m_modelPath)
                               ? "✓ Используется: " + QDir::toNativeSeparators(m_modelPath)
                               : "❌ Модель не выбрана");
    } else if (QFile::exists(installDirectory() + "/models/" + file)) {
        m_modelStatus->setText("✓ Уже скачана — будет просто выбрана");
    } else {
        m_modelStatus->setText("Будет скачано " + formatSize(selectedModelSize()));
    }

    if (QFile::exists(vadModelPath())) {
        m_vadCheck->setText("Фильтр шума Silero VAD — ✓ уже скачан");
    }
}

void DownloadDialog::appendLog(const QString &message)
{
    m_log->append("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] " + message);
}

void DownloadDialog::setBusy(bool busy)
{
    m_startButton->setEnabled(!busy);
    m_binCheck->setEnabled(!busy);
    m_variantCombo->setEnabled(!busy && m_binCheck->isChecked());
    m_modelCombo->setEnabled(!busy);
    m_vadCheck->setEnabled(!busy);
    m_closeButton->setText(busy ? "Отмена" : "Закрыть");
}

void DownloadDialog::onStartClicked()
{
    const bool wantVad = m_vadCheck->isChecked() && !QFile::exists(vadModelPath());
    if (!m_binCheck->isChecked() && selectedModelFile().isEmpty() && !wantVad) {
        appendLog("Нечего скачивать: отметьте программу, выберите модель или фильтр шума");
        return;
    }
    if (QStandardPaths::findExecutable("curl").isEmpty()) {
        fail("Не найден curl. Он входит в Windows 10 (версии 1803 и новее) и Windows 11.");
        return;
    }

    m_cancelled = false;
    setBusy(true);
    m_step = Idle;
    nextStep();
}

void DownloadDialog::onCancelClicked()
{
    if (m_step == Idle) {
        accept();
        return;
    }

    m_cancelled = true;
    m_progressTimer.stop();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (!m_downloadTarget.isEmpty()) {
        QFile::remove(m_downloadTarget);
    }
    m_step = Idle;
    setBusy(false);
    m_stepLabel->setText("Отменено");
    m_progress->setValue(0);
    appendLog("Загрузка отменена");
}

void DownloadDialog::nextStep()
{
    if (m_cancelled) {
        return;
    }

    switch (m_step) {
    case Idle:
        if (m_binCheck->isChecked()) {
            fetchRelease();
        } else {
            startModelDownload();
        }
        return;
    case FetchRelease:
        m_step = DownloadBinaries;
        m_stepLabel->setText("Скачивание whisper.cpp " + m_releaseTag + "...");
        downloadFile(m_assetUrl, installDirectory() + "/" + m_assetName, m_assetSize);
        return;
    case DownloadBinaries:
        extractBinaries();
        return;
    case ExtractBinaries:
        startModelDownload();
        return;
    case DownloadModel:
        startVadDownload();
        return;
    case DownloadVad:
        finishAll();
        return;
    }
}

void DownloadDialog::startModelDownload()
{
    m_step = DownloadModel;
    const QString file = selectedModelFile();
    if (file.isEmpty()) {
        startVadDownload();
        return;
    }
    const QString modelsDir = installDirectory() + "/models";
    QDir().mkpath(modelsDir);
    const QString target = modelsDir + "/" + file;
    if (QFile::exists(target)) {
        appendLog("Модель уже скачана: " + QDir::toNativeSeparators(target));
        m_modelPath = target;
        startVadDownload();
        return;
    }

    m_stepLabel->setText("Скачивание модели " + file + "...");
    downloadFile(QString(kModelsBaseUrl) + file, target + ".part", selectedModelSize());
}

void DownloadDialog::startVadDownload()
{
    m_step = DownloadVad;
    if (!m_vadCheck->isChecked() || QFile::exists(vadModelPath())) {
        finishAll();
        return;
    }
    QDir().mkpath(installDirectory() + "/models");
    m_stepLabel->setText("Скачивание фильтра шума Silero VAD...");
    downloadFile(kVadUrl, vadModelPath() + ".part", kVadSize);
}

void DownloadDialog::fetchRelease()
{
    m_step = FetchRelease;
    m_downloadTarget.clear();
    m_assetUrl.clear();
    m_assetName.clear();
    m_stepLabel->setText("Поиск последней версии whisper.cpp...");
    m_progress->setRange(0, 0);
    appendLog("Запрос списка релизов на GitHub");

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DownloadDialog::onProcessFinished);
    m_process->start("curl", QStringList() << "-sL" << "--fail" << "-A" << kUserAgent << kReleasesApi);
}

void DownloadDialog::downloadFile(const QString &url, const QString &target, qint64 expectedSize)
{
    m_downloadTarget = target;
    m_expectedSize = expectedSize;
    m_lastSize = 0;
    m_lastTick = QDateTime::currentMSecsSinceEpoch();
    QFile::remove(target);

    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    appendLog("Скачивание " + url);

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DownloadDialog::onProcessFinished);
    m_process->start("curl", QStringList() << "-L" << "--fail" << "-s" << "-S"
                                           << "--retry" << "3"
                                           << "-A" << kUserAgent
                                           << "-o" << target << url);
    m_progressTimer.start();
}

void DownloadDialog::onProgressTick()
{
    const qint64 size = QFileInfo(m_downloadTarget).size();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double seconds = qMax<qint64>(1, now - m_lastTick) / 1000.0;
    const double speed = (size - m_lastSize) / seconds;
    m_lastSize = size;
    m_lastTick = now;

    if (m_expectedSize > 0) {
        m_progress->setValue(qBound(0, static_cast<int>(size * 100 / m_expectedSize), 99));
        m_progress->setFormat(QString("%1 из %2 — %3/с")
                              .arg(formatSize(size), formatSize(m_expectedSize), formatSize(qint64(speed))));
    } else {
        m_progress->setFormat(formatSize(size));
    }
}

void DownloadDialog::extractBinaries()
{
    m_step = ExtractBinaries;
    m_progressTimer.stop();
    m_progress->setRange(0, 0);
    m_stepLabel->setText("Распаковка...");

    const QString archive = installDirectory() + "/" + m_assetName;
    const QString binDir = installDirectory() + "/bin";
    QDir(binDir).removeRecursively();
    QDir().mkpath(binDir);

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DownloadDialog::onProcessFinished);

    if (!QStandardPaths::findExecutable("tar").isEmpty()) {
        m_process->start("tar", QStringList() << "-xf" << archive << "-C" << binDir);
    } else {
        m_process->start("powershell", QStringList() << "-NoProfile" << "-Command"
                         << QString("Expand-Archive -Force -LiteralPath '%1' -DestinationPath '%2'")
                            .arg(QDir::toNativeSeparators(archive), QDir::toNativeSeparators(binDir)));
    }
}

void DownloadDialog::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_progressTimer.stop();
    const QByteArray output = m_process->readAllStandardOutput();
    const QString errors = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
    m_process->deleteLater();
    m_process = nullptr;

    if (m_cancelled) {
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (!m_downloadTarget.isEmpty() && m_step != ExtractBinaries) {
            QFile::remove(m_downloadTarget);
        }
        fail(QString("Ошибка (код %1)%2").arg(exitCode).arg(errors.isEmpty() ? QString() : ": " + errors));
        return;
    }

    switch (m_step) {
    case FetchRelease: {
        // У тегов vX.Y.Z файлов может не быть — берём первый релиз, где есть нужная сборка
        const QJsonArray releases = QJsonDocument::fromJson(output).array();
        for (const QJsonValue &release : releases) {
            const QJsonObject rel = release.toObject();
            if (rel.value("draft").toBool()) {
                continue;
            }
            for (const QJsonValue &asset : rel.value("assets").toArray()) {
                const QJsonObject a = asset.toObject();
                if (assetMatches(a.value("name").toString())) {
                    m_assetName = a.value("name").toString();
                    m_assetUrl = a.value("browser_download_url").toString();
                    m_assetSize = static_cast<qint64>(a.value("size").toDouble());
                    m_releaseTag = rel.value("tag_name").toString();
                    break;
                }
            }
            if (!m_assetUrl.isEmpty()) {
                break;
            }
        }
        if (m_assetUrl.isEmpty()) {
            fail("В релизах whisper.cpp не найдена сборка: " + m_variantCombo->currentText());
            return;
        }
        appendLog("Найдена версия " + m_releaseTag + ": " + m_assetName + " (" + formatSize(m_assetSize) + ")");
        break;
    }
    case DownloadBinaries:
        appendLog("Архив скачан");
        m_downloadTarget.clear();
        break;
    case ExtractBinaries: {
        QFile::remove(installDirectory() + "/" + m_assetName);
        const QString cli = findInstalledWhisper();
        if (cli.isEmpty()) {
            fail("В архиве не найден whisper-cli");
            return;
        }
        QFile variantFile(installDirectory() + "/bin/" + kVariantFile);
        if (variantFile.open(QIODevice::WriteOnly)) {
            variantFile.write(selectedVariant().toUtf8());
        }
        m_whisperPath = cli;
        appendLog("Установлено: " + QDir::toNativeSeparators(cli));
        if (QFile::exists(QFileInfo(cli).dir().filePath("whisper-server.exe")) ||
            QFile::exists(QFileInfo(cli).dir().filePath("whisper-server"))) {
            appendLog("whisper-server найден — быстрый режим реального времени доступен");
        }
        break;
    }
    case DownloadModel:
    case DownloadVad: {
        const QString finalPath = m_downloadTarget.left(m_downloadTarget.size() - 5); // без ".part"
        QFile::remove(finalPath);
        if (!QFile::rename(m_downloadTarget, finalPath)) {
            fail("Не удалось сохранить файл: " + finalPath);
            return;
        }
        m_downloadTarget.clear();
        if (m_step == DownloadModel) {
            m_modelPath = finalPath;
            appendLog("Модель сохранена: " + QDir::toNativeSeparators(finalPath));
        } else {
            appendLog("Фильтр шума сохранён. Включите его в «Файл → Настройки → Шумоподавление».");
        }
        break;
    }
    case Idle:
        return;
    }

    nextStep();
}

void DownloadDialog::finishAll()
{
    m_step = Idle;
    setBusy(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(100);
    m_progress->setFormat("%p%");
    m_stepLabel->setText("✅ Готово");
    m_closeButton->setText("Готово");
    m_binCheck->setChecked(false);
    m_modelCombo->setCurrentIndex(0);
    m_vadCheck->setChecked(false);
    appendLog("Все компоненты готовы. Пути сохранятся в настройках после закрытия окна.");
    refreshState();
}

void DownloadDialog::fail(const QString &message)
{
    m_step = Idle;
    setBusy(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFormat("%p%");
    m_stepLabel->setText("❌ " + message);
    appendLog("ОШИБКА: " + message);
}
