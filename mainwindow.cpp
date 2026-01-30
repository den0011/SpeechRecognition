#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "settingsdialog.h"

#include <QDebug>
#include <QDateTime>
#include <QSettings>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcess>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_isRecording(false)
{
    ui->setupUi(this);

    // Создаем распознаватель речи
    m_recognizer = new SpeechRecognizer(this);

    // Инициализируем таймер записи
    m_recordingTimer = new QTimer(this);
    connect(m_recordingTimer, &QTimer::timeout, this, &MainWindow::updateRecordingTimer);

    // Загружаем настройки
    loadSettings();

    // Проверяем наличие модели
    checkModelExists();

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
    connect(m_recognizer, &SpeechRecognizer::recordingStarted,
            this, &MainWindow::onRecordingStarted);
    connect(m_recognizer, &SpeechRecognizer::recordingStopped,
            this, &MainWindow::onRecordingStopped);

    // Подключаем пункты меню
    connect(ui->actionSettings, &QAction::triggered,
            this, &MainWindow::onSettingsClicked);
    connect(ui->actionOpenRecordings, &QAction::triggered,
            this, &MainWindow::onOpenRecordingsClicked);
    connect(ui->actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "О программе",
            "<h3>Голос в текст - Whisper</h3>"
            "<p>Версия 1.2</p>"
            "<p>Программа для распознавания речи с использованием Whisper.cpp</p>"
            "<br>"
            "<p><b>Для работы требуется:</b></p>"
            "<ul>"
            "<li>Whisper.cpp - движок распознавания (whisper-cli.exe)</li>"
            "<li>Модель распознавания (.bin файл)</li>"
            "<li>Микрофон для записи звука</li>"
            "</ul>"
            "<br>"
            "<p>Ссылки для скачивания доступны в меню <b>Файл → Настройки</b></p>"
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

    // Устанавливаем текущие значения
    dialog.setWhisperPath(m_recognizer->getWhisperPath());
    dialog.setModelPath(m_recognizer->getModelPath());
    dialog.setLanguage(m_recognizer->getLanguage());
    
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

void MainWindow::loadSettings()
{
    QSettings settings("MyCompany", "SpeechRecognition");

    QString whisperPath = settings.value("WhisperPath", "").toString();
    QString modelPath = settings.value("ModelPath", "").toString();
    QString language = settings.value("Language", "ru").toString();
    QString audioDevice = settings.value("AudioDevice", "").toString();

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
        m_recognizer->setModelPath("./whisper.cpp/models/ggml-base.bin");
        addLog("Используется путь модели по умолчанию", "INFO");
    }

    m_recognizer->setLanguage(language);
    
    if (!audioDevice.isEmpty()) {
        m_recognizer->setAudioDevice(audioDevice);
    }
}

void MainWindow::saveSettings()
{
    QSettings settings("MyCompany", "SpeechRecognition");

    settings.setValue("WhisperPath", m_recognizer->getWhisperPath());
    settings.setValue("ModelPath", m_recognizer->getModelPath());
    settings.setValue("Language", m_recognizer->getLanguage());
    settings.setValue("AudioDevice", m_recognizer->getAudioDevice());
    
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
        QPushButton *cancelButton = msgBox.addButton("Отмена", QMessageBox::RejectRole);

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
        QPushButton *cancelButton = msgBox.addButton("Отмена", QMessageBox::RejectRole);

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
        "3. Выберите микрофон для записи\n\n"
        "Без этих компонентов программа не сможет работать."
    );

    QPushButton *settingsButton = msgBox.addButton("Открыть настройки", QMessageBox::ActionRole);
    QPushButton *ignoreButton = msgBox.addButton("Понятно", QMessageBox::RejectRole);

    msgBox.setDefaultButton(settingsButton);
    msgBox.exec();

    if (msgBox.clickedButton() == settingsButton) {
        onSettingsClicked();
    }
}

void MainWindow::onRecordButtonClicked()
{
    if (!checkComponentsBeforeRecording()) {
        return;
    }

    if (!m_isRecording) {
        addLog("Начинаем запись...", "INFO");
        m_recognizer->startRecording();
        m_isRecording = true;
    } else {
        addLog("Останавливаем запись...", "INFO");
        m_recognizer->stopRecording();
        m_isRecording = false;
    }
}

void MainWindow::onRecordingStarted()
{
    ui->recordButton->setText("⏹ Остановить запись");
    ui->statusLabel->setText("🔴 Идет запись...");
    ui->textEdit->append("\n--- " + QDateTime::currentDateTime().toString("hh:mm:ss") + " Запись началась ---");
    ui->textEdit->append("Микрофон: " + m_recognizer->getAudioDevice() + "\n");
    
    // Запускаем таймер
    ui->timerLabel->setVisible(true);
    ui->timerLabel->setText("⏱️ 00:00");
    m_elapsedTimer.start();
    m_recordingTimer->start(100);
    
    addLog("Запись началась успешно", "SUCCESS");
    addLog("Устройство: " + m_recognizer->getAudioDevice(), "INFO");
    
    QString appDir = QCoreApplication::applicationDirPath();
    QString recordingsDir = appDir + "/recordings";
    addLog("Файл сохраняется в: " + recordingsDir, "INFO");
}

void MainWindow::onRecordingStopped()
{
    ui->recordButton->setText("🎤 Начать запись");
    ui->recordButton->setEnabled(false);
    ui->statusLabel->setText("⏳ Обработка аудио...");
    ui->textEdit->append("--- Обработка... ---\n");
    
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
    
    ui->timerLabel->setVisible(false);
    
    addLog("Распознавание завершено успешно", "SUCCESS");
    addLog("Результат: " + text.left(50) + (text.length() > 50 ? "..." : ""), "INFO");
}

void MainWindow::onError(const QString &error)
{
    ui->textEdit->append("❌ Ошибка: " + error + "\n");
    ui->statusLabel->setText("❌ Ошибка");
    ui->recordButton->setEnabled(true);
    ui->recordButton->setText("🎤 Начать запись");
    m_isRecording = false;
    
    m_recordingTimer->stop();
    ui->timerLabel->setVisible(false);
    
    addLog("ОШИБКА: " + error, "ERROR");
    
    if (error.contains("не был создан") || error.contains("не записывает")) {
        addLog("Проблема с микрофоном или правами доступа", "ERROR");
        
        QMessageBox::critical(this, "Проблема с микрофоном",
            "Возможные причины:\n\n"
            "1. Микрофон не подключен или отключен в системе\n"
            "2. Программе не предоставлен доступ к микрофону\n"
            "3. Микрофон используется другой программой\n\n"
            "Решение:\n"
            "• Проверьте настройки звука в системе\n"
            "• Предоставьте программе доступ к микрофону\n"
            "• Выберите другой микрофон в настройках программы");
    } else if (error.contains("deprecated") || error.contains("whisper-cli")) {
        addLog("Используется устаревший исполняемый файл", "WARNING");
        
        QMessageBox::warning(this, "Используется устаревший файл",
            "Вы используете устаревший файл 'main.exe'.\n\n"
            "Рекомендуется использовать 'whisper-cli.exe' вместо него.\n\n"
            "Откройте настройки и укажите путь к whisper-cli.exe");
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
