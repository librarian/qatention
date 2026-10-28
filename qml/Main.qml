import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QATENtion.Console

ApplicationWindow {
    id: window
    width: 1100; height: 800
    minimumWidth: 720; minimumHeight: 500
    visible: true
    title: "QATENtion — desktop preview"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent; anchors.margins: 4
            Button { text: "Connect…"; enabled: !kvm.busy; onClicked: connection.open() }
            Button { text: "Disconnect"; enabled: kvm.busy; onClicked: kvm.close() }
            Button { text: "Debug log"; onClicked: logging.open() }
            ToolSeparator {}
            Button { text: "Refresh"; enabled: kvm.connected; onClicked: kvm.refresh() }
            Button { text: "Ctrl+Alt+Del"; enabled: kvm.connected; onClicked: kvm.ctrlAltDelete() }
            Button { text: "Screenshot"; enabled: kvm.connected; onClicked: screenshot.open() }
            Item { Layout.fillWidth: true }
            Button {
                text: window.visibility === Window.FullScreen ? "Windowed" : "Fullscreen"
                onClicked: window.visibility === Window.FullScreen ? window.showNormal() : window.showFullScreen()
            }
        }
    }
    Console { id: console; anchors.fill: parent; session: kvm }
    footer: Label {
        text: kvm.status; padding: 10; wrapMode: Text.Wrap
    }
    FileDialog {
        id: screenshot
        title: "Save console screenshot"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG images (*.png)"]
        defaultSuffix: "png"
        onAccepted: if (!console.saveScreenshot(selectedFile)) screenshotError.open()
    }
    Dialog {
        id: screenshotError; title: "Screenshot could not be saved"
        anchors.centerIn: parent; standardButtons: Dialog.Ok
        Label { text: "Check that a frame is available and the destination is writable." }
    }
    Dialog {
        id: logging; title: "Debug log"
        anchors.centerIn: parent; modal: false
        width: Math.min(850, window.width - 40); height: Math.min(550, window.height - 40)
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            RowLayout {
                CheckBox { text: "Enable debug logging"; checked: debugLog.enabled; onToggled: debugLog.enabled = checked }
                Button { text: "Clear"; onClicked: debugLog.clear() }
            }
            TextField { Layout.fillWidth: true; text: debugLog.path; readOnly: true; selectByMouse: true }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "Logs omit credentials and typed keys. The first unsupported video packet is saved beside the log and may contain console pixels."
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                TextArea { text: debugLog.text; onTextChanged: cursorPosition = length; readOnly: true; selectByMouse: true; font.family: "monospace"; wrapMode: TextEdit.Wrap }
            }
        }
    }
    Dialog {
        id: deleteProfile; title: "Delete saved profile?"
        anchors.centerIn: parent; modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (profilePicker.currentIndex >= 0 && profiles.remove(profiles.entries[profilePicker.currentIndex].name))
                profilePicker.currentIndex = -1
        }
    }
    Dialog {
        id: connection
        objectName: "connectionDialog"
        title: "Connect to a BMC"
        anchors.centerIn: parent
        width: Math.min(560, window.width - 40)
        height: Math.min(implicitHeight, window.height - 40)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            kvm.open(options())
            password.clear()
            console.forceActiveFocus()
        }
        function options() {
            return {host: host.text, user: user.text, password: password.text,
                autoArm: autoArm.checked, port: port.value, tls: tls.checked,
                webPort: webPort.value, allowUntrusted: untrusted.checked,
                switchJava: javaMode.checked, autoReconnect: reconnect.checked,
                frameInterval: continuous.checked ? 33 : 100}
        }
        function loadProfile(p) {
            password.text = p.password || ""
            profileName.text = p.name || ""
            host.text = p.host || ""
            autoArm.checked = p.autoArm !== false
            user.text = p.user || ""
            port.value = p.port || 5900
            webPort.value = p.webPort || 443
            tls.checked = p.tls !== false
            untrusted.checked = p.allowUntrusted === true
            javaMode.checked = p.switchJava === true
            continuous.checked = p.frameInterval === 33
            reconnect.checked = p.autoReconnect !== false
        }
        onOpened: { if (profilePicker.currentIndex >= 0) connection.loadProfile(profiles.entries[profilePicker.currentIndex]) }
        onRejected: password.clear()
        contentItem: ScrollView {
            id: connectionScroll
            implicitHeight: connectionFields.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
            id: connectionFields
            width: connectionScroll.availableWidth
            spacing: 6
            RowLayout {
                ComboBox {
                    id: profilePicker; Layout.fillWidth: true
                    model: profiles.entries; textRole: "name"; currentIndex: -1
                    displayText: currentIndex < 0 ? "Choose a saved profile…" : currentText
                    onActivated: connection.loadProfile(profiles.entries[currentIndex])
                }
                Button { text: "New"; onClicked: { profilePicker.currentIndex = -1; connection.loadProfile({}) } }
                Button {
                    text: "Delete"; enabled: profilePicker.currentIndex >= 0
                    onClicked: deleteProfile.open()
                }
            }
            RowLayout {
                TextField { id: profileName; Layout.fillWidth: true; placeholderText: "Profile name (same name updates it)" }
                Button {
                    text: "Save profile"
                    onClicked: {
                        if (profiles.save(profileName.text, connection.options())) {
                            for (let i = 0; i < profiles.entries.length; ++i)
                                if (profiles.entries[i].name === profileName.text.trim()) profilePicker.currentIndex = i
                        }
                    }
                }
            }
            Label { Layout.fillWidth: true; visible: text.length > 0; text: profiles.error; wrapMode: Text.Wrap }
            TextField { id: host; Layout.fillWidth: true; placeholderText: "BMC hostname or IP address" }
            CheckBox { id: autoArm; text: "Log in through the BMC web interface"; checked: true }
            TextField { id: user; Layout.fillWidth: true; placeholderText: autoArm.checked ? "BMC username" : "KVM username token" }
            TextField { id: password; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: autoArm.checked ? "BMC password" : "KVM password token" }
            RowLayout {
                visible: autoArm.checked
                Label { text: "HTTPS port" }
                SpinBox { id: webPort; from: 1; to: 65535; value: 443; editable: true }
            }
            RowLayout {
                visible: !autoArm.checked
                Label { text: "KVM port" }
                SpinBox { id: port; from: 1; to: 65535; value: 5900; editable: true }
                CheckBox { id: tls; text: "TLS"; checked: true }
            }
            CheckBox { id: untrusted; text: "Allow an unverified BMC certificate" }
            CheckBox { id: javaMode; visible: autoArm.checked; text: "Switch the BMC console interface to Java" }
            CheckBox { id: reconnect; visible: autoArm.checked; checked: true; text: "Reconnect automatically after a dropped connection" }
            CheckBox { id: continuous; text: "Request updates up to 30 times per second" }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "Save profile stores these credentials locally. Manual tokens can expire; refresh them from the BMC launch JNLP."
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "Preview: one console, US keyboard mapping, Hermon/ASPEED video. Virtual media is not yet ported."
            }
            }
        }
    }
}
