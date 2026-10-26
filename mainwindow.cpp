#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "settingsdialog.h"
#include "audioconfigdialog.h"
#include "downloaddialog.h"

#include <QClipboard>
#include <QGuiApplication>

#include <QDebug>
#include <QDateTime>
#include <QSettings>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include <QScrollBar>
#include <QThread>
#include <QTextBlock>
#include <QAudioDeviceInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_useVad(false)
    , m_dictationStarted(false)
    , m_isRecording(false)
{
    ui->setupUi(this);

    // Создаем распознаватель речи
    m_recognizer = new SpeechRecognizer(this);
    m_realtime = new RealtimeRecognizer(this);
    m_hotkey = new GlobalHotkey(this);
    connect(m_hotkey, &GlobalHotkey::activated, this, &MainWindow::onHotkeyActivated);

    // Инициализируем таймер записи
    m_recordingTimer = new QTimer(this);
    connect(m_recordingTimer, &QTimer::timeout, this, &MainWindow::updateRecordingTimer);

    // Загружаем настройки
    loadSettings();

    // Проверяем наличие модели
    checkModelExists();
    applyHotkey();

    connect(ui->copyTextButton, &QPushButton::clicked, this, &MainWindow::onCopyTextClicked);
    connect(ui->saveTextButton, &QPushButton::clicked, this, &MainWindow::onSaveTextClicked);
    connect(ui->clearTextButton, &QPushButton::clicked, ui->textEdit, &QTextEdit::clear);

    // Подключаем сигналы от кнопки записи
    connect(ui->recordButton, &QPushButton::clicked,
            this, &MainWindow::onRecordButtonClicked);

    // Подключаем кнопку очистки логов
    connect(ui->clearLogsButton, &QPushButton::clicked,
            this, &MainWindow::onClearLogsClicked);

    // Подключаем сигналы от распознавателя
    connect(m_recognizer, &SpeechRecognizer::recognitionComplete,
            this, &MainWindow::onRecognitionComplete);
    connect(m_recognizer, &SpeechRecognizer::error,
            this, &MainWindow::onError);

    // Сигналы режима реального времени
    connect(m_realtime, &RealtimeRecognizer::started,
            this, &MainWindow::onRecordingStarted);
    connect(m_realtime, &RealtimeRecognizer::stopped,
            this, &MainWindow::onRecordingStopped);
    connect(m_realtime, &RealtimeRecognizer::finished,
            this, &MainWindow::onRealtimeFinished);
    connect(m_realtime, &RealtimeRecognizer::finalText,
            this, &MainWindow::onRealtimeFinalText);
    connect(m_realtime, &RealtimeRecognizer::partialText,
            ui->partialLabel, &QLabel::setText);
    connect(m_realtime, &RealtimeRecognizer::levelChanged,
            ui->levelBar, &QProgressBar::setValue);
    connect(m_realtime, &RealtimeRecognizer::error,
            this, &MainWindow::onError);
    connect(m_realtime, &RealtimeRecognizer::log,
            this, &MainWindow::addLog);
    connect(m_realtime, &RealtimeRecognizer::noSignal,
            this, &MainWindow::onNoSignal);

    // Подключаем пункты меню
    connect(ui->actionSettings, &QAction::triggered,
            this, &MainWindow::onSettingsClicked);
    QAction *downloadAction = new QAction("⬇ Скачать Whisper и модели", this);
    downloadAction->setToolTip("Автоматически скачать whisper.cpp и модель распознавания");
    ui->menuFile->insertAction(ui->actionSettings, downloadAction);
    connect(downloadAction, &QAction::triggered, this, &MainWindow::onDownloadClicked);
    connect(ui->actionOpenRecordings, &QAction::triggered,
            this, &MainWindow::onOpenRecordingsClicked);
    connect(ui->actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "О программе",
            "<h3>Голос в текст - Whisper</h3>"
            "<p>Версия 1.4</p>"
            "<p>Программа для распознавания речи с использованием Whisper.cpp</p>"
            "<p>Исходный код: <a href='https://github.com/den0011/SpeechRecognition'>"
            "github.com/den0011/SpeechRecognition</a></p>"
            "<br>"
            "<p><b>Возможности:</b></p>"
            "<ul>"
            "<li>Распознавание в реальном времени — текст появляется во время речи</li>"
            "<li>Диктовка в любую программу по горячей клавише</li>"
            "<li>Фильтр шума Silero VAD, автоопределение языка</li>"
            "<li>Копирование и сохранение текста, отметки времени</li>"
"<li>Запись голоса с микрофона</li>"
            "<li>Распознавание аудио файлов</li>"
            "<li>Настройка параметров микрофона</li>"
            "<li>Эквалайзер и визуализация</li>"
            "<li>Тестовая запись</li>"
            "</ul>"
            "<br>"
            "<p><b>Для работы требуется:</b></p>"
            "<ul>"
            "<li>Whisper.cpp - движок распознавания (whisper-cli.exe)</li>"
            "<li>Модель распознавания (.bin файл)</li>"
            "<li>Микрофон для записи звука</li>"
            "</ul>"
            "<br>"
            "<p>Whisper и модели можно скачать автоматически: <b>Файл → Скачать Whisper и модели</b></p>"
            "<br>"
            "<p style='color: #666;'>Разработано с использованием Qt и Whisper.cpp</p>");
    });
    
    // Показываем информацию о выбранном микрофоне
    QString audioDevice = m_recognizer->getAudioDevice();
    if (!audioDevice.isEmpty()) {
        addLog("Используется микрофон: " + audioDevice, "INFO");
    }
    
    addLog("Программа запущена", "INFO");
}

