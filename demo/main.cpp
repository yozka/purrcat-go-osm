#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
	QGuiApplication app(argc, argv);
	QCoreApplication::setOrganizationName(QStringLiteral("PurrCatGO"));
	QCoreApplication::setApplicationName(QStringLiteral("OsmDemo"));
	QQuickStyle::setStyle(QStringLiteral("Basic"));

	QQmlApplicationEngine engine;
	QObject::connect(
		&engine,
		&QQmlApplicationEngine::objectCreationFailed,
		&app,
		[]() { QCoreApplication::exit(-1); },
		Qt::QueuedConnection);

	const QString customCacheDir =
		QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
		+ QStringLiteral("/OsmDemoCustom");
	engine.rootContext()->setContextProperty(QStringLiteral("customCacheDir"), customCacheDir);

	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/OsmDemo/main.qml")));

	return app.exec();
}
