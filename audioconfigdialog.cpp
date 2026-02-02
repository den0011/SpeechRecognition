#include "audioconfigdialog.h"
#include "ui_audioconfigdialog.h"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QSettings>
#include <QTimer>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>

AudioConfigDialog::AudioConfigDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AudioConfigDialog),
    m_audioRecorder(new QAudioRecorder(this)),
    m_audioProbe(new QAudioProbe(this)),
    m_mediaPlayer(new QMediaPlayer(this)),
    m_levelTimer(new QTimer(this)),
    m_currentLevel(0.0),
    m_isTestRecording(false)
{
    ui->setupUi(this);
    
    setWindowTitle("Настройка микрофона");
    setMinimumSize(700, 700);
    
    // Загружаем сохраненные настройки
    loadSettings();
    
    // Настраиваем аудио рекордер
    setupAudioRecorder();
    
    // Подключаем сигналы кнопок
    connect(ui->testRecordButton, &QPushButton::clicked, 
            this, &AudioConfigDialog::onTestRecordClicked);
    connect(ui->testPlayButton, &QPushButton::clicked, 
            this, &AudioConfigDialog::onTestPlayClicked);
    connect(ui->stopTestButton, &QPushButton::clicked, 
            this, &AudioConfigDialog::onStopTestClicked);
    connect(ui->saveButton, &QPushButton::clicked, 
            this, &AudioConfigDialog::onSaveClicked);
    connect(ui->cancelButton, &QPushButton::clicked, 
            this, &AudioConfigDialog::onCancelClicked);
    connect(ui->refreshButton, &QPushButton::clicked, 
            this, &AudioConfigDialog::onRefreshDevicesClicked);
    
    // Подключаем кнопки пресетов
    connect(ui->savePresetButton, &QPushButton::clicked,
            this, &AudioConfigDialog::onSavePresetClicked);
    connect(ui->loadPresetButton, &QPushButton::clicked,
            this, &AudioConfigDialog::onLoadPresetClicked);
    connect(ui->deletePresetButton, &QPushButton::clicked,
            this, &AudioConfigDialog::onDeletePresetClicked);
    connect(ui->presetComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioConfigDialog::onPresetSelected);
    
    // Подключаем слайдеры эквалайзера
    connect(ui->eq60Slider, &QSlider::valueChanged, this, [this](int value) {
        ui->eq60ValueLabel->setText(QString("%1 dB").arg(value));
    });
    connect(ui->eq250Slider, &QSlider::valueChanged, this, [this](int value) {
        ui->eq250ValueLabel->setText(QString("%1 dB").arg(value));
    });
    connect(ui->eq1kSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->eq1kValueLabel->setText(QString("%1 dB").arg(value));
    });
    connect(ui->eq4kSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->eq4kValueLabel->setText(QString("%1 dB").arg(value));
    });
    connect(ui->eq16kSlider, &QSlider::valueChanged, this, [this](int value) {
        ui->eq16kValueLabel->setText(QString("%1 dB").arg(value));
    });
    
    // Таймер для обновления уровня звука
    connect(m_levelTimer, &QTimer::timeout, 
            this, &AudioConfigDialog::updateAudioLevel);
    
    // Подключаем probe для мониторинга аудио
    m_audioProbe->setSource(m_audioRecorder);
    connect(m_audioProbe, &QAudioProbe::audioBufferProbed,
            this, [this](const QAudioBuffer &buffer) {
        updateVisualization(buffer);
    });
    
    // Мониторинг состояния записи
    connect(m_audioRecorder, &QAudioRecorder::stateChanged,
            this, &AudioConfigDialog::onRecordingStateChanged);
    
    // Мониторинг плеера
    connect(m_mediaPlayer, &QMediaPlayer::stateChanged,
            this, &AudioConfigDialog::onPlayerStateChanged);
    
    // Загружаем список пресетов
    loadPresets();
}

AudioConfigDialog::~AudioConfigDialog()
{
    if (m_audioRecorder->state() == QMediaRecorder::RecordingState) {
        m_audioRecorder->stop();
    }
    if (m_mediaPlayer->state() == QMediaPlayer::PlayingState) {
        m_mediaPlayer->stop();
    }
    delete ui;
}

