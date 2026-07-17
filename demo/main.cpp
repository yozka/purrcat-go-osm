#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

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

	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/OsmDemo/main.qml")));

	return app.exec();
}