MainWindow::~MainWindow()
{
    // Сохраняем настройки при закрытии
    saveSettings();
    delete ui;
}

void MainWindow::addLog(const QString &message, const QString &level)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString coloredMessage;
    
    if (level == "ERROR") {
        coloredMessage = QString("<span style='color: #d32f2f;'>[%1] ❌ %2</span>").arg(timestamp, message);
    } else if (level == "WARNING") {
        coloredMessage = QString("<span style='color: #f57c00;'>[%1] ⚠️ %2</span>").arg(timestamp, message);
    } else if (level == "SUCCESS") {
        coloredMessage = QString("<span style='color: #388e3c;'>[%1] ✅ %2</span>").arg(timestamp, message);
    } else {
        coloredMessage = QString("<span style='color: #666;'>[%1] ℹ️ %2</span>").arg(timestamp, message);
    }
    
    ui->logTextEdit->append(coloredMessage);
    
    // Прокручиваем вниз
    QTextCursor cursor = ui->logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui->logTextEdit->setTextCursor(cursor);
    
    // Также выводим в qDebug
    qDebug() << "[" << level << "]" << message;
}

void MainWindow::onClearLogsClicked()
{
    ui->logTextEdit->clear();
    addLog("Логи очищены", "INFO");
}

void MainWindow::onSettingsClicked()
{
    SettingsDialog dialog(this);
    auto modeToString = [](SpeechRecognizer::PerformanceMode mode) {
        switch (mode) {
            case SpeechRecognizer::FastMode:
                return QString("fast");
            case SpeechRecognizer::AccurateMode:
                return QString("accurate");
            default:
                return QString("balanced");
        }
    };

    // Устанавливаем текущие значения
    dialog.setWhisperPath(m_recognizer->getWhisperPath());
    dialog.setModelPath(m_recognizer->getModelPath());
    dialog.setLanguage(m_recognizer->getLanguage());
    dialog.setPerformanceMode(modeToString(m_recognizer->getPerformanceMode()));
    dialog.setHotkey(m_hotkeyName);
    dialog.setUseVad(m_useVad, QFile::exists(DownloadDialog::vadModelPath()));

    // Загружаем доступные аудио устройства
    QStringList audioDevices = m_recognizer->getAvailableAudioDevices();
    addLog("Найдено устройств записи: " + QString::number(audioDevices.size()), "INFO");
    dialog.setAvailableAudioDevices(audioDevices);
    dialog.setAudioDevice(m_recognizer->getAudioDevice());

    // Подключаем сигнал обновления устройств
    connect(&dialog, &SettingsDialog::refreshAudioDevices, this, &MainWindow::onRefreshAudioDevices);
    connect(this, &MainWindow::audioDevicesRefreshed, &dialog, &SettingsDialog::setAvailableAudioDevices);

    if (dialog.exec() == QDialog::Accepted) {
        // Сохраняем новые настройки
        m_recognizer->setWhisperPath(dialog.getWhisperPath());
        m_recognizer->setModelPath(dialog.getModelPath());
        m_recognizer->setLanguage(dialog.getLanguage());
        m_recognizer->setAudioDevice(dialog.getAudioDevice());
        if (dialog.getPerformanceMode() == "fast") {
            m_recognizer->setPerformanceMode(SpeechRecognizer::FastMode);
        } else if (dialog.getPerformanceMode() == "accurate") {
            m_recognizer->setPerformanceMode(SpeechRecognizer::AccurateMode);
        } else {
            m_recognizer->setPerformanceMode(SpeechRecognizer::BalancedMode);
        }
        m_hotkeyName = dialog.getHotkey();
        m_useVad = dialog.getUseVad();
        applyHotkey();

        // Сохраняем в настройки приложения
        saveSettings();

        // Проверяем наличие модели
        checkModelExists();

        addLog("Настройки сохранены", "SUCCESS");
        addLog("Микрофон: " + dialog.getAudioDevice(), "INFO");
        addLog("Язык: " + dialog.getLanguage(), "INFO");

        QMessageBox::information(this, "Настройки",
            "Настройки успешно сохранены!\n\n"
            "Микрофон: " + dialog.getAudioDevice());
    }
}