void AudioConfigDialog::setupAudioRecorder()
{
    // Настройки по умолчанию
    QAudioEncoderSettings settings;
    settings.setCodec("audio/pcm");
    settings.setSampleRate(16000);
    settings.setChannelCount(1);
    settings.setBitRate(256000);
    settings.setQuality(QMultimedia::HighQuality);
    
    m_audioRecorder->setEncodingSettings(settings);
    m_audioRecorder->setContainerFormat("audio/x-wav");
    
    // Устанавливаем устройство по умолчанию
    QAudioDeviceInfo defaultDevice = QAudioDeviceInfo::defaultInputDevice();
    if (!defaultDevice.isNull()) {
        m_audioRecorder->setAudioInput(defaultDevice.deviceName());
    }
}

void AudioConfigDialog::loadSettings()
{
    QSettings settings("MyCompany", "SpeechRecognition");
    
    // Загружаем параметры
    int sampleRate = settings.value("AudioConfig/SampleRate", 16000).toInt();
    int bitRate = settings.value("AudioConfig/BitRate", 256000).toInt();
    int channels = settings.value("AudioConfig/Channels", 1).toInt();
    
    // Устанавливаем в UI
    if (sampleRate == 8000) ui->sampleRateComboBox->setCurrentIndex(0);
    else if (sampleRate == 16000) ui->sampleRateComboBox->setCurrentIndex(1);
    else if (sampleRate == 22050) ui->sampleRateComboBox->setCurrentIndex(2);
    else if (sampleRate == 44100) ui->sampleRateComboBox->setCurrentIndex(3);
    else if (sampleRate == 48000) ui->sampleRateComboBox->setCurrentIndex(4);
    
    if (bitRate == 128000) ui->bitRateComboBox->setCurrentIndex(0);
    else if (bitRate == 192000) ui->bitRateComboBox->setCurrentIndex(1);
    else if (bitRate == 256000) ui->bitRateComboBox->setCurrentIndex(2);
    else if (bitRate == 320000) ui->bitRateComboBox->setCurrentIndex(3);
    
    ui->channelsComboBox->setCurrentIndex(channels - 1);
    
    // Загружаем настройки эквалайзера
    ui->eq60Slider->setValue(settings.value("AudioConfig/EQ60", 0).toInt());
    ui->eq250Slider->setValue(settings.value("AudioConfig/EQ250", 0).toInt());
    ui->eq1kSlider->setValue(settings.value("AudioConfig/EQ1k", 0).toInt());
    ui->eq4kSlider->setValue(settings.value("AudioConfig/EQ4k", 0).toInt());
    ui->eq16kSlider->setValue(settings.value("AudioConfig/EQ16k", 0).toInt());
}

int AudioConfigDialog::getSampleRate() const
{
    QString text = ui->sampleRateComboBox->currentText();
    if (text.contains("8000")) return 8000;
    if (text.contains("16000")) return 16000;
    if (text.contains("22050")) return 22050;
    if (text.contains("44100")) return 44100;
    if (text.contains("48000")) return 48000;
    return 16000;
}

int AudioConfigDialog::getBitRate() const
{
    QString text = ui->bitRateComboBox->currentText();
    if (text.contains("128")) return 128000;
    if (text.contains("192")) return 192000;
    if (text.contains("256")) return 256000;
    if (text.contains("320")) return 320000;
    return 256000;
}

int AudioConfigDialog::getChannelCount() const
{
    return ui->channelsComboBox->currentIndex() + 1;
}

QString AudioConfigDialog::getAudioDevice() const
{
    return ui->deviceComboBox->currentText();
}

void AudioConfigDialog::setSampleRate(int rate)
{
    if (rate == 8000) ui->sampleRateComboBox->setCurrentIndex(0);
    else if (rate == 16000) ui->sampleRateComboBox->setCurrentIndex(1);
    else if (rate == 22050) ui->sampleRateComboBox->setCurrentIndex(2);
    else if (rate == 44100) ui->sampleRateComboBox->setCurrentIndex(3);
    else if (rate == 48000) ui->sampleRateComboBox->setCurrentIndex(4);
}

void AudioConfigDialog::setBitRate(int rate)
{
    if (rate == 128000) ui->bitRateComboBox->setCurrentIndex(0);
    else if (rate == 192000) ui->bitRateComboBox->setCurrentIndex(1);
    else if (rate == 256000) ui->bitRateComboBox->setCurrentIndex(2);
    else if (rate == 320000) ui->bitRateComboBox->setCurrentIndex(3);
}

