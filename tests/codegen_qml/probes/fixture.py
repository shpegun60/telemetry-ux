"""Independent explicit descriptor bytes for strict Qt value/snapshot probes.

No native producer, QML facade, transport, or hardware receipt is involved.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import descriptor_fixture as golden
from descriptor_fixture import descriptor


def fixture():
    """Keep original scalar IDs; include enum64, empty/zero and nested shapes."""
    scalars = ("flag", "u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "signals", "slots")
    extra = (
        golden.struct_type(12, 43, list(enumerate(scalars, 1))),
        golden.enum_type(13, 8, 8, [(0, "Zero"), ((1 << 64) - 1, "Max")]),
        golden.enum_type(14, 9, 8, [(-(1 << 63), "Min"), ((1 << 63) - 1, "Max")]),
        golden.type_record(15, 4, 86, golden.u32(12, 2)),
        golden.struct_type(16, 0, []),
        golden.type_record(17, 4, 0, golden.u32(16, 0)),
        golden.struct_type(18, 145, [(12, "__proto__"), (13, "unsignedEnum"), (14, "signedEnum"),
                                    (15, "samples"), (16, "empty"), (17, "zero")]),
    )
    data = descriptor(extra, ((("motor", [(18, "All", 3)]),), (), ()))
    return data


def write(output, generate):
    """Write only to a caller-selected external build directory."""
    output = Path(output).resolve()
    if output == ROOT or ROOT in output.parents:
        raise ValueError("probe output must be outside the repository")
    return generate(fixture(), "sdk::device", output)


def write_modules(output, generate):
    """Compile the complete registration facade for empty and hostile schemas."""
    output = Path(output).resolve()
    if output == ROOT or ROOT in output.parents:
        raise ValueError("probe module output must be outside the repository")
    names = ("config", "configValid", "configChanged", "refreshConfig", "writeConfig", "m_device",
             "signals", "slots", "emit", "foreach", "__proto__", "\u03bc", "a/b", "a b", "a_b_2",
             "Q_OBJECT", "Q_GADGET", "QT_VERSION", "Q_SIGNALS", "Q_SLOTS", "Q_INVOKABLE",
             "QML_ELEMENT", "Q_ASSERT", "Q_EMIT", "QT_UNICODE_LITERAL", "qApp", "qGuiApp", "qAppName",
             "input", "values", "byteView", "byteOutput", "resultMap", "thread", "sender",
             "senderSignalIndex", "event", "eventFilter", "timerEvent", "childEvent", "customEvent",
             "connectNotify", "disconnectNotify", "qt_static_metacall", "d_func", "d_ptr",
             "requestActive", "snapshotInput", "commitTransportHandoff", "transportHandoffAccepted",
             "operationFinished", "invalidate")
    meta_state = ("qt_staticMetaObjectContent", "qt_staticMetaObjectStaticContent",
                  "qt_staticMetaObjectRelocatingContent", "qt_create_metaobjectdata", "qt_qmlMarker_anonymous")
    names += meta_state
    groups = ("pending", "timer", "inputEngine", "m_pending", "device", "m_device", "ready", "thread",
              "deleteLater", "Type_12", "\u03bc", "a/b", "a b", "QString", "QObject", "QVariant",
              "QMetaObject", "Qt", "QtPrivate", "qjsEngine", "qMetaTypeId", "QLatin1Char", "QThread",
              "QQmlEngine", "QPointer", "QDeadlineTimer", "QJSEngine", "QBitArray", "QJSManagedValue",
              "QJSValue", "QJSValueIterator", "QMetaType", "QStringList", "QVariantMap", "QVariantList",
              "QList", "input", "values", "byteView", "byteOutput", "resultMap", "qApp", "qGuiApp",
              "qAppName", "Q_OBJECT", "Q_GADGET", "QT_VERSION", "Q_SIGNALS", "Q_SLOTS", "Q_INVOKABLE",
              "QML_ELEMENT", "Q_ASSERT", "sender", "senderSignalIndex", "property", "connect",
              "disconnect", "parent", "children", "event", "eventFilter", "timerEvent", "childEvent",
              "customEvent", "connectNotify", "disconnectNotify", "qt_static_metacall", "d_func", "d_ptr",
              "requestActive", "snapshotInput", "commitTransportHandoff", "transportHandoffAccepted",
              "operationFinished", "invalidate", "trackHandoff", "nextToken", "session", "timeout")
    groups += meta_state
    # A single-byte struct request is legal for Command/Service. All original
    # member spellings survive in DTOs, including Qt's optional keyword macros.
    # Enum labels are also real identifiers accessed inside converter shields.
    # All aliases are valid/open representable byte values, including Qt macros.
    macro_names = names[15:28]
    extra = (golden.struct_type(12, len(names), [(2, name) for name in names]),
             golden.enum_type(13, 2, 1, list(enumerate(macro_names))),
             golden.struct_type(14, len(names) + 1, [(12, "config"), (13, "enumValue")]))
    catalogs = [[], [], []]
    for group_index, group in enumerate(groups):
        endpoints = names if group_index == 0 else ("config",)
        catalogs[0].append((group, [(14, name, 3) for name in endpoints]))
        catalogs[1].append((group, [(12, name) for name in endpoints]))
        catalogs[2].append((group, [(12, name, 14) for name in endpoints]))
    # Empty catalogs still generate real groups and can also collide by name.
    for category in catalogs:
        category.extend((("empty group", []), ("empty/group", [])))
    # Real zero-argument methods also exercise Q_OBJECT/QML_ANONYMOUS helpers
    # and inherited hooks; parameterized command methods cannot expose that clash.
    catalogs[1].append(("zero argument hooks", [(0, name) for name in names]))
    variants = (("empty", descriptor()),
                ("hostile", descriptor(extra, tuple(tuple(rows) for rows in catalogs))))
    result = []
    for name, data in variants:
        folder = output / name
        namespace = "variant::" + name
        manifest = generate(data, namespace, folder)
        result.append(dict(name=name, namespace=namespace, directory=str(folder),
                           descriptor_sha256=manifest['descriptor_sha256'],
                           fingerprint=manifest['descriptor_fingerprint'],
                           endpoints=len(manifest['generated_symbol_mapping']['endpoints']),
                           qml_symbols=manifest['generated_symbol_mapping']['qml']))
    return result
