import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 1040
    height: 680
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: "AirGap QR Transfer"
    color: "#f4f6f1"

    property var selectedReceiveCamera: receiveMediaDevices.defaultVideoInput
    property bool receiveCameraInitialized: false
    property var speedModeLabels: sendController.speedModeLabels
    readonly property bool androidCameraHot: Qt.platform.os === "android"
    property bool scannerMode: false

    readonly property int pagePadding: width < 760 ? 12 : 18
    readonly property color accentColor: "#2f7d63"
    readonly property color accentPressedColor: "#25664f"
    readonly property color dangerColor: "#b84a45"
    readonly property color inkColor: "#222725"
    readonly property color mutedColor: "#66706a"
    readonly property color panelColor: "#ffffff"
    readonly property color lineColor: "#d8ddd5"

    Component.onCompleted: {
        modeTabs.currentIndex = startupMode
        if (startupFullScreen) {
            root.visibility = Window.FullScreen
        }
        if (startupScannerMode) {
            root.scannerMode = true
        }
    }

    Binding {
        target: sendController
        property: "scannerMode"
        value: root.scannerMode
    }

    component AppButton: Button {
        id: control
        property bool primary: false
        property bool danger: false

        width: 96
        height: 40
        font.pixelSize: 13
        font.weight: primary ? Font.DemiBold : Font.Medium
        padding: 10

        contentItem: Text {
            text: control.text
            color: control.enabled
                ? (control.primary || control.danger ? "#ffffff" : root.inkColor)
                : "#8c948e"
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 6
            color: !control.enabled
                ? "#e3e7df"
                : control.pressed
                    ? (control.primary ? root.accentPressedColor : control.danger ? "#983d39" : "#eef2ec")
                    : control.primary
                        ? root.accentColor
                        : control.danger
                            ? root.dangerColor
                            : "#ffffff"
            border.color: control.primary || control.danger ? "transparent" : root.lineColor
            border.width: 1
        }
    }

    component InfoPill: Rectangle {
        id: pill
        property alias text: pillLabel.text
        property color tone: "#edf4ef"
        property color textColor: root.inkColor

        implicitWidth: pillLabel.implicitWidth + 20
        implicitHeight: 28
        radius: 6
        color: tone
        border.color: Qt.darker(tone, 1.08)
        border.width: 1

        Label {
            id: pillLabel
            anchors.centerIn: parent
            color: pill.textColor
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    component StatusStrip: Rectangle {
        id: strip
        property alias text: stripLabel.text
        property bool alert: false

        Layout.fillWidth: true
        implicitHeight: 38
        radius: 6
        color: alert ? "#fff2ee" : "#eef4ef"
        border.color: alert ? "#e6b8ab" : "#cadbd0"

        Label {
            id: stripLabel
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            color: strip.alert ? "#80382f" : root.inkColor
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    FileDialog {
        id: openDialog
        fileMode: FileDialog.OpenFile
        onAccepted: {
            var fileUrl = selectedFile.toString()
            if (fileUrl.length === 0 && selectedFiles.length > 0) {
                fileUrl = selectedFiles[0].toString()
            }
            sendController.prepareFileFromString(fileUrl)
        }
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        onAccepted: receiveController.saveToFile(selectedFile)
    }

    FileDialog {
        id: scannerSaveDialog
        fileMode: FileDialog.SaveFile
        onAccepted: scannerReceiveController.saveToFile(selectedFile)
    }

    MediaDevices {
        id: receiveMediaDevices

        onVideoInputsChanged: root.selectReceiveCamera()
    }

    Camera {
        id: receiveCamera
        cameraDevice: root.selectedReceiveCamera
        active: receiveController.scanning
            || sendController.feedbackScanning
            || (root.androidCameraHot && modeTabs.currentIndex === 1 && !receiveController.feedbackVisible)
        onCameraDeviceChanged: receiveController.setSelectedCameraName(cameraDevice.description)
        onErrorOccurred: function(error, errorString) {
            receiveController.noteCameraError(errorString)
        }
    }

    CaptureSession {
        camera: receiveCamera
        videoOutput: sendController.feedbackScanning ? sendFeedbackPreview : receivePreview
    }

    function receiveCameraName(index) {
        if (index < 0 || index >= receiveMediaDevices.videoInputs.length) {
            return "No camera"
        }

        var description = receiveMediaDevices.videoInputs[index].description
        return description.length > 0 ? description : ("Camera " + (index + 1))
    }

    function selectReceiveCamera() {
        var inputs = receiveMediaDevices.videoInputs
        if (inputs.length <= 0) {
            receiveCameraInitialized = false
            selectedReceiveCamera = receiveMediaDevices.defaultVideoInput
            receiveController.setSelectedCamera(-1, "")
            return
        }

        var index = cameraSelector.currentIndex
        if (!receiveCameraInitialized) {
            index = startupCameraIndex >= 0 && startupCameraIndex < inputs.length ? startupCameraIndex : 0
            cameraSelector.currentIndex = index
            receiveCameraInitialized = true
        } else if (index < 0 || index >= inputs.length) {
            index = 0
            cameraSelector.currentIndex = index
        }

        selectedReceiveCamera = inputs[index]
        receiveController.setSelectedCamera(index, receiveCameraName(index))
    }

    function setTransferSpeedMode(index) {
        sendController.setSpeedMode(index)
        receiveController.setSpeedMode(index)
    }

    header: ToolBar {
        id: topBar
        padding: 0

        background: Rectangle {
            color: "#fbfcf8"
            border.color: root.lineColor
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root.width < 760 ? 8 : 12
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        Layout.fillWidth: true
                        text: "AirGap QR Transfer"
                        color: root.inkColor
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: modeTabs.currentIndex === 0 ? sendController.status : receiveController.status
                        color: root.mutedColor
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                TabBar {
                    id: modeTabs
                    Layout.preferredWidth: root.width < 820 ? 220 : 280
                    Layout.maximumWidth: 320
                    Layout.alignment: Qt.AlignVCenter

                    TabButton { text: "Send" }
                    TabButton { text: "Receive" }
                }

                // 模式切换（全局可见）
                RowLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 4

                    Label {
                        text: "Mode"
                        color: root.mutedColor
                        font.pixelSize: 12
                    }

                    RadioButton {
                        id: globalCameraModeRadio
                        text: "Camera"
                        checked: !root.scannerMode
                        font.pixelSize: 12
                        onCheckedChanged: {
                            if (checked) {
                                root.scannerMode = false
                            }
                        }
                    }

                    RadioButton {
                        id: globalScannerModeRadio
                        text: "Scanner"
                        checked: root.scannerMode
                        font.pixelSize: 12
                        onCheckedChanged: {
                            if (checked) {
                                root.scannerMode = true
                            }
                        }
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 8

                AppButton {
                    visible: modeTabs.currentIndex === 0
                    text: "Open"
                    primary: true
                    onClicked: Qt.platform.os === "android" ? sendController.chooseOpenFile() : openDialog.open()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 0
                    text: "Back"
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.previousFrame()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 0
                    text: sendController.playing ? "Pause" : "Play"
                    primary: !sendController.playing
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.playing ? sendController.stopPlayback() : sendController.startPlayback()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 0
                    text: "Next"
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.nextFrame()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 0
                    width: 110
                    text: sendController.feedbackScanning ? "Stop FB" : "Scan FB"
                    danger: sendController.feedbackScanning
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.feedbackScanning ? sendController.stopFeedbackScan() : sendController.startFeedbackScan()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 0
                    text: "All"
                    enabled: sendController.resendMode
                    onClicked: sendController.clearResendFilter()
                }

                // 摄像头模式 - 接收端控制
                AppButton {
                    visible: modeTabs.currentIndex === 1 && !root.scannerMode
                    text: receiveController.scanning ? "Stop" : "Scan"
                    primary: !receiveController.scanning
                    danger: receiveController.scanning
                    enabled: receiveController.cameraAvailable || receiveController.scanning
                    onClicked: receiveController.scanning ? receiveController.stopScanning() : receiveController.startScanning()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 1 && !root.scannerMode
                    text: "Reset"
                    onClicked: receiveController.reset()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 1 && !root.scannerMode
                    width: 110
                    text: receiveController.feedbackVisible ? "Hide FB" : "Feedback"
                    enabled: receiveController.feedbackVisible || receiveController.feedbackAvailable
                    onClicked: receiveController.feedbackVisible ? receiveController.hideFeedbackQr() : receiveController.showFeedbackQr()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 1 && !root.scannerMode
                    text: "Save"
                    primary: true
                    enabled: receiveController.completed
                    onClicked: Qt.platform.os === "android" ? receiveController.chooseSaveLocation() : saveDialog.open()
                }

                // 扫描器模式 - 接收端控制
                AppButton {
                    visible: modeTabs.currentIndex === 1 && root.scannerMode
                    text: scannerReceiveController.scanning ? "Stop" : "Start Scan"
                    primary: !scannerReceiveController.scanning
                    danger: scannerReceiveController.scanning
                    enabled: scannerReceiveController.selectedPort.length > 0 || scannerReceiveController.scanning
                    onClicked: scannerReceiveController.scanning ? scannerReceiveController.stopScanning() : scannerReceiveController.startScanning()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 1 && root.scannerMode
                    text: "Reset"
                    enabled: !scannerReceiveController.scanning
                    onClicked: scannerReceiveController.reset()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 1 && root.scannerMode
                    text: "Save"
                    primary: true
                    enabled: scannerReceiveController.completed
                    onClicked: scannerSaveDialog.open()
                }

                AppButton {
                    visible: modeTabs.currentIndex === 1 && root.scannerMode
                    text: "Copy Text"
                    enabled: scannerReceiveController.receivedTextAvailable
                    onClicked: scannerReceiveController.copyReceivedText()
                }

                AppButton {
                    text: root.visibility === Window.FullScreen ? "Window" : "Full"
                    enabled: modeTabs.currentIndex === 0 ? sendController.frameCount > 0 : true
                    onClicked: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width < 820 ? 1 : 3
                columnSpacing: 10
                rowSpacing: 8

                // 串口选择（扫描器模式）
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: root.scannerMode && modeTabs.currentIndex === 1

                    Label {
                        text: "Port"
                        color: root.mutedColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 48
                    }

                    ComboBox {
                        id: portSelector
                        Layout.fillWidth: true
                        model: scannerReceiveController.availablePorts
                        onActivated: function(index) {
                            scannerReceiveController.selectPort(model[index])
                        }
                    }

                    AppButton {
                        width: 64
                        text: "Refresh"
                        onClicked: scannerReceiveController.refreshPorts()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: "Speed"
                        color: root.mutedColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 48
                    }

                    ComboBox {
                        id: speedSelector
                        Layout.fillWidth: true
                        model: root.speedModeLabels
                        currentIndex: sendController.speedMode

                        onActivated: function(index) {
                            currentIndex = index
                            root.setTransferSpeedMode(index)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: (modeTabs.currentIndex === 1 && !root.scannerMode) || sendController.feedbackScanning

                    Label {
                        text: "Camera"
                        color: root.mutedColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 48
                    }

                    ComboBox {
                        id: cameraSelector
                        Layout.fillWidth: true
                        enabled: receiveMediaDevices.videoInputs.length > 0
                        model: receiveMediaDevices.videoInputs
                        textRole: "description"
                        displayText: root.receiveCameraName(currentIndex)

                        Component.onCompleted: root.selectReceiveCamera()

                        onActivated: function(index) {
                            var wasReceiveScanning = receiveController.scanning
                            var wasFeedbackScanning = sendController.feedbackScanning
                            if (wasReceiveScanning) {
                                receiveController.stopScanning()
                            }
                            if (wasFeedbackScanning) {
                                sendController.stopFeedbackScan()
                            }

                            currentIndex = index
                            root.selectReceiveCamera()

                            if (wasReceiveScanning) {
                                Qt.callLater(receiveController.startScanning)
                            }
                            if (wasFeedbackScanning) {
                                Qt.callLater(sendController.startFeedbackScan)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        color: root.inkColor
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                        text: modeTabs.currentIndex === 0 ? sendController.fileName : (root.scannerMode ? scannerReceiveController.fileName : receiveController.fileName)
                    }

                    InfoPill {
                        text: modeTabs.currentIndex === 0
                            ? (sendController.frameCount > 0 ? (sendController.currentFrameIndex + 1) + " / " + sendController.frameCount : "0 / 0")
                            : (root.scannerMode
                                ? (scannerReceiveController.totalChunks > 0 ? scannerReceiveController.receivedChunks + " / " + scannerReceiveController.totalChunks : "0 / 0")
                                : (receiveController.totalChunks > 0 ? receiveController.receivedChunks + " / " + receiveController.totalChunks : "0 / 0"))
                    }
                }
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: modeTabs.currentIndex

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.pagePadding
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: root.panelColor
                    border.color: root.lineColor
                    border.width: 1
                    clip: true

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 12
                        radius: 6
                        color: "#fbfcf8"
                        border.color: "#edf0ea"
                        visible: !sendController.feedbackScanning
                    }

                    Image {
                        anchors.centerIn: parent
                        width: Math.max(32, Math.min(parent.width, parent.height) - (Qt.platform.os === "android" ? 16 : 32))
                        height: width
                        fillMode: Image.PreserveAspectFit
                        smooth: false
                        mipmap: false
                        cache: false
                        visible: !sendController.feedbackScanning
                        source: sendController.frameCount > 0
                            ? "image://airgapqr/current/" + sendController.imageRevision
                            : ""
                    }

                    Label {
                        anchors.centerIn: parent
                        color: root.mutedColor
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        text: "No transfer loaded"
                        visible: !sendController.feedbackScanning && sendController.frameCount <= 0
                    }

                    VideoOutput {
                        id: sendFeedbackPreview
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectCrop
                        visible: sendController.feedbackScanning

                        Component.onCompleted: sendController.attachFeedbackVideoSink(videoSink)
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 52
                        color: "#cc111111"
                        visible: sendController.feedbackScanning

                        Label {
                            anchors.centerIn: parent
                            color: "#f6f6f2"
                            text: "Point camera at receiver feedback QR"
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                    }

                    InfoPill {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 14
                        visible: sendController.frameCount > 0 && !sendController.feedbackScanning
                        text: sendController.resendMode ? "Resend loop" : sendController.speedModeName
                        tone: sendController.resendMode ? "#fff0d8" : "#edf4ef"
                        textColor: sendController.resendMode ? "#705021" : root.inkColor
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? (root.width < 760 ? 126 : 104) : 0
                    radius: 8
                    color: root.panelColor
                    border.color: root.lineColor
                    visible: !sendController.playing && !sendController.feedbackScanning

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        TextArea {
                            id: sendTextInput
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            placeholderText: "Text"
                            wrapMode: TextEdit.Wrap
                            font.pixelSize: 14
                            background: Rectangle {
                                radius: 6
                                color: "#fbfcf8"
                                border.color: root.lineColor
                            }
                        }

                        AppButton {
                            width: root.width < 760 ? 104 : 132
                            Layout.alignment: Qt.AlignVCenter
                            text: "Prepare Text"
                            primary: true
                            enabled: sendTextInput.text.length > 0
                            onClicked: sendController.prepareText(sendTextInput.text)
                        }
                    }
                }

                StatusStrip {
                    text: sendController.status
                    alert: sendController.status.indexOf("denied") >= 0
                        || sendController.status.indexOf("rejected") >= 0
                        || sendController.status.indexOf("failed") >= 0
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.pagePadding
                spacing: 12

                // 摄像头模式显示
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: "#141816"
                    border.color: "#2b312e"
                    border.width: 1
                    clip: true
                    visible: !root.scannerMode

                    VideoOutput {
                        id: receivePreview
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectCrop
                        visible: !receiveController.feedbackVisible
                            && (receiveController.scanning || (root.androidCameraHot && modeTabs.currentIndex === 1))

                        Component.onCompleted: {
                            receiveController.attachVideoSink(videoSink)
                            if (startupAutoScan) {
                                Qt.callLater(receiveController.startScanning)
                            }
                        }
                    }

                    Image {
                        anchors.centerIn: parent
                        width: Math.max(32, Math.min(parent.width, parent.height) - (Qt.platform.os === "android" ? 16 : 32))
                        height: width
                        fillMode: Image.PreserveAspectFit
                        smooth: false
                        mipmap: false
                        cache: false
                        visible: receiveController.feedbackVisible
                        source: receiveController.feedbackVisible
                            ? "image://airgapfeedback/current/" + receiveController.feedbackImageRevision
                            : ""
                    }

                    Label {
                        anchors.centerIn: parent
                        color: "#f6f6f2"
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        text: receiveController.cameraAvailable
                            ? (receiveController.scanning || receiveController.feedbackVisible ? "" : "Camera ready")
                            : "No camera available"
                        visible: text.length > 0
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 58
                        color: "#cc111111"
                        visible: receiveController.scanning || receiveController.feedbackVisible || receiveController.completed

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12

                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: receiveController.progress
                            }

                            Label {
                                color: "#f6f6f2"
                                font.pixelSize: 13
                                text: receiveController.totalChunks > 0
                                    ? receiveController.receivedChunks + " / " + receiveController.totalChunks
                                    : "0 / 0"
                            }
                        }
                    }
                }

                // 扫描器模式显示
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: root.panelColor
                    border.color: root.lineColor
                    border.width: 1
                    clip: true
                    visible: root.scannerMode

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Label {
                            text: "Scanner Mode"
                            color: root.inkColor
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: "Status: " + scannerReceiveController.status
                            color: root.mutedColor
                            font.pixelSize: 13
                        }

                        Label {
                            text: "Received frames: " + scannerReceiveController.acceptedFrameCount + " / " + scannerReceiveController.receivedFrameCount
                            color: root.mutedColor
                            font.pixelSize: 13
                            visible: scannerReceiveController.receivedFrameCount > 0
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 1
                            value: scannerReceiveController.progress
                            visible: scannerReceiveController.totalChunks > 0
                        }

                        Label {
                            text: "Progress: " + (scannerReceiveController.progress * 100).toFixed(1) + "%"
                            color: root.mutedColor
                            font.pixelSize: 13
                            visible: scannerReceiveController.totalChunks > 0
                        }

                        Label {
                            text: "Error: " + scannerReceiveController.lastError
                            color: root.dangerColor
                            font.pixelSize: 13
                            visible: scannerReceiveController.lastError.length > 0
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                StatusStrip {
                    text: root.scannerMode ? scannerReceiveController.status : receiveController.status
                    alert: root.scannerMode ? scannerReceiveController.lastError.length > 0 : receiveController.lastError.length > 0
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 112
                    radius: 8
                    color: root.panelColor
                    border.color: root.lineColor
                    visible: root.scannerMode ? scannerReceiveController.receivedTextAvailable : receiveController.receivedTextAvailable

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        TextArea {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            readOnly: true
                            wrapMode: TextEdit.Wrap
                            text: root.scannerMode ? scannerReceiveController.receivedText : receiveController.receivedText
                            font.pixelSize: 14
                            background: Rectangle {
                                radius: 6
                                color: "#fbfcf8"
                                border.color: root.lineColor
                            }
                        }

                        AppButton {
                            width: 96
                            Layout.alignment: Qt.AlignVCenter
                            text: "Copy"
                            primary: true
                            onClicked: root.scannerMode ? scannerReceiveController.copyReceivedText() : receiveController.copyReceivedText()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.width < 760 ? 118 : 78
                    radius: 8
                    color: root.panelColor
                    border.color: root.lineColor
                    visible: !root.scannerMode

                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: root.width > 900 ? 6 : 3
                        columnSpacing: 12
                        rowSpacing: 6

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Video " + receiveController.videoFrameCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Attempts " + receiveController.decodeAttemptCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Failures " + receiveController.decodeFailureCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Decoded " + receiveController.decodedQrFrameCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Accepted " + receiveController.acceptedQrFrameCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Rejected " + receiveController.rejectedQrFrameCount
                        }
                    }
                }

                // 扫描器模式统计
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.width < 760 ? 118 : 78
                    radius: 8
                    color: root.panelColor
                    border.color: root.lineColor
                    visible: root.scannerMode

                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: root.width > 900 ? 4 : 2
                        columnSpacing: 12
                        rowSpacing: 6

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Received " + scannerReceiveController.receivedFrameCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Accepted " + scannerReceiveController.acceptedFrameCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Rejected " + scannerReceiveController.rejectedFrameCount
                        }

                        Label {
                            Layout.fillWidth: true
                            color: root.mutedColor
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            text: "Port: " + scannerReceiveController.selectedPort
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    color: "#80382f"
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    visible: root.scannerMode ? scannerReceiveController.lastError.length > 0 : receiveController.lastError.length > 0
                    text: "Last error: " + (root.scannerMode ? scannerReceiveController.lastError : receiveController.lastError)
                }

                Label {
                    Layout.fillWidth: true
                    color: root.mutedColor
                    elide: Text.ElideMiddle
                    font.pixelSize: 12
                    visible: root.scannerMode ? scannerReceiveController.lastSavedPath.length > 0 : receiveController.lastSavedPath.length > 0
                    text: "Saved: " + (root.scannerMode ? scannerReceiveController.lastSavedPath : receiveController.lastSavedPath)
                }
            }
        }
    }
}
