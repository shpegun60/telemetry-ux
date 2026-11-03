// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    id: host
    required property QtObject deviceProvider
    visible: true
    width: 1024
    height: 768

    Configurator {
        objectName: "ownedProviderView"
        anchors.fill: parent
        provider: host.deviceProvider
    }
}
