"""Opt-in static Configurator binding over the same generated DeviceApi. MIT."""

from tools.telemetry_codegen.emitter_api import GeneratedTarget, cpp_string_literal
from . import qml, qml_macros


def _string(value):
    return f"QString::fromUtf8({cpp_string_literal(value)}, {len(value.encode('utf-8'))})"


def _access(group):
    return f"api->{group['symbol']}()"


def _binding(schema, groups):
    rows = [qml._prefix(schema, "CONFIGURATORBINDING", '\n'),
            '#include "DeviceApi.hpp"\n#include "QmlValues.hpp"\n',
            '#include <Telemetry/UX/StaticConfiguratorBinding.hpp>\n',
            '#include <memory>\n#include <utility>\n', qml_macros.push(schema),
            f'namespace {schema.namespace}::qml {{\n',
            qml_macros.qt_names('QObject', 'QByteArray', 'QVariant', 'QVariantMap', 'QString'),
            '// The host owns Api and its existing adapter. This binding only projects them.\n',
            'inline ::telemetry::ux::StaticConfiguratorBinding configuratorBinding(DeviceApi& api) {\n',
            '    using StaticConfiguratorBinding = ::telemetry::ux::StaticConfiguratorBinding;\n',
            '    using StaticType = ::telemetry::ux::StaticType;\n',
            '    using StaticTypeKind = ::telemetry::ux::StaticTypeKind;\n',
            '    using StaticEndpoint = ::telemetry::ux::StaticEndpoint;\n',
            '    StaticConfiguratorBinding binding;\n    binding.api = &api;\n',
            f'    binding.fingerprint = QString::fromLatin1("0x{schema.fingerprint:016x}");\n']
    for source in schema.types:
        rows.append('    { StaticType type;\n'
                    f'      type.id = {source["id"]}; type.kind = StaticTypeKind::{("Void", "Scalar", "Enum", "Struct", "Array")[source["kind"]]};\n')
        if source['kind'] == 1:
            rows.append(f'      type.scalarCode = {source["code"]};\n')
        elif source['kind'] == 2:
            rows.append(f'      type.underlyingTypeId = {source["underlying"]};\n')
            for code, name in source['entries']:
                rows.append(f'      type.entries.append({{{_string(name)}, {_string(str(code))}}});\n')
        elif source['kind'] == 3:
            for child, name in source['members']:
                rows.append(f'      type.members.append({{{_string(name)}, {child}}});\n')
        elif source['kind'] == 4:
            rows.append(f'      type.elementTypeId = {source["element"]}; type.count = {source["count"]}ULL;\n')
        rows.append('      binding.types.append(std::move(type)); }\n')
    for group in groups:
        for ep, stem in group['endpoints']:
            access = _access(group)
            request = ep.value_type if ep.category == 1 else ep.request_type
            rows.append('    { StaticEndpoint endpoint;\n'
                        f'      endpoint.category = {ep.category}; endpoint.id = {ep.id}u;\n'
                        f'      endpoint.catalog = {_string(ep.catalog_name)}; endpoint.name = {_string(ep.name)};\n'
                        f'      endpoint.valueType = {ep.value_type}; endpoint.requestType = {ep.request_type}; endpoint.responseType = {ep.response_type};\n'
                        f'      endpoint.readable = {str(bool(ep.capabilities & 1)).lower()}; endpoint.writable = {str(bool(ep.capabilities & 2)).lower()};\n')
            if request:
                rows.append('      endpoint.defaultRequest = +[]() -> QVariant {\n'
                            f'          auto value = std::make_unique<{qml._type(schema, request)}>();\n'
                            f'          return ::{schema.namespace}::qml::values::to_{request}(*value); }};\n'
                            '      endpoint.validate = +[](const QVariant& value, QString& error) {\n'
                            f'          auto output = std::make_unique<{qml._type(schema, request)}>();\n'
                            f'          return ::{schema.namespace}::qml::values::from_{request}(value, *output, error); }};\n')
            if ep.category == 1:
                rows.extend([
                    '      endpoint.cached = +[](QObject* object) -> QVariant { auto* api = ::qobject_cast<DeviceApi*>(object);\n'
                    f'          return api && {access} ? {access}->{stem}() : QVariant{{}}; }};\n',
                    '      endpoint.cachedValid = +[](QObject* object) { auto* api = ::qobject_cast<DeviceApi*>(object);\n'
                    f'          return api && {access} && {access}->{stem}Valid(); }};\n',
                    '      endpoint.cachedPayload = +[](QObject* object) -> QByteArray { auto* api = ::qobject_cast<DeviceApi*>(object);\n'
                    f'          return api && {access} ? {access}->{stem}CanonicalPayload() : QByteArray{{}}; }};\n'])
                if ep.capabilities & 1:
                    rows.append('      endpoint.read = +[](QObject* object) -> QString { auto* api = ::qobject_cast<DeviceApi*>(object);\n'
                                f'          return api && {access} ? {access}->refresh{qml._title(stem)}() : QString{{}}; }};\n')
                if ep.capabilities & 2:
                    rows.append('      endpoint.writePreserving = +[](QObject* object, const QVariant& value, const QByteArray& seed) -> QString { auto* api = ::qobject_cast<DeviceApi*>(object);\n'
                                f'          return api && {access} ? {access}->write{qml._title(stem)}Preserving(value, seed) : QString{{}}; }};\n')
            else:
                arg_name = 'value' if request else 'unused'
                argument = 'value' if request else ''
                rows.append(f'      endpoint.invoke = +[](QObject* object, const QVariant& {arg_name}) -> QString {{ auto* api = ::qobject_cast<DeviceApi*>(object);\n')
                if not request:
                    rows.append('          static_cast<void>(unused);\n')
                rows.append(f'          return api && {access} ? {access}->{stem}({argument}) : QString{{}}; }};\n')
            rows.append('      binding.endpoints.append(std::move(endpoint)); }\n')
    rows.extend([
        '    binding.ready = +[](QObject* object) { auto* api = ::qobject_cast<DeviceApi*>(object); return api && api->ready(); };\n',
        '    binding.session = +[](QObject* object) -> quint32 { auto* api = ::qobject_cast<DeviceApi*>(object); return api ? api->session() : 0; };\n',
        '    binding.connect = +[](QObject* object, QObject* context, std::function<void()> changed, std::function<void(QString, QVariantMap)> finished) {\n',
        '        auto* api = ::qobject_cast<DeviceApi*>(object); if (!api || !context) return;\n',
        '        QObject::connect(api, &DeviceApi::readyChanged, context, changed);\n',
        '        QObject::connect(api, &DeviceApi::sessionChanged, context, changed);\n',
        '        QObject::connect(api, &QObject::destroyed, context, changed);\n',
        '        QObject::connect(api, &DeviceApi::operationFinished, context, std::move(finished));\n'])
    for group in groups:
        for ep, stem in group['endpoints']:
            if ep.category == 1:
                rows.append(f'        if (api->{group["symbol"]}()) QObject::connect(api->{group["symbol"]}(), &{group["class_name"]}::{stem}Changed, context, changed);\n')
    rows.extend(['    };\n    return binding;\n}\n',
                 f'}} // namespace {schema.namespace}::qml\n', qml_macros.pop(schema), '#endif\n'])
    return ''.join(rows)


def generate(schema):
    """Generate the existing adapter once, with explicit preserving/cache hooks."""
    groups = qml.layout(schema, configurator=True)
    files = qml.generate(schema, configurator=True)
    files['qml/ConfiguratorBinding.hpp'] = _binding(schema, groups)
    files['qml/CMakeLists.txt'] += '''
# Only the opt-in Configurator starter needs the optional Telemetry UX provider.
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS static_configurator)
target_link_libraries(telemetry_qml_@TARGET@ PUBLIC TelemetryUX::static_configurator)
'''.replace('@TARGET@', schema.namespace.replace('::', '__'))
    return GeneratedTarget(files, qml.symbol_mapping(schema, configurator=True))
