"""Optional Telemetry UX generators; consumes only the public SDK emitter seam.

Qt is required to compile generated QML C++, not to run this Python emitter.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from tools.telemetry_codegen.emitter_api import GeneratedTarget

API_VERSION = 1
IR_VERSION = 1
SDK_VERSION = '0.1.0'
__version__ = '0.1.0'


def generate_qml(schema):
    """One emission supplies source files and the legacy QML symbol receipt."""
    from . import qml
    return GeneratedTarget(qml.generate(schema), qml.symbol_mapping(schema))
