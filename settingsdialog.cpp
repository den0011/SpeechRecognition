#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "dictation.h"

#include <QCheckBox>
#include <QComboBox>

#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    setWindowTitle("Настройки - Распознавание речи");
    setMinimumSize(650, 600);
    resize(650, 600);

    connect(ui->browseWhisperButton, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseWhisperClicked);
    connect(ui->browseModelButton, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseModelClicked);
    connect(ui->saveButton, &QPushButton::clicked,
            this, &SettingsDialog::onSaveClicked);
    connect(ui->cancelButton, &QPushButton::clicked,
            this, &SettingsDialog::onCancelClicked);
    connect(ui->refreshDevicesButton, &QPushButton::clicked,
            this, &SettingsDialog::onRefreshDevicesClicked);

    ui->whisperPathEdit->setToolTip("Путь к исполняемому файлу whisper.cpp/whisper-cli");
    ui->modelPathEdit->setToolTip("Путь к файлу модели whisper (.bin файл)");
    ui->audioDeviceComboBox->setToolTip("Выберите микрофон для записи");
    ui->performanceModeComboBox->setItemData(0, "fast");
    ui->performanceModeComboBox->setItemData(1, "balanced");
    ui->performanceModeComboBox->setItemData(2, "accurate");
    ui->performanceModeComboBox->setItemData(0, "Быстрее: укороченное окно распознавания, чуть менее точно", Qt::ToolTipRole);
    ui->performanceModeComboBox->setItemData(1, "Обычный режим", Qt::ToolTipRole);
    ui->performanceModeComboBox->setItemData(2, "Точнее: перебор 5 вариантов (beam search), примерно на 25% медленнее", Qt::ToolTipRole);
    ui->performanceModeComboBox->setToolTip("Модель выбирается в поле «Путь к модели» и не подменяется автоматически");
    ui->performanceModeComboBox->setCurrentIndex(1);

    // Дополнительные языки и автоопределение
    ui->languageComboBox->addItems(QStringList() << "pl" << "auto");
    ui->languageComboBox->setToolTip("auto — язык определяется автоматически (медленнее и иногда ошибается на коротких фразах)");

    // Горячая клавиша для записи из любой программы (режим диктовки)
    m_hotkeyCombo = new QComboBox(this);
    m_hotkeyCombo->addItem("Не использовать", QString());
    for (const QString &shortcut : GlobalHotkey::availableShortcuts()) {
        m_hotkeyCombo->addItem(shortcut, shortcut);
    }
    m_hotkeyCombo->setToolTip("Начать/остановить запись, даже когда окно программы свёрнуто.\n"
                              "С галочкой «Вставлять в активное окно» текст печатается туда, где стоит курсор.");
    ui->formLayout->addRow("Горячая клавиша:", m_hotkeyCombo);

    // Фильтр шума Silero VAD
    m_vadCheck = new QCheckBox("Нейросетевой фильтр шума (Silero VAD)", this);
    m_vadCheck->setToolTip("Отсекает шум и музыку внутри фраз, уменьшает «выдуманный» текст.\n"
                           "Модель (0.9 МБ) скачивается через «Файл → Скачать Whisper и модели».");
    ui->formLayout->addRow("Шумоподавление:", m_vadCheck);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

QString SettingsDialog::getWhisperPath() const
{
    return ui->whisperPathEdit->text();
}

QString SettingsDialog::getModelPath() const
{
    return ui->modelPathEdit->text();
}

QString SettingsDialog::getLanguage() const
{
    return ui->languageComboBox->currentText();
}

QString SettingsDialog::getAudioDevice() const
{
    return ui->audioDeviceComboBox->currentText();
}

QString SettingsDialog::getPerformanceMode() const
{
    return ui->performanceModeComboBox->currentData().toString();
}

QString SettingsDialog::getHotkey() const
{
    return m_hotkeyCombo->currentData().toString();
}

bool SettingsDialog::getUseVad() const
{
    return m_vadCheck->isChecked();
}

void SettingsDialog::setHotkey(const QString &hotkey)
{
    const int index = m_hotkeyCombo->findData(hotkey);
    m_hotkeyCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void SettingsDialog::setUseVad(bool enabled, bool available)
{
    m_vadCheck->setEnabled(available);
    m_vadCheck->setChecked(enabled && available);
    if (!available) {
        m_vadCheck->setText("Нейросетевой фильтр шума (Silero VAD) — модель не скачана");
    }
}

void SettingsDialog::setWhisperPath(const QString &path)
{
    ui->whisperPathEdit->setText(path);
}

void SettingsDialog::setModelPath(const QString &path)
{
    ui->modelPathEdit->setText(path);
}

void SettingsDialog::setLanguage(const QString &language)
{
    int index = ui->languageComboBox->findText(language);
    if (index != -1) {
        ui->languageComboBox->setCurrentIndex(index);
    }
}

void SettingsDialog::setAudioDevice(const QString &device)
{
    int index = ui->audioDeviceComboBox->findText(device);
    if (index != -1) {
        ui->audioDeviceComboBox->setCurrentIndex(index);
    }
}

void SettingsDialog::setPerformanceMode(const QString &mode)
{
    int index = ui->performanceModeComboBox->findData(mode);
    if (index != -1) {
        ui->performanceModeComboBox->setCurrentIndex(index);
    }
}

void SettingsDialog::setAvailableAudioDevices(const QStringList &devices)
{
    ui->audioDeviceComboBox->clear();

    if (devices.isEmpty()) {
        ui->audioDeviceComboBox->addItem("Устройства не найдены");
        ui->audioDeviceComboBox->setEnabled(false);
    } else {
        ui->audioDeviceComboBox->addItems(devices);
        ui->audioDeviceComboBox->setEnabled(true);
    }
}

void SettingsDialog::onBrowseWhisperClicked()
{
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString fileName = QFileDialog::getOpenFileName(this,
        "Выберите исполняемый файл whisper",
        homeDir,
        "Исполняемые файлы (*);;Все файлы (*.*)");

    if (!fileName.isEmpty()) {
        ui->whisperPathEdit->setText(fileName);
    }
}

void SettingsDialog::onBrowseModelClicked()
{
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString fileName = QFileDialog::getOpenFileName(this,
        "Выберите файл модели whisper",
        homeDir,
        "Модели whisper (*.bin);;Все файлы (*.*)");

    if (!fileName.isEmpty()) {
        ui->modelPathEdit->setText(fileName);
    }
}

void SettingsDialog::onSaveClicked()
{
    if (ui->whisperPathEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Предупреждение",
            "Пожалуйста, укажите путь к whisper");
        return;
    }

    if (ui->modelPathEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Предупреждение",
            "Пожалуйста, укажите путь к модели");
        return;
    }

    if (ui->audioDeviceComboBox->currentText() == "Устройства не найдены") {
        QMessageBox::warning(this, "Предупреждение",
            "Не найдено ни одного устройства записи звука.\n"
            "Подключите микрофон и перезапустите программу.");
        return;
    }

    accept();
}

void SettingsDialog::onCancelClicked()
{
    reject();
}

void SettingsDialog::onRefreshDevicesClicked()
{
    emit refreshAudioDevices();
}
