"""Package the optional Telemetry UX generator without a second CLI.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from pathlib import Path

from setuptools import find_packages, setup

ROOT = Path(__file__).resolve().parent
VERSION = (ROOT / "VERSION").read_text(encoding="ascii").strip()

setup(
    name="telemetry-ux-codegen",
    version=VERSION,
    description="Optional QML and browser starters for the Telemetry code generator",
    long_description=(ROOT / "README.md").read_text(encoding="utf8"),
    long_description_content_type="text/markdown",
    python_requires=">=3.10",
    license="MIT",
    license_files=("LICENSE",),
    package_dir={"": "python"},
    packages=find_packages("python"),
    install_requires=["telemetry-codegen-core==" + VERSION],
)
