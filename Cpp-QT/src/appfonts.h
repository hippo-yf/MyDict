#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

namespace MyDict {

// Call on the GUI thread after QApplication exists. Metadata discovery reads
// only SFNT name tables; only requested local families are registered with Qt.
class AppFonts {
public:
    static void apply(const QSet<QString> &requiredFamilies, const QStringList &fontDirectories);
    static QString resolve(QString family);
    static QStringList loadedFiles();
    // Local metadata names only; never enumerates or initializes system fonts.
    static QStringList availableFamilies();
};

}