void AudioConfigDialog::setChannelCount(int channels)
{
    ui->channelsComboBox->setCurrentIndex(channels - 1);
}

void AudioConfigDialog::setAudioDevice(const QString &device)
{
    int index = ui->deviceComboBox->findText(device);
    if (index != -1) {
        ui->deviceComboBox->setCurrentIndex(index);
    }
}

void AudioConfigDialog::setAvailableAudioDevices(const QStringList &devices)
{
    ui->deviceComboBox->clear();
    
    if (devices.isEmpty()) {
        ui->deviceComboBox->addItem("Устройства не найдены");
        ui->deviceComboBox->setEnabled(false);
        ui->testRecordButton->setEnabled(false);
    } else {
        ui->deviceComboBox->addItems(devices);
        ui->deviceComboBox->setEnabled(true);
        ui->testRecordButton->setEnabled(true);
    }
}

void AudioConfigDialog::onTestRecordClicked()
{
    if (m_isTestRecording) {
        return;
    }
    
    // Применяем текущие настройки
    QAudioEncoderSettings settings;
    settings.setCodec("audio/pcm");
    settings.setSampleRate(getSampleRate());
    settings.setChannelCount(getChannelCount());
    settings.setBitRate(getBitRate());
    settings.setQuality(QMultimedia::HighQuality);
    
    m_audioRecorder->setEncodingSettings(settings);
    m_audioRecorder->setContainerFormat("audio/x-wav");
    
    // Устанавливаем выбранное устройство
    QString device = ui->deviceComboBox->currentText();
    if (!device.isEmpty() && device != "Устройства не найдены") {
        m_audioRecorder->setAudioInput(device);
    }
    
    // Создаем временный файл для теста
    QString appDir = QCoreApplication::applicationDirPath();
    QString testDir = appDir + "/recordings";
    QDir dir;
    if (!dir.exists(testDir)) {
        dir.mkpath(testDir);
    }
    
    m_testFilePath = testDir + "/test_" + 
                     QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".wav";
    
    m_audioRecorder->setOutputLocation(QUrl::fromLocalFile(m_testFilePath));
    m_audioRecorder->record();
    
    m_isTestRecording = true;
    ui->testRecordButton->setEnabled(false);
    ui->stopTestButton->setEnabled(true);
    ui->testStatusLabel->setText("🔴 Идет запись (10 секунд)...");
    
    // Запускаем визуализацию
    startLevelMonitoring();
    
    // Автоматическая остановка через 10 секунд
    QTimer::singleShot(10000, this, [this]() {
        if (m_isTestRecording) {
            onStopTestClicked();
        }
    });
}

void AudioConfigDialog::onTestPlayClicked()
{
    if (m_testFilePath.isEmpty() || !QFile::exists(m_testFilePath)) {
        QMessageBox::warning(this, "Ошибка", "Сначала сделайте тестовую запись");
        return;
    }
    
    m_mediaPlayer->setMedia(QUrl::fromLocalFile(m_testFilePath));
    m_mediaPlayer->setVolume(100);
    m_mediaPlayer->play();
    
    ui->testPlayButton->setEnabled(false);
    ui->stopTestButton->setEnabled(true);
    ui->testStatusLabel->setText("▶ Воспроизведение...");
}

void AudioConfigDialog::onStopTestClicked()
{
    if (m_audioRecorder->state() == QMediaRecorder::RecordingState) {
        m_audioRecorder->stop();
        stopLevelMonitoring();
    }
    
    if (m_mediaPlayer->state() == QMediaPlayer::PlayingState) {
        m_mediaPlayer->stop();
    }
    
    m_isTestRecording = false;
    ui->testRecordButton->setEnabled(true);
    ui->testPlayButton->setEnabled(true);
    ui->stopTestButton->setEnabled(false);
    ui->testStatusLabel->setText("✓ Запись сохранена. Нажмите 'Воспроизвести' для прослушивания");
}

