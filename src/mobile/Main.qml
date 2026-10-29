import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qatention.Native 1.0
import "AnsiKeys.js" as Ansi

ApplicationWindow {
    id: window
    width: 480; height: 840
    visible: true
    title: "QATENtion"
    header: ToolBar {
        topPadding: mobile.safeInsets.top
        leftPadding: mobile.safeInsets.left
        rightPadding: mobile.safeInsets.right
        contentItem: RowLayout {
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
        anchors.leftMargin: mobile.safeInsets.left
        anchors.rightMargin: mobile.safeInsets.right
        anchors.bottomMargin: mobile.safeInsets.bottom
        Label { text: connection.status; Layout.fillWidth: true; wrapMode: Text.Wrap; padding: 8 }
        ConsoleVideo {
            id: video
            clip: true
            session: connection
            Layout.fillWidth: true; Layout.fillHeight: true
            PinchHandler {
                id: pinch
                target: null
                minimumPointCount: 2
                rotationAxis.enabled: false
                onActiveChanged: if (active) video.release()
                onScaleChanged: delta => video.adjustView(delta, 0, 0)
                onTranslationChanged: delta => video.adjustView(1, delta.x, delta.y)
            }
            MouseArea {
                anchors.fill: parent
                enabled: connection.connected && !pinch.active
                onPressed: mouse => video.pointer(mouse.x, mouse.y, true)
                onPositionChanged: mouse => { if (pressed) video.pointer(mouse.x, mouse.y, true) }
                onReleased: mouse => video.pointer(mouse.x, mouse.y, false)
                onCanceled: video.release()
            }
            Label { anchors.centerIn: parent; visible: !connection.connected; text: "Connect to view the console"; color: "white" }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Fit"; onClicked: video.fit() }
            Button { text: "−"; onClicked: video.adjustView(1 / 1.25, 0, 0) }
            Label { text: Math.round(video.zoom * 100) + "%" }
            Button { text: "+"; onClicked: video.adjustView(1.25, 0, 0) }
            Item { Layout.fillWidth: true }
            Button { text: "Refresh"; onClicked: connection.refresh() }
        }
        Flickable {
            id: keyboard
            Layout.fillWidth: true
            Layout.preferredHeight: 6 * keyHeight + 10
            property real keyHeight: window.height < 500 ? 28 : 36
            property real unit: Math.max(34, width / 23)
            property bool shifted: (mobile.modifiers & 0x22) !== 0
            contentWidth: 23 * unit
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOn }
            Repeater {
                model: Ansi.keys
                Button {
                    required property var modelData
                    x: modelData.x * keyboard.unit; y: modelData.y * keyboard.keyHeight
                    width: modelData.w * keyboard.unit - 2
                    height: modelData.h * keyboard.keyHeight - 2
                    enabled: connection.connected
                    focusPolicy: Qt.NoFocus
                    leftPadding: 0; rightPadding: 0; topInset: 0; bottomInset: 0
                    font.pixelSize: modelData.label.length > 3 ? 10 : 13
                    text: keyboard.shifted && modelData.upper ? modelData.upper : modelData.label
                    highlighted: modelData.usage >= 224 && (mobile.modifiers & (1 << (modelData.usage - 224))) !== 0
                    onClicked: {
                        if (modelData.usage >= 224) mobile.toggleModifier(modelData.usage)
                        else mobile.tapKey(modelData.usage)
                    }
                }
            }
        }
    }
    Dialog {
        background: Rectangle { color: Material.background; radius: 12 }
        id: connectDialog
        title: "Connect to BMC"
        onOpened: password.text = mobile.savedPassword(host.text, user.text)
        anchors.centerIn: parent
        width: Math.min(window.width - 24, 440)
        modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            TextField { id: host; Layout.fillWidth: true; placeholderText: "BMC hostname or IP"; text: mobile.lastHost; onEditingFinished: password.text = mobile.savedPassword(text, user.text); inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase }
            TextField { id: user; Layout.fillWidth: true; placeholderText: "Username"; text: mobile.lastUser; onEditingFinished: password.text = mobile.savedPassword(host.text, text); inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
            TextField { id: password; Layout.fillWidth: true; placeholderText: "Password"; echoMode: TextInput.Password }
            CheckBox { id: unverified; text: "Allow unverified BMC certificates"; checked: false }
            CheckBox { id: remember; text: "Save password on this device"; checked: true }
        }
        onAccepted: { mobile.connectTo(host.text,user.text,password.text,unverified.checked,remember.checked);password.clear() }
        onRejected: password.clear()
    }
    Dialog {
        background: Rectangle { color: Material.background; radius: 12 }
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
        background: Rectangle { color: Material.background; radius: 12 }
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
        background: Rectangle { color: Material.background; radius: 12 }
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
