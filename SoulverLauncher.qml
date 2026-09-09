import QtQuick
import Quickshell
import qs.Services
import "."

QtObject {
    id: root

    property var pluginService: null
    property string pluginId: "dankSoulverCore"
    property string trigger: "="

    property string currentQuery: ""
    property var currentResult: null
    property string pendingQuery: ""
    property int requestSerial: 0
    property int activeRequest: 0
    property int minimumCharacters: 3
    property int debounceMilliseconds: 90
    property string helperPath: ""
    property string resourcesPath: ""

    signal itemsChanged()

    property SoulverService soulver: SoulverService {
        executable: root.helperPath
        resourcesPath: root.resourcesPath
        enabled: root.helperPath.length > 0

        onResultReady: (requestId, expression, result) => {
            if (requestId !== root.activeRequest || expression !== root.pendingQuery)
                return;

            root.currentQuery = expression;
            root.currentResult = root._isDisplayable(result) ? result : null;
            root._requestLauncherRefresh();
        }

        onAvailabilityChanged: root._requestLauncherRefresh()
    }

    property Timer queryDebounce: Timer {
        interval: root.debounceMilliseconds
        repeat: false
        onTriggered: root._evaluatePendingQuery()
    }

    Component.onCompleted: {
        if (pluginService) {
            // DMS reads this setting before asking the provider for items. Persisting the
            // default makes the first install behave as an automatic launcher provider.
            const automatic = pluginService.loadPluginData(pluginId, "noTrigger", true);
            pluginService.savePluginData(pluginId, "noTrigger", automatic);
        }
        _loadSettings();
    }

    property Connections pluginSettingsConnection: Connections {
        target: root.pluginService

        function onPluginDataChanged(changedPluginId) {
            if (changedPluginId === root.pluginId)
                root._loadSettings();
        }
    }

    function _loadSettings() {
        if (!pluginService)
            return;

        const noTrigger = pluginService.loadPluginData(pluginId, "noTrigger", true);
        const configuredTrigger = pluginService.loadPluginData(pluginId, "trigger", "=");
        trigger = noTrigger ? "" : (configuredTrigger || "=");
        const configuredHelper = pluginService.loadPluginData(pluginId, "helperPath", "");
        const pluginPath = pluginService.getPluginPath(pluginId);
        helperPath = configuredHelper || (pluginPath
            ? pluginPath + "/run-helper"
            : "dank-soulver-core");
        resourcesPath = pluginService.loadPluginData(pluginId, "resourcesPath", "");

        const configuredMinimum = parseInt(pluginService.loadPluginData(pluginId, "minimumCharacters", "3"));
        minimumCharacters = Number.isFinite(configuredMinimum) ? Math.max(1, configuredMinimum) : 3;

        const configuredDebounce = parseInt(pluginService.loadPluginData(pluginId, "debounceMilliseconds", "90"));
        debounceMilliseconds = Number.isFinite(configuredDebounce)
            ? Math.max(0, configuredDebounce)
            : 90;

    }

    function getItems(query) {
        const expression = String(query || "").trim();

        if (!expression || (trigger === "" && expression.length < minimumCharacters)) {
            _clearQuery();
            return [];
        }

        if (expression !== pendingQuery) {
            pendingQuery = expression;
            currentQuery = "";
            currentResult = null;
            queryDebounce.restart();
        }

        if (currentQuery === expression)
            return currentResult ? [_toLauncherItem(expression, currentResult)] : [];

        if (trigger !== "" && soulver.errorMessage) {
            return [{
                id: _itemId(expression),
                name: "SoulverCore is unavailable",
                icon: "material:error_outline",
                comment: soulver.errorMessage,
                action: "none",
                categories: ["Soulver"]
            }];
        }

        if (_isLikelyCalculation(expression))
            return [_loadingItem(expression)];

        return [];
    }

    function _evaluatePendingQuery() {
        if (!pendingQuery)
            return;

        activeRequest = ++requestSerial;
        soulver.calculate(activeRequest, pendingQuery);
    }

    function _clearQuery() {
        queryDebounce.stop();
        pendingQuery = "";
        currentQuery = "";
        currentResult = null;
        activeRequest = ++requestSerial;
    }

    function _isDisplayable(result) {
        if (!result || result.error || !result.value)
            return false;

        const type = String(result.type || "").toLowerCase();
        if (["none", "empty", "error", "failed", "pending", "text"].indexOf(type) !== -1)
            return false;

        return String(result.value).trim().length > 0;
    }

    function _toLauncherItem(expression, result) {
        const type = result.type || "Result";
        return {
            id: _itemId(expression),
            name: result.value,
            icon: _iconForResult(type, expression, result.value),
            comment: type,
            action: "copy",
            copyText: result.value,
            expression: expression,
            resultType: type,
            categories: ["Soulver"]
        };
    }

    function _loadingItem(expression) {
        return {
            id: _itemId(expression),
            name: expression,
            icon: "material:calculate",
            comment: "Calculating…",
            action: "none",
            categories: ["Soulver"]
        };
    }

    function _itemId(expression) {
        return "dankSoulverCore:" + expression;
    }

    function _isLikelyCalculation(expression) {
        if (trigger !== "")
            return true;

        const text = String(expression || "").toLowerCase();
        const hasDigit = /\d/.test(text);
        const hasOperator = /[+\-*\/^%=]/.test(text)
            || /\b(to|in|of|plus|minus|times|multiplied|divided|over|percent|mod|after|before|from|ago)\b/.test(text);
        const hasNumberWord = /\b(one|two|three|four|five|six|seven|eight|nine|ten|hundred|thousand|million)\b/.test(text);
        const hasTimeQuestion = /\b(time in|next (monday|tuesday|wednesday|thursday|friday|saturday|sunday)|today|tomorrow|yesterday)\b/.test(text);
        return (hasDigit && hasOperator) || (hasNumberWord && hasOperator) || hasTimeQuestion;
    }

    function _iconForResult(typeName, expression, value) {
        const type = String(typeName || "").toLowerCase();
        const answer = String(value || "");
        if (type.indexOf("currency") !== -1 || type.indexOf("rate") !== -1
                || /(dollar|euro|pound|yen|yuan|franc|rupee|won|bitcoin|ethereum)/.test(type)
                || /[$€£¥₩₹]/.test(answer))
            return "material:currency_exchange";
        if (type.indexOf("date") !== -1 || type.indexOf("time") !== -1
                || /(second|minute|hour|day|week|month|year)/.test(type))
            return "material:schedule";
        if (type.indexOf("unit") !== -1 || type.indexOf("distance") !== -1 || type.indexOf("mass") !== -1)
            return "material:straighten";
        if (type.indexOf("percentage") !== -1)
            return "material:percent";
        return "material:calculate";
    }

    function executeItem(item) {
        if (!item || item.action === "none")
            return;

        if (item.action === "copy" && item.copyText !== undefined) {
            Quickshell.execDetached(["dms", "cl", "copy", String(item.copyText)]);
            if (typeof ToastService !== "undefined")
                ToastService.showInfo("Soulver", "Copied " + item.copyText);
        }
    }

    function _requestLauncherRefresh() {
        itemsChanged();
        if (pluginService && typeof pluginService.requestLauncherUpdate === "function")
            pluginService.requestLauncherUpdate(pluginId);
    }
}
