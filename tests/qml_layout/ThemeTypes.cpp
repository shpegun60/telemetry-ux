// The Theme and Type singletons load from the module, Type follows the
// application font, and a role a host assigns reaches every binding. MIT.
#include <QtCore/QDebug>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
int checks = 0;
int warnings = 0;
void messages(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        ++warnings;
    std::fprintf(stderr, "%s\n", qPrintable(message));
}
bool near(double a, double b) { return std::abs(a - b) < 0.01; }
#define CHECK(x) do { ++checks; if (!(x)) { qCritical() << "Theme failure" << __LINE__ << #x; return 1; } } while (false)
} // namespace

int main(int argc, char** argv)
{
    qInstallMessageHandler(messages);
    QGuiApplication application(argc, argv);
    // The host sets its body size once, before any window loads: 16 px is 12 pt.
    QFont font = QGuiApplication::font();
    font.setPointSizeF(12);
    QGuiApplication::setFont(font);
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/ThemeTypes.qml")));
    QObject* root = nullptr;
    for (auto* object : engine.rootObjects())
        if (object->objectName() == QStringLiteral("themeTypes"))
            root = object;
    CHECK(root != nullptr);
    // Type: the application font is the body; the other steps derive from it.
    CHECK(near(root->property("body").toDouble(), 12.0));
    CHECK(near(root->property("caption").toDouble(), 12.0 / 1.125));
    CHECK(near(root->property("heading").toDouble(), 12.0 * 1.125 * 1.125));
    // Every size is a multiple of the body in pixels, rounded: a 12 pt body is
    // 16 px at 96 dpi, so rows 26, controls 29, chips 24; room 7 / 5 / 10 / 3;
    // icons 19 and an even 18 px check box.
    const double pixels = root->property("bodyPixels").toDouble();
    const auto at = [&](double ratio) { return int(std::lround(pixels * ratio)); };
    CHECK(pixels > 0);
    CHECK(root->property("rowHeight").toInt() == at(1.65) && root->property("controlHeight").toInt() == at(1.8) &&
          root->property("chipHeight").toInt() == at(1.5));
    CHECK(root->property("padding").toInt() == at(0.45) && root->property("spacing").toInt() == at(0.3) &&
          root->property("groupSpacing").toInt() == at(0.6) && root->property("tight").toInt() == std::max(2, at(0.2)));
    CHECK(root->property("iconSize").toInt() == at(1.2) && root->property("checkSize").toInt() == 2 * at(0.55));
    CHECK(root->property("checkSize").toInt() % 2 == 0);
    const int rowBefore = root->property("rowHeight").toInt();
    // Theme: the defaults, then a host's role reaching an existing binding.
    CHECK(root->property("surfaceWindow").value<QColor>() == QColor(QStringLiteral("#11151c")));
    CHECK(root->property("muted").value<QColor>() == QColor(QStringLiteral("#8c9fb3")));
    CHECK(root->property("accentBefore").value<QColor>() == QColor(QStringLiteral("#6fd4bf")));
    QMetaObject::invokeMethod(root, "restyle");
    CHECK(root->property("accentAfter").value<QColor>() == QColor(QStringLiteral("#ff8800")));
    QMetaObject::invokeMethod(root, "setBody", Q_ARG(QVariant, 14.0));
    CHECK(near(root->property("body").toDouble(), 14.0));
    CHECK(near(root->property("caption").toDouble(), 14.0 / 1.125));
    // A zoom is a new body: the sizes follow it, never the text alone.
    CHECK(root->property("rowHeight").toInt() > rowBefore &&
          root->property("rowHeight").toInt() ==
              int(std::lround(root->property("bodyPixels").toDouble() * 1.65)));
    std::printf("Theme checks=%d warnings=%d\n", checks, warnings);
    return warnings == 0 ? 0 : 1;
}
