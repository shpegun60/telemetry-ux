// The Theme and Type singletons load from the module, Type follows the
// application font, and a role a host assigns reaches every binding. MIT.
#include <QtCore/QDebug>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
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
    CHECK(root->property("rowHeight").toInt() == 32);
    // Theme: the defaults, then a host's role reaching an existing binding.
    CHECK(root->property("surfaceWindow").value<QColor>() == QColor(QStringLiteral("#11151c")));
    CHECK(root->property("muted").value<QColor>() == QColor(QStringLiteral("#8597ab")));
    CHECK(root->property("accentBefore").value<QColor>() == QColor(QStringLiteral("#6fd4bf")));
    QMetaObject::invokeMethod(root, "restyle");
    CHECK(root->property("accentAfter").value<QColor>() == QColor(QStringLiteral("#ff8800")));
    std::printf("Theme checks=%d warnings=%d\n", checks, warnings);
    return warnings == 0 ? 0 : 1;
}
