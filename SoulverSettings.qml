import QtQuick
import qs.Common
import qs.Widgets
import qs.Modules.Plugins

PluginSettings {
    id: root
    pluginId: "dankSoulverCore"

    StyledText {
        width: parent.width
        text: "Dank SoulverCore"
        font.pixelSize: Theme.fontSizeLarge
        font.weight: Font.Bold
        color: Theme.surfaceText
    }

    StyledText {
        width: parent.width
        text: "Natural-language arithmetic, percentages, units, dates, time zones, and live currency conversions directly in the launcher."
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.surfaceVariantText
    }

    ToggleSetting {
        id: noTriggerSetting
        settingKey: "noTrigger"
        label: "Automatic results"
        description: value
            ? "Evaluate normal launcher searches and merge valid answers into the results."
            : "Only evaluate searches that begin with the trigger."
        defaultValue: true
    }

    StringSetting {
        visible: !noTriggerSetting.value
        settingKey: "trigger"
        label: "Trigger"
        description: "Prefix used to explicitly invoke Soulver."
        placeholder: "="
        defaultValue: "="
    }

    StringSetting {
        visible: noTriggerSetting.value
        settingKey: "minimumCharacters"
        label: "Minimum query length"
        description: "Avoid evaluating very short launcher searches."
        placeholder: "3"
        defaultValue: "3"
    }

    StringSetting {
        settingKey: "debounceMilliseconds"
        label: "Evaluation delay"
        description: "Milliseconds to wait after typing before evaluating."
        placeholder: "90"
        defaultValue: "90"
    }

    StringSetting {
        settingKey: "helperPath"
        label: "Helper executable"
        description: "Optional absolute path. Leave empty to use the plugin helper, then PATH."
        placeholder: "/usr/local/bin/dank-soulver-core"
        defaultValue: ""
    }

    StringSetting {
        settingKey: "resourcesPath"
        label: "Soulver resources"
        description: "Optional resource directory override. Usually discovered automatically."
        placeholder: "/usr/share/soulver-core/resources"
        defaultValue: ""
    }

    StyledText {
        width: parent.width
        text: noTriggerSetting.value
            ? "Try: 15% of 80, 40 SGD in USD, 12 km in miles, or time in New York in 2 hours"
            : "Try: = 15% of 80 or = 40 SGD in USD"
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.surfaceVariantText
    }
}
