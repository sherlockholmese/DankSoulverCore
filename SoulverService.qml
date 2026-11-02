import QtQuick
import Quickshell
import Quickshell.Io

QtObject {
    id: root

    property string executable: "dank-soulver-core"
    property string backend: "soulver"
    property string resourcesPath: ""
    property bool enabled: false
    property bool ready: false
    property string errorMessage: ""
    property var queuedRequest: null
    property int restartAttempts: 0
    // The helper's own explanation outlives its exit; "helper stopped" would only hide it.
    property string startupError: ""
    property bool restarting: false

    signal resultReady(int requestId, string expression, var result)
    signal availabilityChanged()

    function start() {
        if (!enabled || !executable || restarting)
            return;
        if (!soulverProcess.running)
            soulverProcess.running = true;
    }

    function restart() {
        ready = false;
        errorMessage = "";
        startupError = "";
        restartAttempts = 0;
        restartTimer.stop();
        availabilityChanged();
        if (soulverProcess.running) {
            restarting = true;
            soulverProcess.running = false;
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
        if (!soulverProcess.running || restarting || !queuedRequest)
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
            startupError = "";
            restartAttempts = 0;
            availabilityChanged();
            _writeQueuedRequest();
            return;
        }

        if (payload.event === "error") {
            ready = false;
            startupError = payload.error || "The helper failed to start";
            errorMessage = startupError;
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

    onBackendChanged: {
        if (enabled)
            restart();
    }

    onResourcesPathChanged: {
        if (enabled)
            restart();
    }

    property Process soulverProcess: Process {
        command: {
            const args = [root.executable, "--backend", root.backend, "--server"];
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

            // Held until the old helper is gone: a request written to it now would vanish, and
            // the queued one goes out as soon as the replacement is running.
            if (root.restarting) {
                root.restarting = false;
                root.start();
                return;
            }

            if (!root.enabled)
                return;

            root.ready = false;
            root.restartAttempts += 1;
            root.errorMessage = root.startupError || (root.restartAttempts > 3
                ? "Could not start dank-soulver-core"
                : "The helper stopped");
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

