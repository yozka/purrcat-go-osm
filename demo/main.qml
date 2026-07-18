import QtQuick
import QtQuick.Controls
import QtPositioning
import QtLocation

Window {
	id: root
	width: 900
	height: 640
	visible: true
	title: "Кошачий навигатор 🐾"

	readonly property string apiBase: "http://127.0.0.1:8080"
	readonly property string osmTilesUrl: "https://tile.openstreetmap.org/%z/%x/%y.png"
	// ?v= busts Qt OSM disk cache after Reload
	property int tileRevision: 1
	readonly property string customTilesUrl: apiBase + "/tiles/%z/%x/%y.png?v=" + tileRevision
	readonly property string activeCustomCacheDir: customCacheDir + "/r" + tileRevision

	property bool useCustomServer: false
	property bool customMapReady: false
	readonly property bool showingCustom: useCustomServer && customMapReady

	property var mapCenter: QtPositioning.coordinate(45.2671, 19.8335) // Novi Sad
	// Как в игре: MapViewState kZoomLevelWide / kZoomLevelClose
	readonly property real zoomLevelWide: 16
	readonly property real zoomLevelClose: 18
	property bool closeZoom: false
	readonly property real mapZoom: closeZoom ? zoomLevelClose : zoomLevelWide
	property string statusText: "OpenStreetMap"
	property real loadProgress: 0
	property bool loadingRegion: false
	property string activeJobId: ""
	property string regionId: ""

	function apiGet(path, cb) {
		const xhr = new XMLHttpRequest()
		xhr.onreadystatechange = function () {
			if (xhr.readyState !== XMLHttpRequest.DONE)
				return
			cb(xhr.status, xhr.responseText)
		}
		xhr.open("GET", apiBase + path)
		xhr.send()
	}

	function requestEnsure() {
		const lat = mapCenter.latitude
		const lon = mapCenter.longitude
		apiGet("/regions/ensure?lat=" + lat + "&lon=" + lon, onEnsureResponse)
	}

	/// Переключение на Custom: только ensure при необходимости, без сброса кеша.
	function switchToCustom() {
		if (customMapReady) {
			updateStatusForView()
			return
		}
		loadingRegion = true
		loadProgress = 0
		statusText = "Проверка региона…"
		requestEnsure()
	}

	/// Reload: сброс серверных PNG + новый Qt tile-cache + пересоздание карты.
	function reloadRegion() {
		if (!useCustomServer)
			return
		loadingRegion = true
		customMapReady = false
		customMapLoader.active = false
		tileRevision += 1
		loadProgress = 0
		statusText = "Перезагрузка региона…"
		apiGet("/tiles/clear", function () {
			requestEnsure()
		})
	}

	function onEnsureResponse(status, body) {
		if (status === 0) {
			loadingRegion = false
			statusText = "Сервер недоступен: " + apiBase
			return
		}
		if (status < 200 || status >= 300) {
			loadingRegion = false
			statusText = "Ошибка ensure HTTP " + status
			return
		}
		let data
		try { data = JSON.parse(body) } catch (e) {
			loadingRegion = false
			statusText = "Некорректный JSON от сервера"
			return
		}
		regionId = data.regionId || ""
		activeJobId = data.jobId || ""
		loadProgress = Number(data.progress) || 0
		statusText = (data.message || data.status || "") + (regionId ? (" [" + regionId + "]") : "")
		if (data.status === "ready") {
			showCustomMap(regionId)
			return
		}
		if (data.status === "error") {
			loadingRegion = false
			statusText = "Ошибка: " + (data.error || data.message)
			return
		}
		if (activeJobId.length > 0)
			pollJobTimer.start()
		else
			pollLookupTimer.start()
	}

	function showCustomMap(id) {
		loadingRegion = false
		loadProgress = 1
		customMapReady = true
		statusText = "Custom [" + id + "] z" + (closeZoom ? "18" : "16")
		pollJobTimer.stop()
		pollLookupTimer.stop()
		if (!customMapLoader.active)
			customMapLoader.active = true
	}

	function updateStatusForView() {
		if (!useCustomServer) {
			statusText = "OpenStreetMap"
			return
		}
		if (!customMapReady) {
			statusText = "Custom: регион не готов"
			return
		}
		statusText = "Custom [" + regionId + "] z" + (closeZoom ? "18" : "16")
	}

	onCloseZoomChanged: {
		if (useCustomServer && customMapReady)
			updateStatusForView()
	}

	function pollJob() {
		if (activeJobId.length === 0) {
			pollLookupTimer.start()
			return
		}
		apiGet("/regions/jobs/" + activeJobId, function (status, body) {
			if (status === 0) {
				loadingRegion = false
				statusText = "Сервер недоступен"
				pollJobTimer.stop()
				return
			}
			let data
			try { data = JSON.parse(body) } catch (e) { return }
			loadProgress = Number(data.progress) || 0
			statusText = (data.message || data.status || "") + (data.regionId ? (" [" + data.regionId + "]") : "")
			if (data.status === "ready") {
				showCustomMap(data.regionId)
			} else if (data.status === "error") {
				loadingRegion = false
				pollJobTimer.stop()
				statusText = "Ошибка: " + (data.error || data.message)
			}
		})
	}

	onUseCustomServerChanged: {
		pollJobTimer.stop()
		pollLookupTimer.stop()
		if (useCustomServer) {
			switchToCustom()
		} else {
			loadingRegion = false
			loadProgress = 0
			statusText = "OpenStreetMap"
			osmMap.center = root.mapCenter
			osmMap.zoomLevel = root.mapZoom
			// кастомную карту не уничтожаем и кеш не чистим — только скрываем
		}
	}

	Timer {
		id: pollJobTimer
		interval: 700
		repeat: true
		onTriggered: root.pollJob()
	}

	Timer {
		id: pollLookupTimer
		interval: 1000
		repeat: true
		onTriggered: root.requestEnsure()
	}

	// --- OpenStreetMap ---
	Map {
		id: osmMap
		anchors.fill: parent
		visible: !root.showingCustom
		z: 1
		enabled: visible

		plugin: Plugin {
			name: "osm"
			PluginParameter { name: "osm.useragent"; value: "OsmDemo/0.1.0" }
			PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
			PluginParameter { name: "osm.mapping.custom.host"; value: root.osmTilesUrl }
			PluginParameter { name: "osm.mapping.custom.mapcopyright"; value: "© OpenStreetMap contributors" }
		}

		center: root.mapCenter
		zoomLevel: root.mapZoom
		minimumZoomLevel: root.zoomLevelWide
		maximumZoomLevel: root.zoomLevelClose

		Behavior on zoomLevel {
			NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
		}

		onCenterChanged: if (visible) root.mapCenter = osmMap.center

		Component.onCompleted: if (supportedMapTypes.length > 0)
			activeMapType = supportedMapTypes[supportedMapTypes.length - 1]
		onSupportedMapTypesChanged: if (supportedMapTypes.length > 0)
			activeMapType = supportedMapTypes[supportedMapTypes.length - 1]

		DragHandler {
			target: null
			enabled: osmMap.visible
			onTranslationChanged: (delta) => osmMap.pan(-delta.x, -delta.y)
		}
	}

	Component {
		id: customMapComponent

		Map {
			id: customMap
			anchors.fill: parent

			plugin: Plugin {
				name: "osm"
				PluginParameter { name: "osm.useragent"; value: "OsmDemo/0.1.0-custom" }
				PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
				PluginParameter { name: "osm.mapping.cache.directory"; value: root.activeCustomCacheDir }
				PluginParameter { name: "osm.mapping.custom.host"; value: root.customTilesUrl }
				PluginParameter { name: "osm.mapping.custom.mapcopyright"; value: "PurrCat custom tiles" }
			}

			center: root.mapCenter
			zoomLevel: root.mapZoom
			minimumZoomLevel: root.zoomLevelWide
			maximumZoomLevel: root.zoomLevelClose

			Behavior on zoomLevel {
				NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
			}

			onCenterChanged: root.mapCenter = customMap.center

			Component.onCompleted: {
				if (supportedMapTypes.length > 0)
					activeMapType = supportedMapTypes[supportedMapTypes.length - 1]
				console.log("Custom map mounted", root.customTilesUrl)
			}
			onSupportedMapTypesChanged: if (supportedMapTypes.length > 0)
				activeMapType = supportedMapTypes[supportedMapTypes.length - 1]

			DragHandler {
				target: null
				onTranslationChanged: (delta) => customMap.pan(-delta.x, -delta.y)
			}
		}
	}

	Loader {
		id: customMapLoader
		anchors.fill: parent
		z: 2
		active: false
		visible: root.showingCustom
		sourceComponent: customMapComponent
	}

	Rectangle {
		anchors.fill: parent
		visible: root.loadingRegion
		z: 20
		color: "#66000000"

		Column {
			anchors.centerIn: parent
			spacing: 12
			width: parent.width * 0.6

			Label {
				anchors.horizontalCenter: parent.horizontalCenter
				color: "white"
				font.pixelSize: 16
				text: "Загрузка карты региона…"
			}

			ProgressBar {
				width: parent.width
				from: 0
				to: 1
				value: root.loadProgress
			}

			Label {
				width: parent.width
				wrapMode: Text.WordWrap
				horizontalAlignment: Text.AlignHCenter
				color: "#eee"
				font.pixelSize: 12
				text: root.statusText
			}
		}
	}

	Button {
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.margins: 12
		z: 30
		text: root.useCustomServer ? "OSM" : "Custom"
		onClicked: root.useCustomServer = !root.useCustomServer
	}

	Button {
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.margins: 12
		anchors.rightMargin: 100
		z: 30
		visible: root.useCustomServer
		text: "Reload region"
		onClicked: root.reloadRegion()
	}

	Column {
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		anchors.margins: 16
		z: 30
		spacing: 8

		Button {
			width: 48
			height: 48
			text: "+"
			font.pixelSize: 22
			highlighted: root.closeZoom
			onClicked: root.closeZoom = true
		}
		Button {
			width: 48
			height: 48
			text: "−"
			font.pixelSize: 22
			highlighted: !root.closeZoom
			onClicked: root.closeZoom = false
		}
	}

	Label {
		anchors.left: parent.left
		anchors.bottom: parent.bottom
		anchors.margins: 10
		z: 30
		padding: 6
		color: "white"
		background: Rectangle {
			color: "#80000000"
			radius: 4
		}
		text: root.statusText
	}
}
