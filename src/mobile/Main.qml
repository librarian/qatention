import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qatention.Native 1.0

ApplicationWindow {
    id: window
    width: 480; height: 840
    visible: true
    title: "QATENtion"
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label { text: "QATENtion"; font.bold: true; Layout.fillWidth: true; leftPadding: 12 }
            ToolButton {
                text: connection.busy ? "Disconnect" : "Connect"
                onClicked: { if (connection.busy) mobile.disconnectSession(); else connectDialog.open() }
            }
            ToolButton { text: "Logs"; onClicked: logs.open() }
        }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 4
        Label { text: connection.status; Layout.fillWidth: true; wrapMode: Text.Wrap; padding: 8 }
        ConsoleVideo {
            id: video
            session: connection
            Layout.fillWidth: true; Layout.fillHeight: true
            MouseArea {
                anchors.fill: parent
                enabled: connection.connected
                onPressed: mouse => video.pointer(mouse.x, mouse.y, true)
                onPositionChanged: mouse => { if (pressed) video.pointer(mouse.x, mouse.y, true) }
                onReleased: mouse => video.pointer(mouse.x, mouse.y, false)
                onCanceled: video.release()
            }
            Label { anchors.centerIn: parent; visible: !connection.connected; text: "Connect to view the console"; color: "white" }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 52
            contentWidth: keys.implicitWidth
            RowLayout {
                id: keys
                enabled: connection.connected
                Repeater {
                    model: [{label:"Esc", key:41}, {label:"Tab", key:43}, {label:"↑", key:82}, {label:"↓", key:81}, {label:"←", key:80}, {label:"→", key:79}, {label:"Enter", key:40}, {label:"Backspace", key:42}]
                    Button { required property var modelData; text: modelData.label; onClicked: mobile.tapKey(modelData.key) }
                }
                Button { text: "Ctrl+Alt+Del"; onClicked: connection.ctrlAltDelete() }
                Button { text: "Refresh"; onClicked: connection.refresh() }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 6
            enabled: connection.connected
            TextField { id: input; Layout.fillWidth: true; placeholderText: "Text to send (US layout)"; maximumLength: 256; onAccepted: send.clicked() }
            Button { id: send; text: "Send"; onClicked: {mobile.typeText(input.text);input.clear()} }
        }
    }
    Dialog {
        background: Rectangle { color: window.palette.window; radius: 12 }
        id: connectDialog
        title: "Connect to BMC"
        anchors.centerIn: parent
        width: Math.min(window.width - 24, 440)
        modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            TextField { id: host; Layout.fillWidth: true; placeholderText: "BMC hostname or IP"; text: mobile.lastHost; inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase }
            TextField { id: user; Layout.fillWidth: true; placeholderText: "Username"; text: mobile.lastUser; inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
            TextField { id: password; Layout.fillWidth: true; placeholderText: "Password"; echoMode: TextInput.Password }
            CheckBox { id: unverified; text: "Allow unverified BMC certificates"; checked: false }
            Label { text: "Password is not saved in this preview."; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
        onAccepted: { mobile.connectTo(host.text,user.text,password.text,unverified.checked);password.clear() }
        onRejected: password.clear()
    }
    Dialog {
        background: Rectangle { color: window.palette.window; radius: 12 }
        id: trust
        property string endpoint
        property string fingerprint
        title: "Trust console certificate?"
        anchors.centerIn: parent; width: Math.min(window.width - 24, 440)
        modal: true; closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label {
            text: "Console: " + trust.endpoint + "\n\nCompare this SHA-256 fingerprint with a trusted source:\n\n" + trust.fingerprint + "\n\nTrust and remember this certificate? HTTPS verification stays enabled."
            wrapMode: Text.WrapAnywhere
        }
        onAccepted: mobile.trustConsole()
        onRejected: mobile.rejectConsole()
    }
    Dialog {
        background: Rectangle { color: window.palette.window; radius: 12 }
        id: logs
        title: "Debug log"
        anchors.centerIn: parent; width: window.width - 24; height: window.height - 80
        modal: true; standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            RowLayout {
                Switch { text: "Enabled"; checked: debugLog.enabled; onToggled: debugLog.enabled = checked }
                Button { text: "Copy"; onClicked: mobile.copyLogs() }
                Button { text: "Clear"; onClicked: debugLog.clear() }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea { text: debugLog.text; readOnly: true; wrapMode: TextEdit.WrapAnywhere; selectByMouse: true; font.pixelSize: 12 }
            }
        }
    }
    Dialog {
        background: Rectangle { color: window.palette.window; radius: 12 }
        id: message
        property string messageText
        title: "QATENtion"
        anchors.centerIn: parent; width: window.width - 32
        modal: true; standardButtons: Dialog.Ok
        contentItem: Label { text: message.messageText; wrapMode: Text.Wrap }
    }
    Connections {
        target: mobile
        function onTrustRequested(endpoint, fingerprint) {trust.endpoint=endpoint;trust.fingerprint=fingerprint;trust.open()}
        function onNotice(text) {message.messageText=text;message.open()}
    }
    Component.onCompleted: connectDialog.open()
}