void MainWindow::onAudioConfigClicked()
{
    AudioConfigDialog dialog(this);
    
    // Загружаем доступные устройства
    QStringList audioDevices = m_recognizer->getAvailableAudioDevices();
    dialog.setAvailableAudioDevices(audioDevices);
    dialog.setAudioDevice(m_recognizer->getAudioDevice());
    
    // Подключаем сигнал сохранения настроек
    connect(&dialog, &AudioConfigDialog::settingsSaved,
            this, &MainWindow::applyAudioSettings);
    
    if (dialog.exec() == QDialog::Accepted) {
        addLog("Настройки микрофона сохранены", "SUCCESS");
    }
}

void MainWindow::onLoadFileClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Выберите аудио файл для распознавания",
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        "Аудио файлы (*.wav *.mp3 *.m4a *.flac *.ogg);;Все файлы (*.*)");
    
    if (fileName.isEmpty()) {
        return;
    }
    
    // Проверяем существование файла
    if (!QFile::exists(fileName)) {
        QMessageBox::warning(this, "Ошибка", "Файл не найден:\n" + fileName);
        addLog("Файл не найден: " + fileName, "ERROR");
        return;
    }
    
    // Проверяем размер файла
    QFileInfo fileInfo(fileName);
    qint64 fileSize = fileInfo.size();
    
    if (fileSize < 1000) {
        QMessageBox::warning(this, "Ошибка", "Файл слишком мал или поврежден");
        addLog("Файл слишком мал: " + fileName, "ERROR");
        return;
    }
    
    // Проверяем наличие Whisper и модели
    if (!checkComponentsBeforeRecording()) {
        return;
    }
    
    // Спрашиваем подтверждение
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Распознать файл?",
        "Распознать речь из файла:\n\n" + fileName + "\n\n"
        "Размер: " + QString::number(fileSize / 1024) + " КБ\n\n"
        "Продолжить?",
        QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        addLog("Начинаем распознавание файла: " + fileName, "INFO");
        addLog("Размер файла: " + QString::number(fileSize) + " байт", "INFO");
        
        ui->statusLabel->setText("⏳ Обработка файла...");
        ui->recordButton->setEnabled(false);
        ui->textEdit->append("\n--- " + QDateTime::currentDateTime().toString("hh:mm:ss") + " Распознавание файла ---");
        ui->textEdit->append("Файл: " + fileInfo.fileName());
        ui->textEdit->append("Путь: " + fileName);
        ui->textEdit->append("Размер: " + QString::number(fileSize / 1024) + " КБ\n");
        
        // Запускаем распознавание
        m_recognizer->recognizeFromFile(fileName);
    }
}

void MainWindow::applyAudioSettings()
{
    // Применяем настройки аудио к распознавателю
    QSettings settings("MyCompany", "SpeechRecognition");
    
    // Формат записи всегда 16 кГц моно — именно его ждёт whisper
    QString device = settings.value("AudioDevice", "").toString();
    if (!device.isEmpty()) {
        m_recognizer->setAudioDevice(device);
        addLog("Микрофон: " + m_recognizer->getAudioDevice(), "INFO");
    }
}