void AudioConfigDialog::onSaveClicked()
{
    // Сохраняем настройки
    QSettings settings("MyCompany", "SpeechRecognition");
    
    settings.setValue("AudioConfig/SampleRate", getSampleRate());
    settings.setValue("AudioConfig/BitRate", getBitRate());
    settings.setValue("AudioConfig/Channels", getChannelCount());
    settings.setValue("AudioDevice", getAudioDevice());
    
    // Сохраняем настройки эквалайзера
    settings.setValue("AudioConfig/EQ60", ui->eq60Slider->value());
    settings.setValue("AudioConfig/EQ250", ui->eq250Slider->value());
    settings.setValue("AudioConfig/EQ1k", ui->eq1kSlider->value());
    settings.setValue("AudioConfig/EQ4k", ui->eq4kSlider->value());
    settings.setValue("AudioConfig/EQ16k", ui->eq16kSlider->value());
    
    emit settingsSaved();
    accept();
}

void AudioConfigDialog::onCancelClicked()
{
    reject();
}

void AudioConfigDialog::onRefreshDevicesClicked()
{
    QStringList devices;
    QList<QAudioDeviceInfo> audioDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    
    for (const QAudioDeviceInfo &deviceInfo : audioDevices) {
        devices.append(deviceInfo.deviceName());
    }
    
    setAvailableAudioDevices(devices);
    
    if (devices.isEmpty()) {
        QMessageBox::warning(this, "Устройства не найдены",
            "Не найдено ни одного устройства записи.\n"
            "Подключите микрофон и попробуйте снова.");
    } else {
        ui->testStatusLabel->setText(QString("Найдено устройств: %1").arg(devices.size()));
    }
}

void AudioConfigDialog::startLevelMonitoring()
{
    m_levelTimer->start(50); // Обновляем каждые 50мс
    ui->levelProgressBar->setValue(0);
}

void AudioConfigDialog::stopLevelMonitoring()
{
    m_levelTimer->stop();
    ui->levelProgressBar->setValue(0);
    ui->levelLabel->setText("Запись остановлена");
}

void AudioConfigDialog::updateAudioLevel()
{
    // Обновляем визуализацию уровня
    int level = static_cast<int>(m_currentLevel * 100);
    ui->levelProgressBar->setValue(level);
    
    if (level < 20) {
        ui->levelLabel->setText("Слишком тихо");
        ui->levelProgressBar->setStyleSheet("QProgressBar::chunk { background-color: #f44336; }");
    } else if (level < 40) {
        ui->levelLabel->setText("Тихо");
        ui->levelProgressBar->setStyleSheet("QProgressBar::chunk { background-color: #ff9800; }");
    } else if (level < 80) {
        ui->levelLabel->setText("Хороший уровень");
        ui->levelProgressBar->setStyleSheet("QProgressBar::chunk { background-color: #4caf50; }");
    } else {
        ui->levelLabel->setText("Громко");
        ui->levelProgressBar->setStyleSheet("QProgressBar::chunk { background-color: #2196f3; }");
    }
}

void AudioConfigDialog::updateVisualization(const QAudioBuffer &buffer)
{
    if (!buffer.isValid() || buffer.frameCount() == 0) {
        return;
    }
    
    // Вычисляем средний уровень сигнала
    qreal sum = 0.0;
    const qint16 *data = buffer.constData<qint16>();
    int count = buffer.frameCount() * buffer.format().channelCount();
    
    for (int i = 0; i < count; ++i) {
        qreal sample = qAbs(data[i]) / 32768.0; // Нормализуем к [0, 1]
        sum += sample;
    }
    
    m_currentLevel = sum / count;
}

void AudioConfigDialog::onRecordingStateChanged()
{
    QMediaRecorder::State state = m_audioRecorder->state();
    
    if (state == QMediaRecorder::StoppedState && m_isTestRecording) {
        // Запись завершена
        qDebug() << "Test recording saved to:" << m_testFilePath;
        
        QFile testFile(m_testFilePath);
        if (testFile.exists() && testFile.size() > 1000) {
            ui->testStatusLabel->setText("✓ Запись сохранена. Нажмите 'Воспроизвести' для прослушивания");
            ui->testPlayButton->setEnabled(true);
        } else {
            ui->testStatusLabel->setText("❌ Ошибка записи");
            QMessageBox::warning(this, "Ошибка записи",
                "Не удалось записать аудио. Проверьте:\n"
                "1. Подключен ли микрофон\n"
                "2. Разрешен ли доступ к микрофону\n"
                "3. Не используется ли микрофон другой программой");
        }
    }
}

