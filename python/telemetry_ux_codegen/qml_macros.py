"""Isolate Qt macros around unchanged standalone C++ SDK declarations.

Wire names may be legal C++ identifiers such as Q_OBJECT or signals. The Qt
facade shields only its own SDK inclusion/access region and restores consumers'
macro state; it does not rename the shared SDK or change Qt's public keywords.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

import re

ALIASES = frozenset(("qApp", "qGuiApp", "qAppName"))


def names(schema):
    candidates = {"signals", "slots", "emit", "foreach", "forever"} | set(ALIASES)
    symbols = set(schema.namespace.split("::"))
    for row in (*schema.member_names, *schema.enum_names):
        symbols.update(row)
    for endpoint in schema.endpoints:
        symbols.update((endpoint.group, endpoint.symbol, endpoint.value_alias,
                        endpoint.request_alias, endpoint.response_alias))
    # This function-like macro is used by QStringLiteral on tested Qt 6.4/6.10.
    # DTO identifier references never put '(' immediately after their names,
    # so keeping a function-like macro here cannot rewrite an SDK declaration.
    candidates.update(name for name in symbols if name.startswith(("Q_", "QT_", "QML_"))
                      and name != "QT_UNICODE_LITERAL")
    return tuple(sorted(candidates))


def push(schema):
    return ("// Preserve Qt's macro state while parsing/accessing standalone DTO names.\n" +
            "".join(f'#pragma push_macro("{name}")\n#undef {name}\n' for name in names(schema)))


def pop(schema):
    return "".join(f'#pragma pop_macro("{name}")\n' for name in reversed(names(schema)))


def qt_names(*types):
    """Imported Qt globals outrank a same-named catalog in the parent namespace."""
    aliases = ("qint8", "quint8", "qint16", "quint16", "qint32", "quint32", "qint64", "quint64",
               "qsizetype", "qlonglong", "qulonglong", "ushort")
    return ("namespace Qt = ::Qt;\nnamespace QtPrivate = ::QtPrivate;\n" +
            "".join("using ::" + name + ";\n" for name in sorted(set(types) | set(aliases))))


def validate_namespace(schema):
    # An explicitly selected namespace also appears in Qt's generated moc and
    # registration code, outside the shield. Descriptor member/group/enum names
    # are unaffected by this requested-host-namespace restriction.
    if any(re.match(r"Q(?:[A-Z_]|t)", part) or part in ALIASES or
           part in ("signals", "slots", "emit", "foreach", "forever", "qint8", "quint8",
                    "qint16", "quint16", "qint32", "quint32", "qint64", "quint64", "qint128",
                    "quint128", "qreal", "qfloat16", "qsizetype", "qptrdiff", "qintptr",
                    "quintptr", "qlonglong", "qulonglong")
           for part in schema.namespace.split("::")):
        raise ValueError("QML namespace conflicts with Qt names; choose a namespace such as device_api")