void MainWindow::loadSettings()
{
    QSettings settings("MyCompany", "SpeechRecognition");

    QString whisperPath = settings.value("WhisperPath", "").toString();
    QString modelPath = settings.value("ModelPath", "").toString();
    QString language = settings.value("Language", "ru").toString();
    QString audioDevice = settings.value("AudioDevice", "").toString();
    QString performanceMode = settings.value("PerformanceMode", "balanced").toString();
    ui->realtimeCheckBox->setChecked(settings.value("RealtimeMode", true).toBool());
    ui->dictationCheckBox->setChecked(settings.value("Dictation", false).toBool());
    ui->timestampsCheckBox->setChecked(settings.value("Timestamps", false).toBool());
    m_hotkeyName = settings.value("Hotkey", "Ctrl+Alt+D").toString();
    m_useVad = settings.value("UseVad", false).toBool();

    if (!whisperPath.isEmpty()) {
        m_recognizer->setWhisperPath(whisperPath);
        addLog("Загружен путь Whisper: " + whisperPath, "INFO");
    } else {
        addLog("Путь Whisper не задан, требуется настройка", "WARNING");
    }

    if (!modelPath.isEmpty()) {
        m_recognizer->setModelPath(modelPath);
        addLog("Загружен путь модели: " + modelPath, "INFO");
    } else {
        m_recognizer->setModelPath("whisper.cpp/models/ggml-base.bin");
        addLog("Используется путь модели по умолчанию", "INFO");
    }

    // Если сохранённые пути устарели, но компоненты уже скачаны встроенным загрузчиком — берём их
    if (!QFile::exists(m_recognizer->getWhisperPath())) {
        const QString installed = DownloadDialog::findInstalledWhisper();
        if (!installed.isEmpty()) {
            m_recognizer->setWhisperPath(installed);
            addLog("Найден установленный Whisper: " + installed, "SUCCESS");
        }
    }
    if (!QFile::exists(m_recognizer->getModelPath())) {
        const QString installed = DownloadDialog::findInstalledModel();
        if (!installed.isEmpty()) {
            m_recognizer->setModelPath(installed);
            addLog("Найдена скачанная модель: " + installed, "SUCCESS");
        }
    }

    m_recognizer->setLanguage(language);
    if (performanceMode == "fast") {
        m_recognizer->setPerformanceMode(SpeechRecognizer::FastMode);
    } else if (performanceMode == "accurate") {
        m_recognizer->setPerformanceMode(SpeechRecognizer::AccurateMode);
    } else {
        m_recognizer->setPerformanceMode(SpeechRecognizer::BalancedMode);
    }
    
    if (!audioDevice.isEmpty()) {
        m_recognizer->setAudioDevice(audioDevice);
    }
}

void MainWindow::saveSettings()
{
    QSettings settings("MyCompany", "SpeechRecognition");
    QString performanceMode = "balanced";
    if (m_recognizer->getPerformanceMode() == SpeechRecognizer::FastMode) {
        performanceMode = "fast";
    } else if (m_recognizer->getPerformanceMode() == SpeechRecognizer::AccurateMode) {
        performanceMode = "accurate";
    }

    settings.setValue("WhisperPath", m_recognizer->getWhisperPath());
    settings.setValue("ModelPath", m_recognizer->getModelPath());
    settings.setValue("Language", m_recognizer->getLanguage());
    settings.setValue("AudioDevice", m_recognizer->getAudioDevice());
    settings.setValue("PerformanceMode", performanceMode);
    settings.setValue("RealtimeMode", ui->realtimeCheckBox->isChecked());
    settings.setValue("Dictation", ui->dictationCheckBox->isChecked());
    settings.setValue("Timestamps", ui->timestampsCheckBox->isChecked());
    settings.setValue("Hotkey", m_hotkeyName);
    settings.setValue("UseVad", m_useVad);

    addLog("Настройки сохранены в реестр", "INFO");
}

void MainWindow::checkModelExists()
{
    QString modelPath = m_recognizer->getModelPath();
    QString whisperPath = m_recognizer->getWhisperPath();

    QFile modelFile(modelPath);
    QFile whisperFile(whisperPath);

    if (!modelFile.exists() || !whisperFile.exists()) {
        showModelNotFoundWarning();
    } else {
        addLog("Компоненты найдены: Whisper и модель", "SUCCESS");
    }
}