void AudioConfigDialog::onPlayerStateChanged()
{
    if (m_mediaPlayer->state() == QMediaPlayer::StoppedState) {
        ui->testPlayButton->setEnabled(true);
        ui->stopTestButton->setEnabled(false);
        ui->testStatusLabel->setText("✓ Воспроизведение завершено");
    }
}

void AudioConfigDialog::applyEQSettings()
{
    // Здесь можно добавить применение настроек эквалайзера
    // В текущей версии значения сохраняются и могут использоваться
    // при обработке аудио в будущем
    qDebug() << "EQ Settings:"
             << "60Hz:" << ui->eq60Slider->value()
             << "250Hz:" << ui->eq250Slider->value()
             << "1kHz:" << ui->eq1kSlider->value()
             << "4kHz:" << ui->eq4kSlider->value()
             << "16kHz:" << ui->eq16kSlider->value();
}

QString AudioConfigDialog::getPresetsFilePath() const
{
    QString appDir = QCoreApplication::applicationDirPath();
    return appDir + "/presets.ini";
}

void AudioConfigDialog::loadPresets()
{
    ui->presetComboBox->clear();
    ui->presetComboBox->addItem("-- Выберите пресет --");
    
    QString presetsFile = getPresetsFilePath();
    
    if (!QFile::exists(presetsFile)) {
        qDebug() << "Presets file not found, creating default presets";
        // Создаем пресеты по умолчанию
        QSettings presets(presetsFile, QSettings::IniFormat);
        
        // Пресет для речи
        presets.beginGroup("Речь (оптимально)");
        presets.setValue("SampleRate", 16000);
        presets.setValue("BitRate", 256000);
        presets.setValue("Channels", 1);
        presets.setValue("EQ60", 0);
        presets.setValue("EQ250", 0);
        presets.setValue("EQ1k", 3);
        presets.setValue("EQ4k", 3);
        presets.setValue("EQ16k", 0);
        presets.endGroup();
        
        // Пресет для музыки
        presets.beginGroup("Музыка (высокое качество)");
        presets.setValue("SampleRate", 48000);
        presets.setValue("BitRate", 320000);
        presets.setValue("Channels", 2);
        presets.setValue("EQ60", 0);
        presets.setValue("EQ250", 0);
        presets.setValue("EQ1k", 0);
        presets.setValue("EQ4k", 0);
        presets.setValue("EQ16k", 0);
        presets.endGroup();
        
        // Пресет для шумного окружения
        presets.beginGroup("Шумное окружение");
        presets.setValue("SampleRate", 16000);
        presets.setValue("BitRate", 256000);
        presets.setValue("Channels", 1);
        presets.setValue("EQ60", -6);
        presets.setValue("EQ250", -3);
        presets.setValue("EQ1k", 6);
        presets.setValue("EQ4k", 6);
        presets.setValue("EQ16k", -3);
        presets.endGroup();
        
        presets.sync();
    }
    
    QSettings presets(presetsFile, QSettings::IniFormat);
    QStringList groups = presets.childGroups();
    
    for (const QString &group : groups) {
        ui->presetComboBox->addItem(group);
    }
    
    qDebug() << "Loaded presets:" << groups;
}

void AudioConfigDialog::onSavePresetClicked()
{
    QString presetName = ui->presetNameEdit->text().trimmed();
    
    if (presetName.isEmpty()) {
        QMessageBox::warning(this, "Ошибка",
            "Введите название пресета");
        return;
    }
    
    // Проверяем, существует ли уже такой пресет
    QString presetsFile = getPresetsFilePath();
    QSettings presets(presetsFile, QSettings::IniFormat);
    
    if (presets.childGroups().contains(presetName)) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Пресет существует",
            "Пресет с таким названием уже существует.\n"
            "Перезаписать?",
            QMessageBox::Yes|QMessageBox::No);
        
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    
    // Сохраняем пресет
    savePresetToFile(presetName);
    
    // Обновляем список
    loadPresets();
    
    // Выбираем новый пресет
    int index = ui->presetComboBox->findText(presetName);
    if (index != -1) {
        ui->presetComboBox->setCurrentIndex(index);
    }
    
    ui->presetNameEdit->clear();
    
    QMessageBox::information(this, "Успех",
        "Пресет '" + presetName + "' успешно сохранен!");
}

