import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One folder of the video library (a lecture course, say): where it sits under
// the library folder, how much is watched, a button that resumes where you left
// off, its subfolders as tiles and its videos as a numbered list with progress.
Item {
    id: page

    property string rootPath   // the library folder this page belongs to
    property string path       // the folder shown (rootPath or a folder under it)
    property string rootName   // a Drive library folder's name (its path is only an id)
    readonly property bool isDrive: drive.isDrive(page.rootPath)
    property real bottomInset: 0
    signal message(string text)

    readonly property int rev: progress.revision  // re-evaluate progress bindings
    function entry(p) { return page.rev >= 0 ? progress.get(p) : ({}) }  // reading rev makes callers re-evaluate
    // A Drive video plays with the rest of its Drive folder as the playlist.
    function open(p) { desktop.openInPlayer(p, page.isDrive ? page.path : "", page.crumbs[page.crumbs.length - 1].name) }
    // A video's name without the extension (Drive paths are ids, so look it up).
    function titleOf(p) {
        const v = listing.videos.find(v => v.path === p)
        return v ? v.title : page.fileName(p).replace(/\.[^.]+$/, "")
    }
    function fileName(p) { return p.substring(p.replace(/\\/g, "/").lastIndexOf("/") + 1) }

    readonly property QtObject store: progress  // the context property (FolderListing has its own "progress")
    FolderListing {
        id: listing
        folder: page.path
        progress: page.store
        folderName: page.crumbs[page.crumbs.length - 1].name
    }
    onVisibleChanged: if (visible) listing.refresh()

    // Breadcrumb: the library folder, then each folder down to this one.
    readonly property var crumbs: {
        if (page.isDrive) {
            // Drive ids carry no names: the library folder's, then those learned while listing.
            const known = listing.videos.length >= 0 && !listing.loading ? drive.trail(page.rootPath, page.path) : []
            return [{ name: page.rootName || qsTr("Google Drive"), path: page.rootPath }].concat(known)
        }
        const norm = s => s.replace(/\\/g, "/").replace(/\/+$/, "")
        const root = norm(page.rootPath), here = norm(page.path)
        const list = [{ name: page.fileName(root) || root, path: root }]
        if (here.length > root.length && here.toLowerCase().startsWith(root.toLowerCase())) {
            let p = root
            for (const part of here.substring(root.length + 1).split("/")) {
                p += "/" + part
                list.push({ name: part, path: p })
            }
        }
        return list
    }

    readonly property string filterText: search.text.trim().toLowerCase()
    readonly property var shownVideos: listing.videos.filter(v => filterText.length === 0 || v.title.toLowerCase().includes(filterText))
    readonly property var shownFolders: listing.subfolders.filter(f => filterText.length === 0 || f.name.toLowerCase().includes(filterText))

    readonly property string next: page.rev >= 0 && listing.videos.length >= 0 ? listing.nextToWatch() : ""
    readonly property var nextEntry: next.length > 0 ? entry(next) : ({})
    readonly property real remaining: {
        let total = page.rev >= 0 ? 0 : 0
        for (const v of listing.videos) {
            const e = progress.get(v.path)
            if (e.watched || !(e.duration > 0)) continue
            total += e.duration - (e.position || 0)
        }
        return total
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + page.bottomInset + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            width: flick.width - 12
            spacing: 22

            // Where, what, how far along, and the one action.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: page.crumbs
                        Row {
                            id: crumb
                            required property var modelData
                            required property int index
                            spacing: 4
                            Text {
                                visible: crumb.index > 0
                                text: "›"
                                color: Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.caption
                            }
                            Text {
                                readonly property bool last: crumb.index === page.crumbs.length - 1
                                text: crumb.modelData.name
                                color: last ? Theme.textDim : Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.caption
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !parent.last
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: page.path = crumb.modelData.path
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: page.crumbs[page.crumbs.length - 1].name
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: Theme.largeTitle
                            font.weight: Font.DemiBold
                            font.letterSpacing: Theme.tracking(Theme.largeTitle)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: listing.loading && listing.videos.length === 0 ? qsTr("불러오는 중…")
                                : !listing.exists ? (listing.errorText || qsTr("폴더를 찾을 수 없습니다"))
                                : listing.videos.length === 0 ? qsTr("하위 폴더 %1개").arg(listing.subfolders.length)
                                : qsTr("동영상 %1개 · %2개 다 봄").arg(listing.videos.length).arg(listing.watchedCount)
                                  + (page.remaining > 60 ? qsTr(" · 남은 시간 약 %1").arg(Theme.formatClock(page.remaining)) : "")
                            color: Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.body
                        }
                    }
                    SearchField {
                        id: search
                        Layout.alignment: Qt.AlignBottom
                        implicitWidth: 200
                        placeholderText: qsTr("강의 찾기")
                    }
                    AppButton {
                        Layout.alignment: Qt.AlignBottom
                        visible: page.next.length > 0
                        primary: true
                        text: (page.nextEntry.position >= 5 ? qsTr("이어서 재생") : qsTr("재생")) + " · "
                              + page.titleOf(page.next)
                              + (page.nextEntry.position >= 5 ? qsTr(" %1부터").arg(Theme.formatClock(page.nextEntry.position)) : "")
                        implicitWidth: Math.min(360, contentItem.implicitWidth + 36)
                        onClicked: page.open(page.next)
                    }
                }
                // This folder's own videos, watched / all.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    visible: listing.videos.length > 0
                    height: 4
                    radius: 2
                    color: Theme.fill
                    Rectangle {
                        width: parent.width * listing.watchedCount / Math.max(1, listing.videos.length)
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                        Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                    }
                }
            }

            // Subfolders as tiles.
            ColumnLayout {
                Layout.fillWidth: true
                visible: page.shownFolders.length > 0
                spacing: 10
                Text {
                    text: qsTr("하위 폴더")
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.floor(width / 240))
                    columnSpacing: 12
                    rowSpacing: 12
                    Repeater {
                        model: page.shownFolders
                        AbstractButton {
                            id: tile
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 70
                            hoverEnabled: true
                            Accessible.name: modelData.name
                            onClicked: page.path = modelData.path
                            scale: down ? Theme.pressScale : 1
                            Behavior on scale { NumberAnimation { duration: Theme.fast } }
                            background: Rectangle {
                                radius: Theme.cardRadius
                                color: tile.hovered ? Theme.surfaceHover : Theme.surface
                                border.color: tile.visualFocus ? Theme.accentFocus : Theme.border
                                border.width: tile.visualFocus ? 2 : 1
                            }
                            contentItem: RowLayout {
                                spacing: 12
                                Icon {
                                    Layout.leftMargin: 16
                                    glyph: Theme.iconFolder
                                    size: 22
                                    color: Theme.accent
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.rightMargin: 16
                                    spacing: 5
                                    Text {
                                        Layout.fillWidth: true
                                        text: tile.modelData.name
                                        elide: Text.ElideRight
                                        color: Theme.text
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.body
                                        font.weight: Font.DemiBold
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 3
                                        radius: 1.5
                                        color: Theme.fill
                                        Rectangle {
                                            width: parent.width * tile.modelData.watchedCount / Math.max(1, tile.modelData.videoCount)
                                            height: parent.height
                                            radius: 1.5
                                            color: Theme.accent
                                        }
                                    }
                                    Text {
                                        text: tile.modelData.watchedCount === 0 ? qsTr("%1개 · 아직 안 봄").arg(tile.modelData.videoCount)
                                            : tile.modelData.watchedCount >= tile.modelData.videoCount ? qsTr("%1개 · 다 봄").arg(tile.modelData.videoCount)
                                            : qsTr("%1개 · %2개 다 봄").arg(tile.modelData.videoCount).arg(tile.modelData.watchedCount)
                                        color: Theme.textDim
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fine
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // The videos, numbered, in one grouped list.
            ColumnLayout {
                Layout.fillWidth: true
                visible: page.shownVideos.length > 0
                spacing: 10
                Text {
                    text: qsTr("동영상")
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.caption
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: rows.implicitHeight
                    radius: Theme.cardRadius
                    color: Theme.surface
                    border.color: Theme.border
                    clip: true

                    ColumnLayout {
                        id: rows
                        width: parent.width
                        spacing: 0
                        Repeater {
                            model: page.shownVideos
                            AbstractButton {
                                id: row
                                required property var modelData
                                required property int index
                                readonly property var e: page.entry(modelData.path)
                                readonly property bool watched: e.watched === true
                                readonly property bool isNext: modelData.path === page.next
                                readonly property real fraction: watched ? 1 : e.duration > 0 ? Math.min(1, (e.position || 0) / e.duration) : 0
                                Layout.fillWidth: true
                                implicitHeight: 74
                                hoverEnabled: true
                                Accessible.name: modelData.title
                                onClicked: page.open(modelData.path)
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: rowMenu.popup()
                                }
                                background: Rectangle {
                                    color: row.down ? Theme.pressed : row.hovered ? Theme.surfaceHover : row.isNext ? Theme.accentSoft : "transparent"
                                    Rectangle {  // hairline between rows, inset like a grouped list
                                        visible: row.index > 0
                                        x: 16
                                        width: parent.width - 16
                                        height: 1
                                        color: Theme.separator
                                    }
                                }
                                contentItem: RowLayout {
                                    spacing: 14
                                    Text {
                                        Layout.leftMargin: 16
                                        Layout.preferredWidth: 24
                                        horizontalAlignment: Text.AlignRight
                                        text: row.index + 1
                                        color: Theme.textDim
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.body
                                        font.features: { "tnum": 1 }
                                    }
                                    Item {
                                        Layout.preferredWidth: 96
                                        Layout.preferredHeight: 54
                                        ProductShadow { anchors.fill: parent }
                                        RoundedClip {
                                            anchors.fill: parent
                                            radius: Theme.radius
                                            Rectangle { anchors.fill: parent; color: Theme.mediaBackground }
                                            Image {
                                                anchors.fill: parent
                                                source: Theme.thumbnailUrl(row.modelData.path)
                                                sourceSize: Qt.size(192, 108)
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                opacity: row.watched ? 0.55 : 1
                                            }
                                            Rectangle {  // how far into it
                                                anchors.left: parent.left
                                                anchors.bottom: parent.bottom
                                                visible: row.fraction > 0 && !row.watched
                                                width: parent.width * row.fraction
                                                height: 3
                                                color: Theme.accent
                                            }
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.modelData.title
                                            elide: Text.ElideRight
                                            color: row.isNext ? Theme.accent : row.watched ? Theme.textDim : Theme.text
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.body
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            text: row.watched ? qsTr("다 봄")
                                                : (row.e.position || 0) >= 5 ? qsTr("%1까지 봄").arg(Theme.formatClock(row.e.position))
                                                                                 + (row.isNext ? qsTr(" · 이어서 재생") : "")
                                                : qsTr("안 봄")
                                            color: Theme.textDim
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.caption
                                        }
                                    }
                                    Text {
                                        visible: row.e.duration > 0
                                        text: Theme.formatClock(row.e.duration || 0)
                                        color: Theme.textDim
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.caption
                                        font.features: { "tnum": 1 }
                                    }
                                    // Watched mark; click to toggle.
                                    AbstractButton {
                                        id: check
                                        Layout.rightMargin: 16
                                        implicitWidth: 24
                                        implicitHeight: 24
                                        hoverEnabled: true
                                        Accessible.name: row.watched ? qsTr("안 본 것으로 표시") : qsTr("다 본 것으로 표시")
                                        onClicked: progress.setWatched(row.modelData.path, !row.watched)
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 600
                                        ToolTip.text: Accessible.name
                                        background: Rectangle {
                                            radius: 12
                                            color: row.watched ? Theme.accent : "transparent"
                                            border.width: row.watched ? 0 : 1.5
                                            border.color: check.hovered ? Theme.accent : Theme.textFaint
                                        }
                                        contentItem: Icon {
                                            glyph: Theme.iconCheck
                                            size: 11
                                            color: row.watched ? Theme.accentText : check.hovered ? Theme.accent : "transparent"
                                        }
                                    }
                                }
                                AppMenu {
                                    id: rowMenu
                                    AppMenuItem { text: qsTr("재생"); onTriggered: page.open(row.modelData.path) }
                                    AppMenuItem {
                                        text: row.watched ? qsTr("안 본 것으로 표시") : qsTr("다 본 것으로 표시")
                                        onTriggered: progress.setWatched(row.modelData.path, !row.watched)
                                    }
                                    AppMenuSeparator { visible: !page.isDrive }
                                    AppMenuItem {
                                        visible: !page.isDrive
                                        height: visible ? implicitHeight : 0
                                        text: qsTr("폴더에서 보기")
                                        onTriggered: desktop.revealFile(row.modelData.path)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Nothing to show.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 60
                visible: !listing.loading && page.shownVideos.length === 0 && page.shownFolders.length === 0
                spacing: 8
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: !listing.exists ? qsTr("폴더를 찾을 수 없습니다")
                        : page.filterText.length > 0 ? qsTr("찾는 강의가 없습니다") : qsTr("이 폴더에 동영상이 없습니다")
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: Theme.tagline
                    font.weight: Font.DemiBold
                    font.letterSpacing: Theme.tracking(Theme.tagline)
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: !listing.exists ? (page.isDrive ? (listing.errorText || qsTr("Google Drive를 읽을 수 없습니다. 연결을 확인하세요."))
                                                          : qsTr("Google Drive라면 Drive 데스크톱 앱이 실행 중인지 확인하세요."))
                                          : qsTr("mp4, mkv, avi, mov, webm 등의 동영상이 있는 폴더를 고르세요.")
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.body
                }
                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                    visible: page.isDrive && !listing.exists
                    primary: true
                    text: drive.connected ? qsTr("다시 시도") : qsTr("Google 계정 연결")
                    enabled: !drive.busy
                    onClicked: drive.connected ? listing.refresh() : drive.signIn()
                }
            }
        }
    }
}
