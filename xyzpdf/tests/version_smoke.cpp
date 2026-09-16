// The version, and the manifests it gets stamped into.
//
// The version is written once, in project(VERSION) in CMakeLists.txt, and
// reaches the About box and the VERSIONINFO resource through a generated
// header. The packaging scripts read that same header and stamp
// Identity/@Version into whichever manifest they are packing, so the number in
// the committed manifests is a placeholder and deliberately zero -- a plausible
// number there would invite someone to edit it and believe it did something.
//
// What is worth checking, then, is not that the manifests agree with the build
// (they cannot disagree any more) but that they are still parseable and still
// placeholders. The first of those is not hypothetical: an XML comment cannot
// contain a double hyphen, and adding one to a manifest breaks the packaging
// script at the point where it loads the file -- after it has already deployed
// the Qt runtime into a temporary layout.

#include "../src/core/aboutinfo.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QXmlStreamReader>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

struct Manifest
{
    bool read = false;
    bool parses = false;
    QString version;
    QString parseError;
};

Manifest readManifest(const QString &path)
{
    Manifest result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    result.read = true;
    const QByteArray text = file.readAll();
    file.close();

    QXmlStreamReader xml(text);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement
            && xml.name() == QLatin1String("Identity")) {
            result.version = xml.attributes().value(QLatin1String("Version")).toString();
        }
    }
    result.parses = !xml.hasError();
    result.parseError = xml.errorString();
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    const QString version = QStringLiteral(XYZPDF_VERSION);
    const QString quad = QStringLiteral(XYZPDF_VERSION_QUAD);

    check(QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+$")).match(version).hasMatch(),
          "the version is three dotted numbers");
    check(quad == version + QStringLiteral(".0"),
          "the four-part version is the three-part one with a zero revision");

    // The About box has to show the version this was built as. Asserting on the
    // number rather than on the sentence, so rewording the box does not fail.
    check(!version.isEmpty() && AboutInfo::html().contains(version),
          "the About box shows the version");
    check(!AboutInfo::title().isEmpty(), "the About box has a title");

    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("packaging");
    for (const QString &name : {QStringLiteral("AppxManifest.xml"),
                                QStringLiteral("StoreAppxManifest.xml")}) {
        const QString path = directory + QLatin1Char('/') + name;
        const Manifest manifest = readManifest(path);
        if (!manifest.read) {
            std::cerr << "FAIL: could not read " << path.toStdString() << '\n';
            ++failures;
            continue;
        }
        if (!manifest.parses) {
            std::cerr << "FAIL: " << name.toStdString() << " is not well-formed XML: "
                      << manifest.parseError.toStdString() << '\n';
            ++failures;
            continue;
        }
        if (manifest.version != QStringLiteral("0.0.0.0")) {
            std::cerr << "FAIL: " << name.toStdString() << " declares Version=\""
                      << manifest.version.toStdString()
                      << "\" where it should be the placeholder 0.0.0.0 -- the packaging "
                         "script stamps the real version, so a number here is a lie\n";
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "the version holds together (" << quad.toStdString() << ")\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