bool MainWindow::checkComponentsBeforeRecording()
{
    QString whisperPath = m_recognizer->getWhisperPath();
    QString modelPath = m_recognizer->getModelPath();

    QFile whisperFile(whisperPath);
    QFile modelFile(modelPath);

    if (whisperPath.isEmpty() || !whisperFile.exists()) {
        addLog("Whisper не найден: " + whisperPath, "ERROR");
        
        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowTitle("Whisper не найден");
        msgBox.setText("❌ Не найден исполняемый файл Whisper!");
        msgBox.setInformativeText(
            "Для работы программы необходимо:\n\n"
            "1. Скачать и установить Whisper.cpp\n"
            "2. Указать путь к whisper-cli.exe (или main.exe) в настройках\n\n"
            "Хотите открыть настройки сейчас?"
        );

        QPushButton *settingsButton = msgBox.addButton("Открыть настройки", QMessageBox::ActionRole);
        msgBox.addButton("Отмена", QMessageBox::RejectRole);

        msgBox.setDefaultButton(settingsButton);
        msgBox.exec();

        if (msgBox.clickedButton() == settingsButton) {
            onSettingsClicked();
        }

        return false;
    }

    if (modelPath.isEmpty() || !modelFile.exists()) {
        addLog("Модель не найдена: " + modelPath, "ERROR");
        
        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowTitle("Модель не найдена");
        msgBox.setText("❌ Не найден файл модели Whisper!");
        msgBox.setInformativeText(
            "Для работы программы необходимо:\n\n"
            "1. Скачать модель распознавания (.bin файл)\n"
            "2. Указать путь к файлу модели в настройках\n\n"
            "Хотите открыть настройки сейчас?"
        );

        QPushButton *settingsButton = msgBox.addButton("Открыть настройки", QMessageBox::ActionRole);
        msgBox.addButton("Отмена", QMessageBox::RejectRole);

        msgBox.setDefaultButton(settingsButton);
        msgBox.exec();

        if (msgBox.clickedButton() == settingsButton) {
            onSettingsClicked();
        }

        return false;
    }
    
    QStringList audioDevices = m_recognizer->getAvailableAudioDevices();
    if (audioDevices.isEmpty()) {
        addLog("Не найдено аудио устройств!", "ERROR");
        
        QMessageBox::critical(this, "Микрофон не найден",
            "❌ Не найдено ни одного устройства записи звука!\n\n"
            "Пожалуйста:\n"
            "1. Подключите микрофон\n"
            "2. Проверьте настройки системы\n"
            "3. Перезапустите программу");
        return false;
    }

#ifndef Q_OS_WIN
    if (!whisperFile.permissions().testFlag(QFile::ExeUser)) {
        addLog("У файла Whisper нет прав на выполнение", "ERROR");
        
        QMessageBox::warning(this, "Ошибка прав доступа",
            "Файл Whisper не имеет прав на выполнение.\n\n"
            "Выполните в терминале:\n"
            "chmod +x " + whisperPath);
        return false;
    }
#endif

    addLog("Все компоненты проверены успешно", "SUCCESS");
    return true;
}

void MainWindow::showModelNotFoundWarning()
{
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle("Требуется настройка");

    QString missingComponents;
    if (!QFile::exists(m_recognizer->getWhisperPath())) {
        missingComponents += "❌ Whisper не найден\n";
        addLog("Whisper не найден", "WARNING");
    }
    if (!QFile::exists(m_recognizer->getModelPath())) {
        missingComponents += "❌ Модель не найдена\n";
        addLog("Модель не найдена", "WARNING");
    }

    msgBox.setText("Не найдены необходимые компоненты:\n\n" + missingComponents);
    msgBox.setInformativeText(
        "\nОткройте настройки и:\n"
        "1. Скачайте необходимые компоненты по ссылкам\n"
        "2. Укажите пути к установленным файлам\n"
        "3. Настройте микрофон\n\n"
        "Без этих компонентов программа не сможет работать."
    );

    QPushButton *downloadButton = msgBox.addButton("⬇ Скачать автоматически", QMessageBox::ActionRole);
    QPushButton *settingsButton = msgBox.addButton("Открыть настройки", QMessageBox::ActionRole);
    QPushButton *audioButton = msgBox.addButton("Настроить микрофон", QMessageBox::ActionRole);
    msgBox.addButton("Понятно", QMessageBox::RejectRole);

    msgBox.setDefaultButton(downloadButton);
    msgBox.exec();

    if (msgBox.clickedButton() == downloadButton) {
        onDownloadClicked();
    } else if (msgBox.clickedButton() == settingsButton) {
        onSettingsClicked();
    } else if (msgBox.clickedButton() == audioButton) {
        onAudioConfigClicked();
    }
}

void MainWindow::onRecordButtonClicked()
{
    if (!checkComponentsBeforeRecording()) {
        return;
    }

    if (!m_isRecording) {
        startRealtime();
    } else {
        addLog("Останавливаем запись...", "INFO");
        m_realtime->stop();
        m_isRecording = false;
    }
}

