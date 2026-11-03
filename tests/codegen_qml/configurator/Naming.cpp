// Generated binding compiles with real Qt macros and hostile catalog identifiers. MIT.
#include <ConfiguratorBinding.hpp>
#include <QCoreApplication>
#include <cstdio>
#include <stdexcept>

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	naming_api::qml::DeviceApi api;
	const auto binding = naming_api::qml::configuratorBinding(api);
	if (binding.endpoints.size() != 36 || binding.types.size() != 12 || binding.api != &api)
		throw std::runtime_error("hostile binding metadata does not match the fixture");
	for (const auto& endpoint : binding.endpoints) {
		if (!endpoint.read || !endpoint.writePreserving || !endpoint.validate ||
		    !endpoint.defaultRequest || endpoint.category != 1 || !endpoint.readable ||
		    !endpoint.writable)
			throw std::runtime_error("hostile binding lost a Field callback");
		QString error;
		if (!endpoint.validate(endpoint.defaultRequest(), error))
			throw std::runtime_error("default canonical request failed the generated converter");
	}
	std::printf("Static QML naming endpoints=%lld\n",
	            static_cast<long long>(binding.endpoints.size()));
}
