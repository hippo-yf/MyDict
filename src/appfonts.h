#pragma once
#include <QSet>
#include <QFont>
#include <QString>
#include <QStringList>

namespace MyDict {

// Call on the GUI thread after QApplication exists. Metadata discovery reads
// only SFNT name tables; only requested local families are registered with Qt.
class AppFonts {
public:
    static void apply(const QSet<QString> &requiredFamilies, const QStringList &fontDirectories);
    static QString resolve(QString family);
    // Preserve a real local bold-italic face when installed versions of the
    // same family introduce conflicting style weights in Qt's font database.
    static QFont styledFont(QFont font);
    static QStringList loadedFiles();
    // Local metadata names only; never enumerates or initializes system fonts.
    static QStringList availableFamilies();
};

}
