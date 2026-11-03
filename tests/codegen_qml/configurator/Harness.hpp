#pragma once
// Opt-in generated bindings reuse the existing API and its single native FIFO. MIT.
#include <DemoModel.hpp>
#include <ConfiguratorBinding.hpp>
#include <DeviceClientAdapter.hpp>
#include <integration/encoded/Backend.hpp>
#include <integration/encoded/Dispatcher.hpp>
#include <QCoreApplication>
#include <QEvent>
#include <QJSEngine>
#include <QSignalSpy>
#include <array>
#include <bit>
#include <cstdio>
#include <stdexcept>

namespace static_qml_fixture {
namespace cf = codegen_fixture;
namespace ex = client_example;
namespace qa = device_api::qml;
namespace tc = telemetry::client;
namespace encoded = telemetry::integration::encoded;
namespace wire = telemetry::protocol::encoded;
namespace ux = telemetry::ux;
inline unsigned checks = 0;

inline void check(bool value, const char* message)
{
	++checks;
	if (!value)
		throw std::runtime_error(message);
}

inline void events()
{
	for (unsigned i = 0; i != 4; ++i)
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	QCoreApplication::processEvents();
}

inline QByteArray bytes(const ex::Config& value)
{
	std::array<std::byte, telemetry::wireSize<ex::Config>> output{};
	check(telemetry::encode(value, output) == telemetry::CodecStatus::Ok,
	      "native canonical encoding");
	return {reinterpret_cast<const char*>(output.data()), static_cast<qsizetype>(output.size())};
}

struct Harness {
	qa::DeviceApi api;
	std::uint32_t now = 0;
	std::array<std::byte, 4096> tx{}, result{}, copied{}, response{};
	tc::Clock clock{this, [](void* self) noexcept {
		                return static_cast<Harness*>(self)->now;
	                }};
	encoded::EncodedBackend backend{tx, result, clock};
	qa::DeviceClientAdapter<qa::DeviceApi, 3, 256> adapter{api, backend, clock};
	QSignalSpy finished{&api, &qa::DeviceApi::operationFinished};
	ux::StaticConfiguratorBinding binding = qa::configuratorBinding(api);
	QByteArray lastPayload;
	unsigned packets = 0, changed = 0, delivered = 0;

	Harness()
	{
		binding.connect(
		    &api, &api,
		    [this] {
			    ++changed;
		    },
		    [this](QString, QVariantMap) {
			    ++delivered;
		    });
		check(api.admitDescriptor({reinterpret_cast<const char*>(cf::descriptorBytes.data()), 64}),
		      "admit exact native schema");
		events();
	}

	~Harness()
	{
		QObject::disconnect(&api, nullptr, &api, nullptr);
		adapter.close();
		for (unsigned i = 0; i != 8 && !adapter.drained(); ++i)
			adapter.process(now++);
	}

	const ux::StaticEndpoint& endpoint(int category, const QString& name) const
	{
		for (const auto& endpoint : binding.endpoints)
			if (endpoint.category == category && endpoint.name == name)
				return endpoint;
		throw std::runtime_error("missing bound endpoint");
	}

	void exchange(bool deliver = true)
	{
		events();
		adapter.process(now);
		const auto pending = adapter.nextPacket();
		if (!pending)
			return;
		check(pending.bytes.size() <= copied.size(), "bounded native frame");
		std::copy(pending.bytes.begin(), pending.bytes.end(), copied.begin());
		const auto frame = tc::Input{copied}.first(pending.bytes.size());
		const auto parsed = wire::parse(frame);
		check(bool(parsed), "native request envelope");
		lastPayload = QByteArray(reinterpret_cast<const char*>(parsed.payload.data()),
		                         static_cast<qsizetype>(parsed.payload.size()));
		check(adapter.prepareHandoff(pending.session, pending.requestId), "owner handoff gate");
		check(adapter.onSendAccepted(pending.session, pending.requestId), "single accepted send");
		const auto reply = encoded::dispatchEncoded(cf::model.view(), cf::workspace, api.session(),
		                                            frame, response);
		check(bool(reply), "real native model dispatch");
		++packets;
		check(adapter.onReply(tc::Input{response}.first(reply.written), pending.session,
		                      pending.requestId),
		      "correlated native reply");
		adapter.process(now);
		if (deliver)
			events();
	}

	QVariantMap await(const QString& token)
	{
		check(!token.isEmpty(), "nonempty generated request token");
		for (unsigned i = 0; i != 50; ++i) {
			for (qsizetype index = 0; index < finished.size(); ++index) {
				if (finished[index][0].toString() == token)
					return finished.takeAt(index)[1].toMap();
			}
			exchange();
		}
		throw std::runtime_error("generated request did not complete");
	}
};

} // namespace static_qml_fixture
