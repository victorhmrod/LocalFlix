#include "Dialogs.h"

#include "Format.h"
#include "Theme.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lf::ui {

// ---------------------------------------------------------------- import report

ImportReportDialog::ImportReportDialog(const ImportReport& report, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Import results"));
    setMinimumSize(620, 380);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);

    auto* summary = new QLabel(this);
    summary->setObjectName(QStringLiteral("SectionTitle"));
    summary->setText(report.ok ? QStringLiteral("%1 added · %2 already in the library · %3 could not be imported")
                                     .arg(report.added)
                                     .arg(report.duplicates.size())
                                     .arg(report.rejected.size())
                               : QStringLiteral("Import failed: %1").arg(report.error));
    root->addWidget(summary);

    if (!report.duplicates.isEmpty()) {
        auto* box = new QGroupBox(QStringLiteral("Skipped: already in the library (each file can only be added once)"), this);
        auto* l = new QVBoxLayout(box);
        auto* list = new QListWidget(box);
        for (const DuplicateInfo& d : report.duplicates) {
            auto* item = new QListWidgetItem(QStringLiteral("%1\n    → already in: %2").arg(QDir::toNativeSeparators(d.path), d.existingAs));
            item->setToolTip(d.path);
            list->addItem(item);
        }
        l->addWidget(list);
        root->addWidget(box, 1);
    }
    if (!report.rejected.isEmpty()) {
        auto* box = new QGroupBox(QStringLiteral("Could not be imported"), this);
        auto* l = new QVBoxLayout(box);
        auto* list = new QListWidget(box);
        list->addItems(report.rejected);
        l->addWidget(list);
        root->addWidget(box, 1);
    }
    if (report.duplicates.isEmpty() && report.rejected.isEmpty()) root->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);
}

// ---------------------------------------------------------------- entity editor

EntityEditDialog::EntityEditDialog(const QString& heading, const QString& title, const QString& description, const QString& currentImagePath,
                                   const QString& imageNoun, QWidget* parent)
    : QDialog(parent), m_currentImage(currentImagePath)
{
    setWindowTitle(heading);
    setMinimumWidth(560);

    m_title = new QLineEdit(title, this);
    m_title->setPlaceholderText(QStringLiteral("Title"));
    m_description = new QPlainTextEdit(description, this);
    m_description->setPlaceholderText(QStringLiteral("Description (optional)"));
    m_description->setMinimumHeight(110);

    m_preview = new QLabel(this);
    m_preview->setFixedSize(120, 180);
    m_preview->setScaledContents(false);
    auto* choose = new QPushButton(Theme::icon(Icon::Image, Colors::text, 18), QStringLiteral("Choose %1…").arg(imageNoun), this);
    auto* remove = new QPushButton(QStringLiteral("Remove"), this);
    remove->setProperty("variant", QStringLiteral("ghost"));
    connect(choose, &QPushButton::clicked, this, &EntityEditDialog::chooseImage);
    connect(remove, &QPushButton::clicked, this, &EntityEditDialog::removeImage);

    auto* imageButtons = new QVBoxLayout;
    imageButtons->addWidget(choose);
    imageButtons->addWidget(remove);
    imageButtons->addStretch();
    auto* imageRow = new QHBoxLayout;
    imageRow->setSpacing(16);
    imageRow->addWidget(m_preview);
    imageRow->addLayout(imageButtons);
    imageRow->addStretch();

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignTop | Qt::AlignRight);
    form->setSpacing(12);
    form->addRow(QStringLiteral("Title"), m_title);
    form->addRow(QStringLiteral("Description"), m_description);
    form->addRow(imageNoun.left(1).toUpper() + imageNoun.mid(1), imageRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setProperty("variant", QStringLiteral("primary"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_title->text().trimmed().isEmpty()) {
            m_title->setFocus();
            m_title->setStyleSheet(QStringLiteral("border-color: %1;").arg(Colors::danger.name()));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(16);
    root->addLayout(form);
    root->addWidget(buttons);

    refreshPreview();
    m_title->setFocus();
    m_title->selectAll();
}

QString EntityEditDialog::title() const
{
    return m_title->text().trimmed();
}

QString EntityEditDialog::description() const
{
    return m_description->toPlainText().trimmed();
}

void EntityEditDialog::refreshPreview()
{
    const QString path = !m_newImage.isEmpty() ? m_newImage : (m_imageRemoved ? QString() : m_currentImage);
    m_preview->setPixmap(Theme::cover(path, m_title->text().isEmpty() ? QStringLiteral("?") : m_title->text(), m_preview->size(), devicePixelRatioF()));
}

void EntityEditDialog::chooseImage()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose image"), QString(),
                                                      QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg);;All files (*)"));
    if (file.isEmpty()) return;
    if (!QImageReader(file).canRead()) {
        QMessageBox::warning(this, QStringLiteral("Image"), QStringLiteral("That file could not be read as an image."));
        return;
    }
    m_newImage = file;
    m_imageRemoved = false;
    refreshPreview();
}

void EntityEditDialog::removeImage()
{
    m_newImage.clear();
    m_imageRemoved = true;
    refreshPreview();
}

// ---------------------------------------------------------------- season editor

SeasonDialog::SeasonDialog(const QString& heading, const QString& title, int number, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(heading);
    setMinimumWidth(420);
    m_title = new QLineEdit(title, this);
    m_number = new QSpinBox(this);
    m_number->setRange(0, 999);
    m_number->setValue(number);
    m_number->setSpecialValueText(QStringLiteral("None (specials)"));
    auto* form = new QFormLayout;
    form->setSpacing(12);
    form->addRow(QStringLiteral("Title"), m_title);
    form->addRow(QStringLiteral("Season number"), m_number);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_title->text().trimmed().isEmpty()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* root = new QVBoxLayout(this);
    root->setSpacing(16);
    root->addLayout(form);
    root->addWidget(buttons);
    m_title->setFocus();
    m_title->selectAll();
}

QString SeasonDialog::title() const
{
    return m_title->text().trimmed();
}

int SeasonDialog::number() const
{
    return m_number->value();
}

// ---------------------------------------------------------------- resume choice

ResumeChoice askResumeChoice(QWidget* parent, const QString& title, qint64 positionMs)
{
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("Resume playback?"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    box.setInformativeText(QStringLiteral("You stopped at %1.").arg(formatClock(positionMs)));
    QPushButton* resume = box.addButton(QStringLiteral("Resume from %1").arg(formatClock(positionMs)), QMessageBox::AcceptRole);
    QPushButton* restart = box.addButton(QStringLiteral("Start over"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(resume);
    box.exec();
    if (box.clickedButton() == resume) return ResumeChoice::Resume;
    if (box.clickedButton() == restart) return ResumeChoice::StartOver;
    return ResumeChoice::Cancel;
}

}  // namespace lf::ui
