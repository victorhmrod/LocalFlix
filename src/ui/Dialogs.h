#pragma once

#include "LibraryManager.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace lf::ui {

// Summary of an import: how many were added, which files were skipped as
// duplicates (and where they already live) and which could not be read.
class ImportReportDialog : public QDialog {
    Q_OBJECT
public:
    ImportReportDialog(const ImportReport& report, QWidget* parent = nullptr);
};

// Title + description + cover/poster editor for shows and movies.
class EntityEditDialog : public QDialog {
    Q_OBJECT
public:
    EntityEditDialog(const QString& heading, const QString& title, const QString& description, const QString& currentImagePath,
                     const QString& imageNoun, QWidget* parent = nullptr);

    QString title() const;
    QString description() const;
    // Path of a newly chosen image, empty when unchanged.
    QString newImagePath() const { return m_newImage; }
    bool imageRemoved() const { return m_imageRemoved; }

private:
    void chooseImage();
    void removeImage();
    void refreshPreview();

    QLineEdit* m_title;
    QPlainTextEdit* m_description;
    QLabel* m_preview;
    QString m_currentImage;
    QString m_newImage;
    bool m_imageRemoved = false;
};

class SeasonDialog : public QDialog {
    Q_OBJECT
public:
    SeasonDialog(const QString& heading, const QString& title, int number, QWidget* parent = nullptr);
    QString title() const;
    int number() const;

private:
    QLineEdit* m_title;
    QSpinBox* m_number;
};

enum class ResumeChoice { Resume, StartOver, Cancel };

// "Resume from 12:34 or start over?" (shown when "resume automatically" is off).
ResumeChoice askResumeChoice(QWidget* parent, const QString& title, qint64 positionMs);

}  // namespace lf::ui
