"""QML naming/ownership and unchanged standalone SDK byte regressions.

This suite checks publication and identities independently of Qt availability;
the adjacent CMake/runtime suite executes the generated module in Qt itself.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

import hashlib
import json
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

import descriptor_fixture as golden
from descriptor_fixture import descriptor
from tools.telemetry_codegen import load_schema as installed_load_schema
from telemetry_ux_codegen import qml

_inputs = {}
_outputs = {}


def load_schema(data, namespace="device_api"):
    schema = installed_load_schema(data, namespace)
    _inputs[id(schema)] = data
    return schema


def generate(data, namespace, targets, output, check=False):
    """Exercise the installed CLI from an unrelated working directory."""
    with tempfile.TemporaryDirectory() as temporary:
        cwd = Path(temporary)
        source = cwd / 'descriptor.bin'
        source.write_bytes(data)
        command = [sys.executable, '-B', '-m', 'tools.telemetry_codegen', 'generate',
                   str(source), '--namespace', namespace, '--targets', ','.join(targets),
                   '--output', str(output)]
        if check:
            command.append('--check')
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1')
        env.pop('PYTHONPATH', None)
        result = subprocess.run(command, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=60)
        if result.returncode:
            raise ValueError(result.stdout)


def render(schema, targets):
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / 'sdk'
        generate(_inputs[id(schema)], schema.namespace, targets, output)
        files = {path.relative_to(output).as_posix(): path.read_bytes()
                 for path in output.rglob('*') if path.is_file()}
    _outputs[id(files)] = (_inputs[id(schema)], schema.namespace, targets)
    return files


def publish(files, output, check=False):
    data, namespace, targets = _outputs[id(files)]
    generate(data, namespace, targets, output, check)


class QmlGeneratorTests(unittest.TestCase):
    def test_existing_sdk_is_byte_identical(self):
        schema = load_schema(golden.fixture("mixed"))
        old = render(schema, ("cpp", "js", "ts"))
        combined = render(schema, ("cpp", "js", "ts", "qml"))
        for path, source in old.items():
            if path != "manifest.json":
                self.assertEqual(source, combined[path], path)
        only = render(schema, ("qml",))
        self.assertEqual({p for p in only if p.startswith("cpp/")},
                         {p for p in old if p.startswith("cpp/")})
        for path in only:
            if path.startswith("cpp/"):
                self.assertEqual(only[path], old[path])
        manifest = json.loads(only["manifest.json"])
        self.assertEqual(manifest["options"]["targets"], ["qml"])
        for path, digest in manifest["generated_files"].items():
            self.assertEqual(digest, hashlib.sha256(only[path]).hexdigest())
        self.assertIn("qml", manifest["generated_symbol_mapping"])

    def test_full_scope_names_and_nesting(self):
        names = ["Voltage", "voltage", "voltageChanged", "refreshVoltage", "m_device",
                 "objectName", "signals", "slots", "property", "μ", "__proto__", "1x"]
        groups = ((("device", [(2, name, 3) for name in names]),
                   ("m_pending", [(2, "ready")]), ("pending", [(2, "session")]),
                   ("setTransportHandoffTracking", [(2, "transportHandoffAccepted")]),
                   ("transportHandoffAccepted", [(2, "setTransportHandoffTracking")]),
                   ("trackHandoff", [(2, "m_trackHandoff")])),
                  (("device", [(12, "Voltage"), (0, "resetSession")]),),
                  (("device", [(12, "Voltage", 12), (0, "refreshVoltage", 0)]),))
        schema = load_schema(descriptor([golden.struct_type(12, 0, [])], groups), "company::device_api")
        mapped = qml.layout(schema)
        self.assertEqual(len({g["symbol"] for g in mapped}), len(mapped))
        occupied = set(qml.GROUP_RESERVED)
        for group in mapped:
            self.assertFalse({group["symbol"], "m_" + group["symbol"]} & occupied)
            occupied.update((group["symbol"], "m_" + group["symbol"]))
            family = set(qml.RESERVED)
            for ep, stem in group["endpoints"]:
                names = qml._family(stem, ep.category)
                self.assertFalse(names & family, (stem, names & family))
                family.update(names)
        files = render(schema, ("qml",))
        self.assertIn(b"namespace company::device_api::qml", files["qml/DeviceApi.hpp"])
        self.assertIn(b"URI Telemetry.Generated.company.device_api", files["qml/CMakeLists.txt"])
        mapping = json.loads(files["manifest.json"])["generated_symbol_mapping"]["qml"]
        self.assertEqual(mapping["groups"][0]["endpoints"][0]["original_name"], "Voltage")

    def test_empty_schema_and_case_distinct_guards(self):
        data = golden.fixture("empty")
        first = render(load_schema(data, "lower"), ("qml",))
        second = render(load_schema(data, "Lower"), ("qml",))
        for path in ("qml/DeviceApi.hpp", "qml/QmlValues.hpp", "qml/QmlInput.hpp"):
            guards = [next(line for line in files[path].splitlines() if line.startswith(b"#ifndef"))
                      for files in (first, second)]
            self.assertNotEqual(*guards, path)
        self.assertNotIn(b"Group_0Api", first["qml/DeviceApi.hpp"])

    def test_guarded_publish_and_target_removal(self):
        schema = load_schema(golden.fixture("mixed"))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sdk"
            publish(render(schema, ("qml",)), path)
            publish(render(schema, ("qml",)), path, check=True)
            (path / "application.txt").write_text("owned by application")
            publish(render(schema, ("cpp",)), path)
            self.assertFalse((path / "qml/DeviceApi.hpp").exists())
            self.assertEqual((path / "application.txt").read_text(), "owned by application")
            publish(render(schema, ("qml",)), path)
            changed = path / "qml/DeviceApi.hpp"
            changed.write_bytes(changed.read_bytes() + b"// local edit\n")
            with self.assertRaisesRegex(ValueError, "modified generated"):
                publish(render(schema, ("qml",)), path)

    def test_qt_specific_namespace_profile(self):
        data = golden.fixture("empty")
        for name in ("QObject", "company::QString", "Q_OBJECT", "QML_ELEMENT", "qint64", "qApp"):
            schema = load_schema(data, name)
            self.assertIn("manifest.json", render(schema, ("cpp",)))
            with self.assertRaisesRegex(ValueError, "QML namespace conflicts"):
                render(schema, ("qml",))

    def test_group_cannot_hide_qobject_thread(self):
        schema = load_schema(descriptor(groups=((("thread", [(2, "sample")]),), (), ())))
        group = qml.layout(schema)[0]
        self.assertEqual(group["name"], "thread")
        self.assertEqual(group["symbol"], "thread_2")
        self.assertNotIn(b"Group_0Api* thread()", render(schema, ("qml",))["qml/DeviceApi.hpp"])

    def test_qobject_macro_state_cannot_become_property_getters(self):
        names = ("qt_staticMetaObjectContent", "qt_staticMetaObjectStaticContent",
                 "qt_staticMetaObjectRelocatingContent")
        schema = load_schema(descriptor(groups=(tuple((name, [(2, name)]) for name in names), (), ())))
        for group, name in zip(qml.layout(schema), names):
            self.assertEqual(group["symbol"], name + "_2")
            self.assertEqual(group["endpoints"][0][1], name + "_2")

    def test_anonymous_marker_cannot_become_zero_argument_command(self):
        schema = load_schema(descriptor(groups=((), (("device", [(0, "qt_qmlMarker_anonymous")]),), ())))
        self.assertEqual(qml.layout(schema)[0]["endpoints"][0][1], "qt_qmlMarker_anonymous_2")

    def test_qobject_virtual_hooks_cannot_be_hidden(self):
        names = ("event", "eventFilter", "timerEvent", "childEvent", "customEvent",
                 "connectNotify", "disconnectNotify")
        groups = (tuple((name, [(2, name)]) for name in names),
                  (("commands", [(0, name) for name in names]),), ())
        mapped = qml.layout(load_schema(descriptor(groups=groups)))
        for group, name in zip(mapped, names):
            self.assertEqual(group["symbol"], name + "_2")
            self.assertEqual(group["endpoints"][0][1], name + "_2")
        self.assertEqual([stem for _, stem in mapped[-1]["endpoints"]],
                         [name + "_2" for name in names])


if __name__ == "__main__":
    unittest.main()
