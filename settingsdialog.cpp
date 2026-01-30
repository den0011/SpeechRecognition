#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    // Установка заголовка окна
    setWindowTitle("Настройки - Распознавание речи");

    // Настройка размеров окна
    setMinimumSize(650, 600);
    resize(650, 600);

    // Подключение слотов
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

    // Установка подсказок
    ui->whisperPathEdit->setToolTip("Путь к исполняемому файлу whisper.cpp/main");
    ui->modelPathEdit->setToolTip("Путь к файлу модели whisper (.bin файл)");
    ui->audioDeviceComboBox->setToolTip("Выберите микрофон для записи");
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
    // Запрашиваем обновленный список устройств у родительского окна
    emit refreshAudioDevices();
}
