import QtQuick
import Quickshell
import Quickshell.Io

QtObject {
    id: root

    property string executable: "dank-soulver-core"
    property string resourcesPath: ""
    property bool enabled: false
    property bool ready: false
    property string errorMessage: ""
    property var queuedRequest: null
    property int restartAttempts: 0

    signal resultReady(int requestId, string expression, var result)
    signal availabilityChanged()

    function start() {
        if (!enabled || !executable)
            return;
        if (!soulverProcess.running)
            soulverProcess.running = true;
    }

    function restart() {
        ready = false;
        errorMessage = "";
        restartAttempts = 0;
        if (soulverProcess.running) {
            soulverProcess.running = false;
            restartTimer.restart();
        } else {
            start();
        }
    }

    function stop() {
        restartTimer.stop();
        queuedRequest = null;
        ready = false;
        soulverProcess.running = false;
    }

    function calculate(requestId, expression) {
        queuedRequest = {
            id: requestId,
            expression: expression
        };

        if (!soulverProcess.running) {
            start();
            return;
        }

        _writeQueuedRequest();
    }

    function _writeQueuedRequest() {
        if (!soulverProcess.running || !queuedRequest)
            return;

        soulverProcess.write(JSON.stringify(queuedRequest) + "\n");
        queuedRequest = null;
    }

    function _handleStdout(line) {
        const text = String(line || "").trim();
        if (!text)
            return;

        let payload;
        try {
            payload = JSON.parse(text);
        } catch (error) {
            console.warn("DankSoulverCore: invalid helper response:", text);
            return;
        }

        if (payload.event === "ready") {
            ready = true;
            errorMessage = "";
            restartAttempts = 0;
            availabilityChanged();
            _writeQueuedRequest();
            return;
        }

        if (payload.event === "error") {
            ready = false;
            errorMessage = payload.error || "SoulverCore failed to start";
            availabilityChanged();
            return;
        }

        resultReady(Number(payload.id || 0), String(payload.expression || ""), payload);
    }

    onEnabledChanged: {
        if (enabled)
            start();
        else
            stop();
    }

    onExecutableChanged: {
        if (enabled)
            restart();
    }

    onResourcesPathChanged: {
        if (enabled)
            restart();
    }

    property Process soulverProcess: Process {
        command: {
            const args = [root.executable, "--server"];
            if (root.resourcesPath)
                args.push("--resources", root.resourcesPath);
            return args;
        }
        stdinEnabled: true

        stdout: SplitParser {
            onRead: data => root._handleStdout(data)
        }

        stderr: SplitParser {
            onRead: data => {
                const message = String(data || "").trim();
                if (message)
                    console.warn("DankSoulverCore helper:", message);
            }
        }

        onRunningChanged: {
            if (running) {
                root.errorMessage = "";
                root._writeQueuedRequest();
                return;
            }

            if (!root.enabled)
                return;

            root.ready = false;
            root.restartAttempts += 1;
            root.errorMessage = root.restartAttempts > 3
                ? "Could not start dank-soulver-core"
                : "SoulverCore helper stopped";
            root.availabilityChanged();

            if (root.restartAttempts <= 3) {
                restartTimer.interval = root.restartAttempts * 500;
                restartTimer.restart();
            }
        }
    }

    property Timer restartTimer: Timer {
        repeat: false
        onTriggered: root.start()
    }
}

