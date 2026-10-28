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
    color: "#20262d"
    palette.window: "#20262d"
    palette.windowText: "#e4e9ed"
    palette.base: "#15191e"
    palette.text: "#e4e9ed"
    palette.button: "#35404b"
    palette.buttonText: "#e4e9ed"
    palette.highlight: "#4b91c8"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent; anchors.margins: 4
            Button { text: "Connect…"; enabled: !kvm.busy; onClicked: connection.open() }
            Button { text: "Disconnect"; enabled: kvm.busy; onClicked: kvm.close() }
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
        background: Rectangle { color: "#29323b" }
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
        id: connection
        title: "Connect to a BMC"
        anchors.centerIn: parent
        width: Math.min(560, window.width - 40)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            kvm.open({host: host.text, user: user.text, password: password.text,
                autoArm: autoArm.checked, port: port.value, tls: tls.checked,
                webPort: webPort.value, allowUntrusted: untrusted.checked,
                switchJava: javaMode.checked, frameInterval: continuous.checked ? 33 : 100})
            password.clear()
            console.forceActiveFocus()
        }
        onRejected: password.clear()
        contentItem: ColumnLayout {
            spacing: 10
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
            CheckBox { id: continuous; text: "Request updates up to 30 times per second" }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "Credentials are kept in memory only. Manual mode needs fresh tokens from the BMC launch JNLP, not the web password."
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "Preview: one console, US keyboard mapping, Hermon/ASPEED video. Virtual media and saved profiles are not yet ported."
            }
        }
    }
}
