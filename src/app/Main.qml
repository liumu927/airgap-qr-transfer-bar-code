import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 960
    height: 640
    visible: true
    title: "AirGap QR Transfer"
    color: "#f6f6f2"
    property var selectedReceiveCamera: receiveMediaDevices.defaultVideoInput
    property bool receiveCameraInitialized: false
    property var speedModeLabels: ["Safe", "Balanced", "Fast"]

    Component.onCompleted: {
        modeTabs.currentIndex = startupMode
        if (startupFullScreen) {
            root.visibility = Window.FullScreen
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

    MediaDevices {
        id: receiveMediaDevices

        onVideoInputsChanged: root.selectReceiveCamera()
    }

    Camera {
        id: receiveCamera
        cameraDevice: root.selectedReceiveCamera
        active: receiveController.scanning || sendController.feedbackScanning
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
        ColumnLayout {
            anchors.fill: parent
            spacing: 4

            TabBar {
                id: modeTabs
                Layout.fillWidth: true

                TabButton { text: "Send" }
                TabButton { text: "Receive" }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    visible: modeTabs.currentIndex === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: "Open"
                    onClicked: Qt.platform.os === "android" ? sendController.chooseOpenFile() : openDialog.open()
                }

                Button {
                    visible: modeTabs.currentIndex === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: "Back"
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.previousFrame()
                }

                Button {
                    visible: modeTabs.currentIndex === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: sendController.playing ? "Pause" : "Play"
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.playing ? sendController.stopPlayback() : sendController.startPlayback()
                }

                Button {
                    visible: modeTabs.currentIndex === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: "Next"
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.nextFrame()
                }

                Button {
                    visible: modeTabs.currentIndex === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 72
                    text: sendController.feedbackScanning ? "Stop FB" : "Scan FB"
                    enabled: sendController.frameCount > 0
                    onClicked: sendController.feedbackScanning ? sendController.stopFeedbackScan() : sendController.startFeedbackScan()
                }

                Button {
                    visible: modeTabs.currentIndex === 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 64
                    text: "All"
                    enabled: sendController.resendMode
                    onClicked: sendController.clearResendFilter()
                }

                Button {
                    visible: modeTabs.currentIndex === 1
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: receiveController.scanning ? "Stop" : "Scan"
                    enabled: receiveController.cameraAvailable || receiveController.scanning
                    onClicked: receiveController.scanning ? receiveController.stopScanning() : receiveController.startScanning()
                }

                Button {
                    visible: modeTabs.currentIndex === 1
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: "Reset"
                    onClicked: receiveController.reset()
                }

                Button {
                    visible: modeTabs.currentIndex === 1
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    text: receiveController.feedbackVisible ? "Hide FB" : "Feedback"
                    enabled: receiveController.feedbackVisible || receiveController.feedbackAvailable
                    onClicked: receiveController.feedbackVisible ? receiveController.hideFeedbackQr() : receiveController.showFeedbackQr()
                }

                Button {
                    visible: modeTabs.currentIndex === 1
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: "Save"
                    enabled: receiveController.completed
                    onClicked: Qt.platform.os === "android" ? receiveController.chooseSaveLocation() : saveDialog.open()
                }

                Button {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 56
                    text: root.visibility === Window.FullScreen ? "Window" : "Full"
                    enabled: modeTabs.currentIndex === 0 ? sendController.frameCount > 0 : true
                    onClicked: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "Speed"
                    Layout.minimumWidth: 64
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

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    text: modeTabs.currentIndex === 0 ? sendController.fileName : receiveController.fileName
                }

                Label {
                    visible: modeTabs.currentIndex === 0
                    text: sendController.frameCount > 0
                        ? (sendController.currentFrameIndex + 1) + " / " + sendController.frameCount
                        : "0 / 0"
                }

                Label {
                    visible: modeTabs.currentIndex === 1
                    text: receiveController.totalChunks > 0
                        ? receiveController.receivedChunks + " / " + receiveController.totalChunks
                        : "0 / 0"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: modeTabs.currentIndex === 1

                Label {
                    text: "Camera"
                    Layout.minimumWidth: 64
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
                        var wasScanning = receiveController.scanning
                        if (wasScanning) {
                            receiveController.stopScanning()
                        }

                        currentIndex = index
                        root.selectReceiveCamera()

                        if (wasScanning) {
                            Qt.callLater(receiveController.startScanning)
                        }
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
                anchors.margins: 16
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "white"
                    border.color: "#d0d0c8"
                    border.width: 1

                    Image {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, parent.height - 48)
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

                    VideoOutput {
                        id: sendFeedbackPreview
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectCrop
                        visible: sendController.feedbackScanning

                        Component.onCompleted: sendController.attachFeedbackVideoSink(videoSink)
                    }

                    Label {
                        anchors.centerIn: parent
                        color: "#f6f6f2"
                        text: "Point camera at receiver feedback QR"
                        visible: sendController.feedbackScanning
                        background: Rectangle {
                            color: "#111111"
                            opacity: 0.88
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 92
                    spacing: 8

                    TextArea {
                        id: sendTextInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 84
                        placeholderText: "Text"
                        wrapMode: TextEdit.Wrap
                    }

                    Button {
                        Layout.preferredWidth: 128
                        Layout.alignment: Qt.AlignVCenter
                        text: "Prepare Text"
                        enabled: sendTextInput.text.length > 0
                        onClicked: sendController.prepareText(sendTextInput.text)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: sendController.status
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#111111"
                    border.color: "#2c2c2c"
                    border.width: 1

                    VideoOutput {
                        id: receivePreview
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectCrop
                        visible: receiveController.scanning && !receiveController.feedbackVisible

                        Component.onCompleted: {
                            receiveController.attachVideoSink(videoSink)
                            if (startupAutoScan) {
                                Qt.callLater(receiveController.startScanning)
                            }
                        }
                    }

                    Image {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, parent.height - 48)
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
                        text: receiveController.cameraAvailable
                            ? (receiveController.scanning || receiveController.feedbackVisible ? "" : "Camera ready")
                            : "No camera available"
                        visible: text.length > 0
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: receiveController.progress
                }

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: receiveController.status
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 112
                    spacing: 8
                    visible: receiveController.receivedTextAvailable

                    TextArea {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 104
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        text: receiveController.receivedText
                    }

                    Button {
                        Layout.preferredWidth: 96
                        Layout.alignment: Qt.AlignVCenter
                        text: "Copy"
                        onClicked: receiveController.copyReceivedText()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 720 ? 3 : 2
                    columnSpacing: 16
                    rowSpacing: 4

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: "Video frames: " + receiveController.videoFrameCount
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: "Decode attempts: " + receiveController.decodeAttemptCount
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: "Decode failures: " + receiveController.decodeFailureCount
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: "QR decoded: " + receiveController.decodedQrFrameCount
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: "QR accepted: " + receiveController.acceptedQrFrameCount
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: "QR rejected: " + receiveController.rejectedQrFrameCount
                    }
                }

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    visible: receiveController.lastError.length > 0
                    text: "Last error: " + receiveController.lastError
                }

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pixelSize: 12
                    visible: receiveController.lastSavedPath.length > 0
                    text: "Saved: " + receiveController.lastSavedPath
                }
            }
        }
    }
}
