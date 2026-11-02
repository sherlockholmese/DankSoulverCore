//@ pragma UseQApplication

import QtQuick
import Quickshell
import Quickshell.Io
import qs.Services

// Drives one scenario through the same calls DMS makes, polling until each step's condition holds.
Scope {
    id: root

    readonly property string scenario: Quickshell.env("TEST_SCENARIO")
    readonly property string helper: Quickshell.env("TEST_HELPER")
    property var launcher: null
    property var steps: []
    property int stepIndex: 0
    property int stepAge: 0
    property int helperStarts: 0
    property int failures: 0
    property var duringRestart: null

    QtObject {
        id: fakePluginService

        property var data: ({})

        signal pluginDataChanged(string pluginId)

        function loadPluginData(pluginId, key, defaultValue) {
            return data[key] !== undefined ? data[key] : defaultValue;
        }

        function savePluginData(pluginId, key, value) {
            data[key] = value;
            pluginDataChanged(pluginId);
            return true;
        }

        function getPluginPath(pluginId) {
            return "";
        }
    }

    function log(message) {
        console.log("TEST: " + message);
    }

    function finish() {
        log(failures === 0 ? "scenario passed" : "scenario FAILED");
        Qt.quit();
    }

    function expect(description, actual, wanted) {
        if (JSON.stringify(actual) === JSON.stringify(wanted))
            return;
        failures += 1;
        log("FAIL " + description + ": got " + JSON.stringify(actual) + ", wanted " + JSON.stringify(wanted));
    }

    function items(query) {
        return launcher.getItems(query);
    }

    // Waits for a settled answer: the first item once it is no longer the loading placeholder.
    function answerTo(query) {
        const found = items(query);
        return found.length === 1 && found[0].comment !== "Calculating…" ? found[0] : null;
    }

    function step(description, condition, then) {
        steps.push({
            description: description,
            condition: condition,
            then: then
        });
    }

    function expectAnswer(query, wantedName, wantedComment, wantedCategory, wantedIcon) {
        step("answer to " + query, () => answerTo(query) !== null, () => {
            const item = answerTo(query);
            expect(query + " name", item.name, wantedName);
            expect(query + " comment", item.comment, wantedComment);
            expect(query + " category", item.categories, [wantedCategory]);
            if (wantedIcon)
                expect(query + " icon", item.icon, wantedIcon);
        });
    }

    // Silent means the engine answered and the launcher chose to show nothing.
    function expectSilent(query) {
        step("silence for " + query, () => {
            items(query);
            return launcher.currentQuery === query;
        }, () => expect(query + " items", items(query), []));
    }

    function setting(key, value) {
        step("set " + key, () => true, () => fakePluginService.savePluginData("dankSoulverCore", key, value));
    }

    function createLauncher(settings) {
        fakePluginService.data = Object.assign({
            noTrigger: true,
            helperPath: helper,
            debounceMilliseconds: "10"
        }, settings);

        const component = Qt.createComponent("SoulverLauncher.qml");
        if (component.status !== Component.Ready) {
            log("FAIL could not load launcher: " + component.errorString());
            failures += 1;
            finish();
            return;
        }

        launcher = component.createObject(root, {
            pluginService: fakePluginService
        });
        launcher.soulver.resultReady.connect((requestId, expression, result) => {
            if (requestId === 1000)
                duringRestart = result;
        });
        launcher.soulver.soulverProcess.runningChanged.connect(() => {
            if (launcher.soulver.soulverProcess.running)
                helperStarts += 1;
        });
    }

    function numenScenario() {
        createLauncher({
            backend: "numen"
        });

        expect("configured engine", launcher.backend, "numen");
        expect("helper command", launcher.soulver.soulverProcess.command,
            [helper, "--backend", "numen", "--server"]);

        expectAnswer("15% of 80", "12", "Number", "Numen", "material:calculate");
        expectAnswer("40 SGD in USD", "$20", "Currency", "Numen", "material:currency_exchange");
        expectAnswer("12 km in miles", "7.456454mi", "Unit", "Numen", "material:straighten");
        expectAnswer("1 day + 2 days", "3 days", "Duration", "Numen", "material:schedule");
        step("time zone answer", () => answerTo("time in tokyo") !== null,
            () => expect("time icon", answerTo("time in tokyo").icon, "material:schedule"));

        for (const search of ["firefox", "gimp 2", "3d printer", "usd", "week", "9^9^9^9"])
            expectSilent(search);

        step("short query", () => true, () => expect("below minimum", items("12"), []));
        step("cleared query", () => true, () => {
            expect("empty query", items(""), []);
            expect("pending cleared", launcher.pendingQuery, "");
        });

        expectAnswer("6 * 7", "42", "Number", "Numen");
        step("copy to clipboard", () => true, () => {
            launcher.executeItem(answerTo("6 * 7"));
            expect("toast", ToastService.lastMessage, "Numen: Copied 42");
        });
        step("clipboard written", () => {
            clipboardCheck.reload();
            return clipboardCheck.text().trim() === "42";
        }, () => {});

        step("a single helper process", () => true, () => expect("helper starts", helperStarts, 1));

        step("helper crash", () => true, () => launcher.soulver.soulverProcess.signal(9));
        step("helper restarted", () => helperStarts === 2 && launcher.soulver.ready, () => {});
        expectAnswer("8 * 8", "64", "Number", "Numen");
    }

    function switchingScenario() {
        createLauncher({});
        expect("default engine", launcher.backend, "soulver");
        expect("default command", launcher.soulver.soulverProcess.command,
            [helper, "--backend", "soulver", "--server"]);

        expectAnswer("15% of 80", "12", "Number", "SoulverCore");

        // SoulverCore understands this; Numen does not, so a stale answer cannot pass for a new one.
        expectAnswer("forty two plus eight", "50", "Number", "SoulverCore");
        setting("backend", "numen");
        step("switch re-evaluates the open query", () => {
            items("forty two plus eight");
            return launcher.soulver.ready && launcher.currentQuery === "forty two plus eight";
        }, () => {
            expect("engine", launcher.backend, "numen");
            expect("Numen cannot answer", items("forty two plus eight"), []);
        });

        // Asked in the same tick as the switch, while the previous helper is still shutting down.
        step("request during restart", () => launcher.soulver.ready, () => {
            launcher.backend = "soulver";
            launcher.soulver.calculate(1000, "forty two plus eight");
        });
        step("answered by the new helper", () => {
            return duringRestart !== null;
        }, () => {
            expect("value", duringRestart.value, "50");
            expect("engine", duringRestart.backend, "soulver");
        });
        setting("backend", "numen");

        expectAnswer("6 * 7", "42", "Number", "Numen");
        setting("backend", "soulver");
        expectAnswer("6 * 7", "42", "Number", "SoulverCore");

        // Rapid flipping must settle on the last choice with exactly one helper alive.
        step("rapid switching", () => true, () => {
            for (const name of ["numen", "soulver", "numen"])
                fakePluginService.savePluginData("dankSoulverCore", "backend", name);
        });
        expectAnswer("7 * 7", "49", "Number", "Numen");
        step("no errors left behind", () => true, () => expect("error", launcher.soulver.errorMessage, ""));
    }

    function triggerScenario() {
        createLauncher({
            backend: "numen",
            noTrigger: false,
            trigger: ";;"
        });

        expect("trigger", launcher.trigger, ";;");
        expectAnswer("2 + 2", "4", "Number", "Numen");
        step("trigger mode has no minimum length", () => answerTo("9") !== null,
            () => expect("single digit", answerTo("9").name, "9"));
        setting("noTrigger", true);
        step("automatic mode", () => launcher.trigger === "", () => {});
    }

    function unknownBackendScenario() {
        createLauncher({
            backend: "bogus"
        });
        expect("falls back to the default", launcher.backend, "soulver");
    }

    function missingSoulverScenario() {
        createLauncher({});

        step("explains the failure", () => {
            const found = items("15% of 80");
            return found.length === 1 && found[0].name === "SoulverCore is unavailable";
        }, () => {
            const item = items("15% of 80")[0];
            expect("not actionable", item.action, "none");
            expect("names the fix", item.comment.indexOf("switch the backend to Numen") !== -1, true);
        });

        // Keeps app search clean: the failure only shows on something that looks like maths.
        step("app search stays silent", () => true, () => expect("firefox", items("firefox"), []));

        setting("backend", "numen");
        expectAnswer("15% of 80", "12", "Number", "Numen");
        step("error cleared", () => true, () => expect("error", launcher.soulver.errorMessage, ""));
    }

    FileView {
        id: clipboardCheck

        path: Quickshell.env("TEST_CLIPBOARD")
        blockLoading: true
        printErrors: false
    }

    Timer {
        interval: 20
        repeat: true
        running: true

        onTriggered: {
            if (root.stepIndex >= root.steps.length) {
                root.finish();
                return;
            }

            const current = root.steps[root.stepIndex];
            if (current.condition()) {
                current.then();
                root.stepIndex += 1;
                root.stepAge = 0;
                return;
            }

            root.stepAge += 1;
            if (root.stepAge > 500) {
                root.failures += 1;
                root.log("FAIL timed out: " + current.description);
                root.finish();
            }
        }
    }

    Component.onCompleted: root[scenario + "Scenario"]()
}
