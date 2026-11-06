// A local native device supplies a runnable host example. No generated DTO or UI. MIT.
#include "Device.hpp"
#include <Telemetry/UX/DynamicConfiguratorProvider.hpp>
#include <integration/encoded/Backend.hpp>
#include <integration/encoded/Dispatcher.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuickControls2/QQuickStyle>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace ux = telemetry::ux;
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace en = telemetry::integration::encoded;
static unsigned warnings = 0;

// This application object is the existing connection owner. In a real host it
// also retains discovered Descriptor/index buffers and the transport's TX/RX.
struct DeviceConnection {
	QElapsedTimer elapsed;
	cl::Clock clock{this, +[](void* self) noexcept {
		                return static_cast<std::uint32_t>(
		                    static_cast<DeviceConnection*>(self)->elapsed.elapsed());
	                }};
	std::array<rt::RuntimeType, 32> types{};
	std::array<rt::RuntimeMember, 32> members{};
	std::array<rt::RuntimeEnumEntry, 8> enums{};
	std::array<rt::RuntimeCatalog, 8> catalogs{};
	std::array<rt::RuntimeEndpoint, 8> endpoints{};
	rt::RuntimeSchemaWorkspace indexes{types, members, enums, catalogs, endpoints};
	rt::RuntimeSchema schema;
	std::array<std::byte, 2048> tx{}, rx{}, port{}, reply{}, scratch{};
	std::array<std::byte, 65536> serviceResponse{};
	telemetry::Workspace workspace{scratch};
	en::EncodedBackend backend{tx, rx, clock};
	ux::Workbench workbench{backend, clock, serviceResponse};

	DeviceConnection()
	{
		elapsed.start();
		const auto parsed = rt::RuntimeSchema::parse(own_device::bytes, indexes);
		if (!parsed)
			throw std::runtime_error("Descriptor parse failed");
		schema = parsed.schema;
		if (workbench.admit(schema, cl::Input{own_device::bytes}.first(64), 17) != cl::Status::Ok)
			throw std::runtime_error("Admission refused");
	}

	bool process()
	{
		workbench.process(static_cast<std::uint32_t>(elapsed.elapsed()));
		const auto packet = workbench.nextPacket();
		if (!packet)
			return true;
		if (packet.bytes.size() > port.size())
			return false;
		std::copy(packet.bytes.begin(), packet.bytes.end(), port.begin());
		if (!workbench.prepareHandoff(packet.session, packet.requestId) ||
		    !workbench.onSendAccepted(packet.session, packet.requestId))
			return false;
		// Only this local example performs synchronous native dispatch. A real
		// host hands its stable copied packet to its own asynchronous transport.
		const auto response =
		    en::dispatchEncoded(own_device::model.view(), workspace, packet.session,
		                        cl::Input{port}.first(packet.bytes.size()), reply);
		return response && workbench.onReply(cl::Input{reply}.first(response.written),
		                                     packet.session, packet.requestId);
	}
};

// Created before providers, so their callbacks detach before owner shutdown.
struct NativeCleanup {
	ux::DynamicConfiguratorBinding& binding;
	DeviceConnection& connection;

	~NativeCleanup()
	{
		binding.retire();
		connection.workbench.close();
		connection.workbench.process(static_cast<std::uint32_t>(connection.elapsed.elapsed()));
		binding.poll();
		if (!binding.drained())
			std::abort();
	}
};

static bool selectConfig(ux::DynamicConfiguratorProvider& provider, const QModelIndex& parent = {})
{
	auto* model = provider.endpoints();
	for (int i = 0; i < model->rowCount(parent); ++i) {
		const auto index = model->index(i, 0, parent);
		if (model->data(index, ux::EndpointModel::IsEndpointRole).toBool() &&
		    model->data(index, ux::EndpointModel::NameRole).toString() == QStringLiteral("Config"))
			return provider.selectEndpoint(index);
		if (selectConfig(provider, index))
			return true;
	}
	return false;
}

int main(int argc, char** argv)
{
	QGuiApplication application(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
		if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
			++warnings;
		std::fprintf(stderr, "%s\n", qPrintable(message));
	});
	try {
		auto connection = std::make_shared<DeviceConnection>();
		ux::DynamicConfiguratorBinding binding{connection->workbench, connection};
		NativeCleanup cleanup{binding, *connection};
		binding.setMutationsAllowed(true); // Host policy; each panel also requires user opt-in.
		ux::DynamicConfiguratorProvider configProvider{&binding}, valueProvider{&binding};
		if (!selectConfig(configProvider) || !selectConfig(valueProvider))
			return 2;
		auto engine = std::make_unique<QQmlApplicationEngine>();
		engine->rootContext()->setContextProperty(QStringLiteral("configProvider"),
		                                          &configProvider);
		engine->rootContext()->setContextProperty(QStringLiteral("valueProvider"), &valueProvider);
		engine->load(QUrl{QStringLiteral("qrc:/Main.qml")});
		if (engine->rootObjects().isEmpty())
			return 3;
		const bool smoke = application.arguments().contains(QStringLiteral("--smoke"));
		if (smoke && (!configProvider.refresh() || !valueProvider.refresh()))
			return 4;
		QTimer progress;
		QObject::connect(&progress, &QTimer::timeout, &application, [&] {
			if (!connection->process())
				application.exit(5);
			binding.poll(); // Notifications after native callbacks returned.
			if (smoke && !configProvider.busy() && !valueProvider.busy()) {
				const bool valid = own_device::device.reads == 2 &&
				                   configProvider.values()->rowCount() == 1 &&
				                   valueProvider.values()->rowCount() == 1;
				std::printf("Dynamic QML example panels=2 reads=%u valid=%u warnings=%u\n",
				            own_device::device.reads, valid ? 1U : 0U, warnings);
				application.exit(valid && !warnings ? 0 : 6);
			}
		});
		if (smoke)
			QTimer::singleShot(5000, &application, [&] {
				application.exit(7);
			});
		progress.start(5); // Host owns the only progress timer; providers add none.
		const auto result = application.exec();
		progress.stop();
		engine.reset();
		return warnings ? 8 : result;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
