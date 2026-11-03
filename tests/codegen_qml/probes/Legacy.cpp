// Keep Qt's public keyword macros available to an ordinary generated SDK consumer.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#include <QCoreApplication>
#include <QmlValues.hpp>
#include <DeviceApi.hpp>
#include <iostream>

#if defined(QT_NO_KEYWORDS) || defined(QT_NO_FOREACH)
#error The generated module must not disable consumer Qt keywords or foreach.
#endif
#if !defined(signals) || !defined(slots) || !defined(emit) || !defined(foreach) || !defined(qApp)
#error Generated headers did not restore consumer Qt keyword macros.
#endif
#if !defined(Q_OBJECT) || !defined(Q_GADGET) || !defined(QT_VERSION) || !defined(Q_SIGNALS) ||     \
    !defined(Q_SLOTS) || !defined(Q_INVOKABLE) || !defined(QML_ELEMENT) || !defined(Q_ASSERT)
#error Generated headers did not restore consumer Qt declaration macros.
#endif

class Consumer : public QObject {
	Q_OBJECT
public:
	int received = 0;

	void send()
	{
		emit fired();
	}

public slots:

	void receive()
	{
		++received;
	}

signals:
	void fired();
};

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	Consumer consumer;
	QObject::connect(&consumer, &Consumer::fired, &consumer, &Consumer::receive);
	consumer.send();
	if (consumer.received != 1 || qApp != &application)
		return 1;
	variant::hostile::qml::DeviceApi api;
	if (api.ready())
		return 2;
	std::cout << "qml legacy checks=3 failures=0\n";
	return 0;
}

#include "Legacy.moc"