QString MainWindow::recordingsDirectory() const
{
    QString recordingsDir = QCoreApplication::applicationDirPath() + "/recordings";
    if (!QDir().mkpath(recordingsDir)) {
        recordingsDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return recordingsDir;
}

QString MainWindow::vadModelIfEnabled() const
{
    const QString path = DownloadDialog::vadModelPath();
    return (m_useVad && QFile::exists(path)) ? path : QString();
}

void MainWindow::startRealtime()
{
    const bool live = ui->realtimeCheckBox->isChecked();
    addLog(live ? "Запуск распознавания в реальном времени..." : "Начинаем запись...", "INFO");

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_realtime->setLiveTranscription(live);
    m_realtime->setWhisperPath(m_recognizer->getWhisperPath());
    m_realtime->setModelPath(m_recognizer->getModelPath());
    m_realtime->setLanguage(m_recognizer->getLanguage());
    m_realtime->setAudioDevice(m_recognizer->getAudioDevice());
    // В реальном времени распознавание — основная нагрузка, отдаём ему все ядра
    m_realtime->setThreads(qBound(1, QThread::idealThreadCount(), 8));
    m_realtime->setCompactFinals(m_recognizer->getPerformanceMode() == SpeechRecognizer::FastMode);
    m_realtime->setBeamSize(m_recognizer->beamSize());
    m_realtime->setVadModel(vadModelIfEnabled());
    m_realtime->setRecordingFile(recordingsDirectory() + "/recording_" + timestamp + ".wav");
    if (live) {
        addLog("Модель: " + QFileInfo(m_recognizer->getModelPath()).fileName()
               + (vadModelIfEnabled().isEmpty() ? QString() : ", фильтр шума включён"), "INFO");
    }

    m_dictationStarted = false;
    m_realtime->start();
    m_isRecording = m_realtime->isRunning();
}

void MainWindow::onRealtimeFinalText(const QString &text)
{
    QTextCursor cursor(ui->textEdit->document());
    cursor.movePosition(QTextCursor::End);
    if (ui->timestampsCheckBox->isChecked()) {
        if (!cursor.block().text().isEmpty()) {
            cursor.insertBlock();
        }
        cursor.insertText("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] ");
    } else if (!cursor.block().text().isEmpty()) {
        cursor.insertText(" ");
    }
    cursor.insertText(text);
    ui->textEdit->verticalScrollBar()->setValue(ui->textEdit->verticalScrollBar()->maximum());

    deliverDictation(text);
}

void MainWindow::deliverDictation(const QString &text)
{
    if (!ui->dictationCheckBox->isChecked() || text.isEmpty()) {
        return;
    }
    const QString chunk = m_dictationStarted ? " " + text : text;
    if (Dictation::typeIntoForegroundWindow(chunk)) {
        m_dictationStarted = true;
    }
}

void MainWindow::applyHotkey()
{
    if (m_hotkeyName.isEmpty()) {
        m_hotkey->setShortcut(QString(), 0);
        return;
    }
    if (m_hotkey->setShortcut(m_hotkeyName, static_cast<quintptr>(winId()))) {
        addLog("Горячая клавиша записи: " + m_hotkeyName, "INFO");
    } else {
        addLog("Не удалось назначить горячую клавишу " + m_hotkeyName
               + " — возможно, она занята другой программой. Выберите другую в настройках.", "WARNING");
    }
}

void MainWindow::onHotkeyActivated()
{
    if (!m_isRecording && !ui->recordButton->isEnabled()) {
        return; // идёт распознавание предыдущей записи
    }
    Dictation::playSignal(!m_isRecording);
    onRecordButtonClicked();
}

void MainWindow::onNoSignal()
{
    const QString message = "Микрофон не передаёт звук: уже 4 секунды приходит полная тишина.";
    addLog(message, "WARNING");
    ui->statusLabel->setText("⚠️ Нет сигнала от микрофона — проверьте устройство в настройках");

    auto *box = new QMessageBox(QMessageBox::Warning, "Нет сигнала от микрофона",
        message + "\n\nВозможные причины:\n"
        "• выбран не тот микрофон (Файл → Настройки → Микрофон)\n"
        "• микрофон выключен или его громкость на нуле в настройках звука Windows\n"
        "• программе запрещён доступ к микрофону (Параметры → Конфиденциальность → Микрофон)\n"
        "• микрофон не подключён или занят другой программой",
        QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
}

void MainWindow::onCopyTextClicked()
{
    QGuiApplication::clipboard()->setText(ui->textEdit->toPlainText());
    addLog("Текст скопирован в буфер обмена", "SUCCESS");
}

void MainWindow::onSaveTextClicked()
{
    const QString defaultName = recordingsDirectory() + "/text_"
        + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".txt";
    const QString fileName = QFileDialog::getSaveFileName(this, "Сохранить текст", defaultName,
                                                          "Текстовые файлы (*.txt);;Все файлы (*.*)");
    if (fileName.isEmpty()) {
        return;
    }
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Ошибка", "Не удалось сохранить файл:\n" + fileName);
        return;
    }
    // BOM — чтобы Блокнот в старых версиях Windows открыл кириллицу правильно
    file.write("\xEF\xBB\xBF");
    file.write(ui->textEdit->toPlainText().toUtf8());
    addLog("Текст сохранён: " + fileName, "SUCCESS");
}

void MainWindow::onDownloadClicked()
{
    if (m_isRecording) {
        QMessageBox::information(this, "Загрузка", "Сначала остановите запись.");
        return;
    }

    // whisper-server держит файлы программы открытыми — останавливаем перед обновлением
    m_realtime->shutdownServer();

    DownloadDialog dialog(m_recognizer->getWhisperPath(), m_recognizer->getModelPath(), this);
    dialog.exec();

    bool changed = false;
    if (QFile::exists(dialog.whisperPath()) && dialog.whisperPath() != m_recognizer->getWhisperPath()) {
        m_recognizer->setWhisperPath(dialog.whisperPath());
        addLog("Whisper: " + dialog.whisperPath(), "SUCCESS");
        changed = true;
    }
    if (QFile::exists(dialog.modelPath()) && dialog.modelPath() != m_recognizer->getModelPath()) {
        m_recognizer->setModelPath(dialog.modelPath());
        addLog("Модель: " + dialog.modelPath(), "SUCCESS");
        changed = true;
    }
    if (changed) {
        saveSettings();
        if (QFile::exists(m_recognizer->getWhisperPath()) && QFile::exists(m_recognizer->getModelPath())) {
            addLog("Компоненты найдены: Whisper и модель", "SUCCESS");
            ui->statusLabel->setText("Готов к записи");
        }
    }
}

void MainWindow::onRealtimeFinished()
{
    if (!m_realtime->isLive()) {
        // Обычный режим: распознаём всю запись целиком
        m_recognizer->recognizeFromFile(m_realtime->recordingFile());
        return;
    }
    ui->partialLabel->clear();
    ui->statusLabel->setText("✓ Готово");
    ui->recordButton->setEnabled(true);
    ui->realtimeCheckBox->setEnabled(true);
    ui->timerLabel->setVisible(false);
    ui->textEdit->append("");
    addLog("Распознавание в реальном времени завершено", "SUCCESS");
}

void MainWindow::onRecordingStarted()
{
    const bool realtime = m_realtime->isLive();
    ui->recordButton->setText("⏹ Остановить запись");
    ui->realtimeCheckBox->setEnabled(false);
    ui->statusLabel->setText(realtime ? "🔴 Идет запись — говорите, текст появляется после каждой паузы"
                                      : "🔴 Идет запись...");
    ui->textEdit->append("\n--- " + QDateTime::currentDateTime().toString("hh:mm:ss") + " Запись началась ---");
    const QString deviceName = m_recognizer->getAudioDevice().isEmpty()
        ? "по умолчанию (" + QAudioDeviceInfo::defaultInputDevice().deviceName() + ")"
        : m_recognizer->getAudioDevice();
    ui->textEdit->append("Микрофон: " + deviceName + "\n");
    
    ui->timerLabel->setVisible(true);
    ui->timerLabel->setText("⏱️ 00:00");
    m_elapsedTimer.start();
    m_recordingTimer->start(100);
    
    addLog("Запись началась успешно", "SUCCESS");
    addLog("Устройство: " + deviceName, "INFO");
    
    QString appDir = QCoreApplication::applicationDirPath();
    QString recordingsDir = appDir + "/recordings";
    addLog("Файл сохраняется в: " + recordingsDir, "INFO");
}

void MainWindow::onRecordingStopped()
{
    ui->recordButton->setText("🎤 Начать запись");
    ui->recordButton->setEnabled(false);
    if (m_realtime->isLive()) {
        ui->statusLabel->setText("⏳ Распознавание последней фразы...");
    } else {
        ui->statusLabel->setText("⏳ Обработка аудио...");
        ui->textEdit->append("--- Обработка... ---\n");
    }

    m_recordingTimer->stop();
    qint64 elapsed = m_elapsedTimer.elapsed();
    int seconds = (elapsed / 1000) % 60;
    int minutes = (elapsed / 1000) / 60;
    
    addLog("Запись остановлена, начинается распознавание", "INFO");
    addLog(QString("Длительность записи: %1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0')), "INFO");
}

void MainWindow::onRecognitionComplete(const QString &text)
{
    ui->textEdit->append("✅ Результат: " + text + "\n");
    ui->statusLabel->setText("✓ Готово");
    ui->recordButton->setEnabled(true);
    ui->realtimeCheckBox->setEnabled(true);

    ui->timerLabel->setVisible(false);

    deliverDictation(text);
    addLog("Распознавание завершено успешно", "SUCCESS");
    addLog("Результат: " + text.left(50) + (text.length() > 50 ? "..." : ""), "INFO");
}

void MainWindow::onError(const QString &error)
{
    ui->textEdit->append("❌ Ошибка: " + error + "\n");
    ui->statusLabel->setText("❌ Ошибка");
    ui->recordButton->setEnabled(true);
    ui->recordButton->setText("🎤 Начать запись");
    ui->realtimeCheckBox->setEnabled(true);
    ui->partialLabel->clear();
    m_isRecording = false;

    m_recordingTimer->stop();
    ui->timerLabel->setVisible(false);
    
    addLog("ОШИБКА: " + error, "ERROR");
    
    if (error.contains("не был создан") || error.contains("не записывает")) {
        addLog("Проблема с микрофоном или правами доступа", "ERROR");
        
        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowTitle("Проблема с микрофоном");
        msgBox.setText("Возможные причины:\n\n"
            "1. Микрофон не подключен или отключен в системе\n"
            "2. Программе не предоставлен доступ к микрофону\n"
            "3. Микрофон используется другой программой\n\n"
            "Решение:\n"
            "• Проверьте настройки звука в системе\n"
            "• Предоставьте программе доступ к микрофону\n"
            "• Откройте настройки микрофона для тестирования");
        
        QPushButton *audioButton = msgBox.addButton("Настроить микрофон", QMessageBox::ActionRole);
        msgBox.addButton("OK", QMessageBox::AcceptRole);
        msgBox.exec();
        
        if (msgBox.clickedButton() == audioButton) {
            onAudioConfigClicked();
        }
    } else if (error.contains("Не удалось открыть аудио файл")) {
        addLog("Проблема с форматом или кодировкой файла", "ERROR");
        
        QMessageBox::critical(this, "Ошибка файла",
            "Не удалось открыть аудио файл.\n\n"
            "Возможные причины:\n"
            "1. Неподдерживаемый формат файла\n"
            "2. Файл поврежден\n"
            "3. Неправильная кодировка\n\n"
            "Попробуйте:\n"
            "• Конвертировать файл в WAV формат\n"
            "• Проверить целостность файла\n"
            "• Использовать другой файл");
    }
}

void MainWindow::updateRecordingTimer()
{
    qint64 elapsed = m_elapsedTimer.elapsed();
    int seconds = (elapsed / 1000) % 60;
    int minutes = (elapsed / 1000) / 60;
    
    ui->timerLabel->setText(QString("⏱️ %1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0')));
}

void MainWindow::onRefreshAudioDevices()
{
    addLog("Обновление списка аудио устройств...", "INFO");
    QStringList devices = m_recognizer->getAvailableAudioDevices();
    addLog("Найдено устройств: " + QString::number(devices.size()), "SUCCESS");
    emit audioDevicesRefreshed(devices);
}

void MainWindow::onOpenRecordingsClicked()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString recordingsDir = appDir + "/recordings";
    
    QDir dir;
    if (!dir.exists(recordingsDir)) {
        if (!dir.mkpath(recordingsDir)) {
            QMessageBox::warning(this, "Ошибка",
                "Не удалось создать папку recordings");
            addLog("Не удалось создать папку recordings", "ERROR");
            return;
        }
    }
    
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", QStringList() << QDir::toNativeSeparators(recordingsDir));
#elif defined(Q_OS_MAC)
    QProcess::startDetached("open", QStringList() << recordingsDir);
#else
    QProcess::startDetached("xdg-open", QStringList() << recordingsDir);
#endif
    
    addLog("Открыта папка: " + recordingsDir, "INFO");
}