void AudioConfigDialog::onLoadPresetClicked()
{
    QString presetName = ui->presetComboBox->currentText();
    
    if (presetName.isEmpty() || presetName == "-- Выберите пресет --") {
        QMessageBox::warning(this, "Ошибка",
            "Выберите пресет из списка");
        return;
    }
    
    loadPresetFromFile(presetName);
    
    QMessageBox::information(this, "Успех",
        "Пресет '" + presetName + "' загружен!");
}

void AudioConfigDialog::onDeletePresetClicked()
{
    QString presetName = ui->presetComboBox->currentText();
    
    if (presetName.isEmpty() || presetName == "-- Выберите пресет --") {
        QMessageBox::warning(this, "Ошибка",
            "Выберите пресет из списка");
        return;
    }
    
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Удалить пресет?",
        "Вы уверены, что хотите удалить пресет\n'" + presetName + "'?",
        QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        deletePresetFromFile(presetName);
        loadPresets();
        
        QMessageBox::information(this, "Успех",
            "Пресет '" + presetName + "' удален!");
    }
}

void AudioConfigDialog::onPresetSelected(int index)
{
    if (index <= 0) {
        return; // "-- Выберите пресет --"
    }
    
    QString presetName = ui->presetComboBox->currentText();
    
    // Показываем информацию о пресете
    QString presetsFile = getPresetsFilePath();
    QSettings presets(presetsFile, QSettings::IniFormat);
    
    presets.beginGroup(presetName);
    int sampleRate = presets.value("SampleRate", 16000).toInt();
    int bitRate = presets.value("BitRate", 256000).toInt();
    int channels = presets.value("Channels", 1).toInt();
    presets.endGroup();
    
    ui->presetInfoLabel->setText(
        QString("💡 %1: %2 Hz, %3 kbps, %4 канал(а)")
        .arg(presetName)
        .arg(sampleRate)
        .arg(bitRate / 1000)
        .arg(channels)
    );
}

void AudioConfigDialog::savePresetToFile(const QString &presetName)
{
    QString presetsFile = getPresetsFilePath();
    QSettings presets(presetsFile, QSettings::IniFormat);
    
    presets.beginGroup(presetName);
    presets.setValue("SampleRate", getSampleRate());
    presets.setValue("BitRate", getBitRate());
    presets.setValue("Channels", getChannelCount());
    presets.setValue("EQ60", ui->eq60Slider->value());
    presets.setValue("EQ250", ui->eq250Slider->value());
    presets.setValue("EQ1k", ui->eq1kSlider->value());
    presets.setValue("EQ4k", ui->eq4kSlider->value());
    presets.setValue("EQ16k", ui->eq16kSlider->value());
    presets.endGroup();
    
    presets.sync();
    
    qDebug() << "Preset saved:" << presetName;
}

void AudioConfigDialog::loadPresetFromFile(const QString &presetName)
{
    QString presetsFile = getPresetsFilePath();
    QSettings presets(presetsFile, QSettings::IniFormat);
    
    presets.beginGroup(presetName);
    
    int sampleRate = presets.value("SampleRate", 16000).toInt();
    int bitRate = presets.value("BitRate", 256000).toInt();
    int channels = presets.value("Channels", 1).toInt();
    
    setSampleRate(sampleRate);
    setBitRate(bitRate);
    setChannelCount(channels);
    
    ui->eq60Slider->setValue(presets.value("EQ60", 0).toInt());
    ui->eq250Slider->setValue(presets.value("EQ250", 0).toInt());
    ui->eq1kSlider->setValue(presets.value("EQ1k", 0).toInt());
    ui->eq4kSlider->setValue(presets.value("EQ4k", 0).toInt());
    ui->eq16kSlider->setValue(presets.value("EQ16k", 0).toInt());
    
    presets.endGroup();
    
    qDebug() << "Preset loaded:" << presetName;
}

void AudioConfigDialog::deletePresetFromFile(const QString &presetName)
{
    QString presetsFile = getPresetsFilePath();
    QSettings presets(presetsFile, QSettings::IniFormat);
    
    presets.beginGroup(presetName);
    presets.remove(""); // Удаляет всю группу
    presets.endGroup();
    
    presets.sync();
    
    qDebug() << "Preset deleted:" << presetName;
}
