// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import Telemetry.Generated.device_api

Item {
    width: 64
    height: 64
    property alias api: apiObject
    property bool cacheValid: apiObject.device.configValid
    property var cache: apiObject.device.config
    property int changes: 0
    DeviceApi { id: apiObject }
    Connections {
        target: apiObject.device
        function onConfigChanged() { ++changes }
    }
    function refresh() { return apiObject.device.refreshConfig() }
    function configValue() {
        return {serial: "18446744073709551615", offset: "-9223372036854775808",
                gain: -0, precise: 0.125, enabled: true, mode: -2,
                samples: [1, 256, 65535]}
    }
    function write() { return apiObject.device.writeConfig(configValue()) }
    function inspect(multiplier) {
        return apiObject.device.inspect({config: configValue(), multiplier: multiplier})
    }
    function badWide() {
        let v = configValue(); v.serial = 18446744073709551615;
        return apiObject.device.writeConfig(v)
    }
    function throwingGetter() {
        let v = configValue(); Object.defineProperty(v, "serial", {get: function() {throw 42}});
        return apiObject.device.writeConfig(v)
    }
    function typedArray() {
        let v = configValue(); v.samples = new Uint16Array([1,256,65535]);
        return apiObject.device.writeConfig(v)
    }
    function sparseArray() {
        let v = configValue(); delete v.samples[1];
        return apiObject.device.writeConfig(v)
    }
    function cycle() {
        let v = configValue(); v.extra = v;
        return apiObject.device.writeConfig(v)
    }
    function getterOnce() {
        let v = configValue(); let reads = 0;
        Object.defineProperty(v, "serial", {get: function() {
            return ++reads === 1 ? "18446744073709551615" : "18446744073709551616"
        }});
        return apiObject.device.writeConfig(v)
    }
    function getterReset() {
        let v = configValue();
        Object.defineProperty(v, "serial", {get: function() {
            apiObject.resetSession(); return "18446744073709551615"
        }});
        return apiObject.device.writeConfig(v)
    }
    function wideExact() { return cache.serial === "18446744073709551615" }
    function negativeZero() { return Object.is(cache.gain, -0) }
}
