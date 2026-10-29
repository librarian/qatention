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
        Flickable {
            id: viewport
            objectName: "consoleViewport"
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumWidth: 0; Layout.preferredWidth: 0
            clip: true
            property real zoom: 1
            property bool mouseMode: false
            readonly property real fitScale: Math.min(width / video.frameSize.width, height / video.frameSize.height)
            contentWidth: Math.max(width, video.frameSize.width * fitScale * zoom)
            contentHeight: Math.max(height, video.frameSize.height * fitScale * zoom)
            boundsBehavior: Flickable.StopAtBounds
            interactive: !pinch.active && !mouseMode
            function constrain() {
                contentX = Math.max(0, Math.min(contentX, contentWidth - width))
                contentY = Math.max(0, Math.min(contentY, contentHeight - height))
            }
            function fit() { cancelFlick(); zoom = 1; contentX = 0; contentY = 0 }
            function zoomAt(factor, x, y) {
                cancelFlick()
                const oldWidth = contentWidth, oldHeight = contentHeight
                const oldX = contentX, oldY = contentY, oldZoom = zoom
                zoom = Math.max(1, Math.min(8, zoom * factor))
                contentX = (oldX + x - oldWidth / 2) * zoom / oldZoom + contentWidth / 2 - x
                contentY = (oldY + y - oldHeight / 2) * zoom / oldZoom + contentHeight / 2 - y
                constrain()
            }
            onWidthChanged: constrain()
            onHeightChanged: constrain()
            ConsoleVideo {
                id: video
                objectName: "consoleVideo"
                width: viewport.contentWidth; height: viewport.contentHeight
                session: connection
                onFrameSizeChanged: viewport.fit()
                TapHandler {
                    enabled: !viewport.mouseMode
                    onTapped: eventPoint => {
                        video.pointer(eventPoint.position.x, eventPoint.position.y, true)
                        video.pointer(eventPoint.position.x, eventPoint.position.y, false)
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: viewport.mouseMode && connection.connected
                    onPressed: mouse => video.pointer(mouse.x, mouse.y, true)
                    onPositionChanged: mouse => { if (pressed) video.pointer(mouse.x, mouse.y, true) }
                    onReleased: mouse => video.pointer(mouse.x, mouse.y, false)
                    onCanceled: video.release()
                }
                Label { anchors.centerIn: parent; visible: !connection.connected; text: "Connect to view the console"; color: "white" }
            }
            PinchHandler {
                id: pinch
                target: null
                minimumPointCount: 2
                rotationAxis.enabled: false
                onActiveChanged: if (active) { viewport.cancelFlick(); video.release() }
                onScaleChanged: delta => viewport.zoomAt(delta, centroid.position.x, centroid.position.y)
                onTranslationChanged: delta => {
                    viewport.contentX -= delta.x; viewport.contentY -= delta.y; viewport.constrain()
                }
            }
            ScrollBar.horizontal: ScrollBar { }
            ScrollBar.vertical: ScrollBar { }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 2
            Button { text: "Fit"; Layout.fillWidth: true; Layout.preferredWidth: 1; onClicked: viewport.fit() }
            Button { text: "−"; Layout.fillWidth: true; Layout.preferredWidth: 1; onClicked: viewport.zoomAt(1 / 1.25, viewport.width / 2, viewport.height / 2) }
            Label { text: Math.round(viewport.zoom * 100) + "%" }
            Button { text: "+"; Layout.fillWidth: true; Layout.preferredWidth: 1; onClicked: viewport.zoomAt(1.25, viewport.width / 2, viewport.height / 2) }
            Button { text: "Drag"; checkable: true; Layout.fillWidth: true; Layout.preferredWidth: 1; checked: viewport.mouseMode; onClicked: {video.release();viewport.mouseMode = checked} }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 2
            Button { text: "Fn"; highlighted: keyboard.keyLayer === "nav"; onClicked: keyboard.keyLayer = keyboard.keyLayer === "nav" ? "main" : "nav" }
            Button { text: "Num"; highlighted: keyboard.keyLayer === "num"; onClicked: keyboard.keyLayer = keyboard.keyLayer === "num" ? "main" : "num" }
            Label { text: keyboard.keyLayer === "main" ? "US ANSI" : keyboard.keyLayer === "nav" ? "Navigation" : "Numpad"; Layout.fillWidth: true }
            Button { text: "Refresh"; onClicked: connection.refresh() }
        }
        Item {
            id: keyboard
            objectName: "keyboard"
            Layout.fillWidth: true
            Layout.preferredHeight: 6 * keyHeight
            property real keyHeight: window.height < 500 ? 28 : 36
            property string keyLayer: "main"
            property real unit: width / (keyLayer === "main" ? 15 : keyLayer === "nav" ? 3 : 4)
            property var keys: Ansi.keys.filter(k => keyLayer === "main" ? k.x < 15 : keyLayer === "nav" ? k.x >= 15 && k.x < 19 : k.x >= 19)
            property real offset: keyLayer === "main" ? 0 : keyLayer === "nav" ? 15.5 : 19
            property bool shifted: (mobile.modifiers & 0x22) !== 0
            clip: true
            Repeater {
                model: keyboard.keys
                Button {
                    required property var modelData
                    x: (modelData.x - keyboard.offset) * keyboard.unit; y: modelData.y * keyboard.keyHeight
                    width: modelData.w * keyboard.unit - 2
                    height: modelData.h * keyboard.keyHeight - 2
                    enabled: connection.connected
                    focusPolicy: Qt.NoFocus
                    leftPadding: 0; rightPadding: 0; topInset: 0; bottomInset: 0
                    font.pixelSize: keyboard.keyLayer === "main" ? Math.max(8, Math.min(12, keyboard.unit * 0.46)) : 14
                    text: keyboard.shifted && modelData.upper ? modelData.upper :
                          (modelData.label === "Backspace" ? "Bksp" : modelData.label)
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
        objectName: "connectDialog"
        title: "Connect to BMC"
        function newProfile() {
            profilePicker.currentIndex = -1; host.text = ""; user.text = "ADMIN"; password.clear()
            remember.checked = true; unverified.checked = false
        }
        onOpened: {
            profilePicker.currentIndex = -1
            for (let i = 0; i < mobile.profiles.length; ++i)
                if (mobile.profiles[i].host === host.text && mobile.profiles[i].user === user.text) {
                    profilePicker.currentIndex = i
                    remember.checked = mobile.profiles[i].remember
                    unverified.checked = mobile.profiles[i].unverified
                    break
                }
            password.text = mobile.savedPassword(host.text, user.text)
        }
        anchors.centerIn: parent
        width: Math.min(window.width - 24, 440)
        modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: profilePicker
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    model: mobile.profiles
                    textRole: "label"
                    displayText: currentIndex < 0 ? "New profile" : currentText
                    onActivated: index => {
                        const profile = mobile.profiles[index]
                        host.text = profile.host; user.text = profile.user
                        remember.checked = profile.remember; unverified.checked = profile.unverified
                        password.text = mobile.savedPassword(host.text, user.text)
                    }
                }
                Button { text: "New"; onClicked: connectDialog.newProfile() }
                Button {
                    text: "Delete"; enabled: profilePicker.currentIndex >= 0
                    onClicked: {
                        const profile = mobile.profiles[profilePicker.currentIndex]
                        mobile.removeProfile(profile.host, profile.user); connectDialog.newProfile()
                    }
                }
            }
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
