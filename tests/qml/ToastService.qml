pragma Singleton

import QtQuick
import Quickshell

Singleton {
    property string lastMessage: ""

    function showInfo(title, message) {
        lastMessage = title + ": " + message;
    }
}
