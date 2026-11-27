// SPDX-License-Identifier: MIT
package com.lifegps.app.prod

import android.graphics.Color
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.lifegps.core.model.CoordSystem
import com.lifegps.core.model.CoordTransform
import com.lifegps.core.model.LatLng as CoreLatLng
import com.lifegps.core.protocol.TrajectoryRecord
import java.util.Locale
import org.maplibre.android.MapLibre
import org.maplibre.android.camera.CameraPosition
import org.maplibre.android.camera.CameraUpdateFactory
import org.maplibre.android.geometry.LatLng
import org.maplibre.android.geometry.LatLngBounds
import org.maplibre.android.maps.MapLibreMap
import org.maplibre.android.maps.MapView
import org.maplibre.android.maps.Style
import org.maplibre.android.style.expressions.Expression
import org.maplibre.android.style.layers.CircleLayer
import org.maplibre.android.style.layers.LineLayer
import org.maplibre.android.style.layers.Property
import org.maplibre.android.style.layers.PropertyFactory
import org.maplibre.android.style.sources.GeoJsonSource

private const val SRC_SEGMENTS = "lgps-track-segments"
private const val SRC_POIS = "lgps-track-pois"
private const val SRC_CURSOR = "lgps-track-cursor"

private const val LAYER_SEGMENTS = "lgps-layer-segments"
private const val LAYER_POIS = "lgps-layer-pois"
private const val LAYER_CURSOR = "lgps-layer-cursor"

/**
 * Paired map tile source and display coordinate system.
 *
 * Track storage and exports remain strictly WGS-84; display overlays are projected
 * automatically according to [usesGcj02] so trajectories never drift off roads.
 */
enum class MapTileProvider(
    val prefKey: String,
    val label: String,
    val usesGcj02: Boolean,
    val tileUrls: List<String>,
    val attribution: String,
) {
    AMAP_VECTOR(
        prefKey = "amap_vector",
        label = "高德标准 (GCJ-02)",
        usesGcj02 = true,
        tileUrls = listOf(
            "https://webrd01.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}",
            "https://webrd02.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}",
            "https://webrd03.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}",
            "https://webrd04.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}",
        ),
        attribution = "© AutoNavi",
    ),
    AMAP_SATELLITE(
        prefKey = "amap_sat",
        label = "高德卫星 (GCJ-02)",
        usesGcj02 = true,
        tileUrls = listOf(
            "https://webst01.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}",
            "https://webst02.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}",
        ),
        attribution = "© AutoNavi Satellite",
    ),
    OSM_CARTO(
        prefKey = "osm_carto",
        label = "Carto / OSM (国际 WGS-84)",
        usesGcj02 = false,
        tileUrls = listOf(
            "https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}@2x.png",
            "https://b.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}@2x.png",
            "https://c.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}@2x.png",
        ),
        attribution = "© OpenStreetMap © CARTO",
    ),
    ;

    val coordSystem: CoordSystem
        get() = if (usesGcj02) CoordSystem.GCJ02 else CoordSystem.WGS84

    companion object {
        fun fromPrefKey(key: String?): MapTileProvider =
            entries.firstOrNull { it.prefKey == key } ?: AMAP_VECTOR
    }
}

private fun escapeJson(value: String): String =
    value.replace("\\", "\\\\").replace("\"", "\\\"")

/**
 * Builds a MapLibre raster style JSON dynamically for the given [MapTileProvider]
 * (zero GMS, zero API key, automatic disk caching via MapLibre Native's SQLite store).
 */
private fun buildBasemapStyleJson(provider: MapTileProvider): String {
    val tilesArray = provider.tileUrls.joinToString(",\n        ") { url ->
        "\"${escapeJson(url)}\""
    }
    val attribution = escapeJson(provider.attribution)
    val styleName = escapeJson("LifeGPS ${provider.prefKey}")
    return """
    {
      "version": 8,
      "name": "$styleName",
      "sources": {
        "basemap-raster": {
          "type": "raster",
          "tiles": [
            $tilesArray
          ],
          "tileSize": 256,
          "maxzoom": 19,
          "attribution": "$attribution"
        }
      },
      "layers": [
        {
          "id": "background",
          "type": "background",
          "paint": { "background-color": "#0F172A" }
        },
        {
          "id": "basemap-layer",
          "type": "raster",
          "source": "basemap-raster",
          "minzoom": 0,
          "maxzoom": 20
        }
      ]
    }
    """.trimIndent()
}

/**
 * Interactive MapLibre Native Android map rendering WGS-84 trajectories with coordinate
 * projection automatically paired to [tileProvider] ([MapTileProvider.usesGcj02]),
 * mode-colored segments (GNSS / PDR / Vehicle / Still), POI markers, and a live / playback cursor.
 */
@Composable
fun MapLibreTrackView(
    records: List<TrajectoryRecord>,
    fallbackTrail: List<CoreLatLng>,
    cursorPoint: CoreLatLng?,
    cameraResetKey: Any,
    modifier: Modifier = Modifier,
    tileProvider: MapTileProvider = MapTileProvider.AMAP_VECTOR,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    remember {
        MapLibre.getInstance(context.applicationContext)
    }

    val mapView = remember {
        MapView(context).apply { onCreate(null) }
    }
    var mapRef by remember { mutableStateOf<MapLibreMap?>(null) }
    var styleReady by remember { mutableStateOf(false) }
    var disposed by remember { mutableStateOf(false) }
    var lastCameraKey by remember { mutableStateOf<Pair<Any, Boolean>?>(null) }

    DisposableEffect(lifecycleOwner, mapView) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_START -> mapView.onStart()
                Lifecycle.Event.ON_RESUME -> mapView.onResume()
                Lifecycle.Event.ON_PAUSE -> mapView.onPause()
                Lifecycle.Event.ON_STOP -> mapView.onStop()
                Lifecycle.Event.ON_DESTROY -> mapView.onDestroy()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            disposed = true
            lifecycleOwner.lifecycle.removeObserver(observer)
            mapView.onStop()
            mapView.onDestroy()
        }
    }

    val usesGcj02 = tileProvider.usesGcj02
    val segmentsJson = remember(records, fallbackTrail, usesGcj02) {
        buildSegmentsGeoJson(records, fallbackTrail, usesGcj02)
    }
    val poisJson = remember(records, usesGcj02) {
        buildPoisGeoJson(records, usesGcj02)
    }
    val cursorJson = remember(cursorPoint, usesGcj02) {
        buildCursorGeoJson(cursorPoint, usesGcj02)
    }

    val latestSegmentsJson by rememberUpdatedState(segmentsJson)
    val latestPoisJson by rememberUpdatedState(poisJson)
    val latestCursorJson by rememberUpdatedState(cursorJson)
    val latestTileProvider by rememberUpdatedState(tileProvider)

    LaunchedEffect(mapRef, tileProvider) {
        val map = mapRef ?: return@LaunchedEffect
        styleReady = false
        val expectedProvider = tileProvider
        val styleJson = buildBasemapStyleJson(expectedProvider)
        map.setStyle(Style.Builder().fromJson(styleJson)) { style ->
            if (disposed || latestTileProvider != expectedProvider) return@setStyle
            style.addSource(GeoJsonSource(SRC_SEGMENTS, latestSegmentsJson))
            style.addSource(GeoJsonSource(SRC_POIS, latestPoisJson))
            style.addSource(GeoJsonSource(SRC_CURSOR, latestCursorJson))

            val segLayer = LineLayer(LAYER_SEGMENTS, SRC_SEGMENTS).withProperties(
                PropertyFactory.lineCap(Property.LINE_CAP_ROUND),
                PropertyFactory.lineJoin(Property.LINE_JOIN_ROUND),
                PropertyFactory.lineWidth(4.5f),
                PropertyFactory.lineColor(
                    Expression.match(
                        Expression.get("mode"),
                        Expression.color(Color.parseColor("#00E5FF")),
                        Expression.stop("pdr", Expression.color(Color.parseColor("#FFB300"))),
                        Expression.stop("vehicle", Expression.color(Color.parseColor("#AB47BC"))),
                        Expression.stop("still", Expression.color(Color.parseColor("#90A4AE"))),
                    ),
                ),
            )
            style.addLayer(segLayer)

            val poiLayer = CircleLayer(LAYER_POIS, SRC_POIS).withProperties(
                PropertyFactory.circleRadius(7f),
                PropertyFactory.circleColor(Color.parseColor("#00E676")),
                PropertyFactory.circleStrokeWidth(2f),
                PropertyFactory.circleStrokeColor(Color.WHITE),
            )
            style.addLayer(poiLayer)

            val cursorLayer = CircleLayer(LAYER_CURSOR, SRC_CURSOR).withProperties(
                PropertyFactory.circleRadius(8f),
                PropertyFactory.circleColor(Color.parseColor("#FF5252")),
                PropertyFactory.circleStrokeWidth(2.5f),
                PropertyFactory.circleStrokeColor(Color.WHITE),
            )
            style.addLayer(cursorLayer)

            styleReady = true
        }
    }

    LaunchedEffect(mapRef, styleReady, segmentsJson, poisJson, cursorJson) {
        val map = mapRef ?: return@LaunchedEffect
        if (!styleReady) return@LaunchedEffect
        val style = map.style ?: return@LaunchedEffect
        style.getSourceAs<GeoJsonSource>(SRC_SEGMENTS)?.setGeoJson(segmentsJson)
        style.getSourceAs<GeoJsonSource>(SRC_POIS)?.setGeoJson(poisJson)
        style.getSourceAs<GeoJsonSource>(SRC_CURSOR)?.setGeoJson(cursorJson)
    }

    LaunchedEffect(mapRef, styleReady, cameraResetKey, usesGcj02) {
        val map = mapRef ?: return@LaunchedEffect
        if (!styleReady) return@LaunchedEffect
        val desiredKey = cameraResetKey to usesGcj02
        if (lastCameraKey == desiredKey) return@LaunchedEffect
        val pts = collectDisplayPoints(records, fallbackTrail, cursorPoint, usesGcj02)
        if (pts.isNotEmpty()) {
            lastCameraKey = desiredKey
        }
        if (pts.size >= 2) {
            val builder = LatLngBounds.Builder()
            pts.forEach { builder.include(LatLng(it.latitude, it.longitude)) }
            val bounds = builder.build()
            if (bounds.latitudeSpan > 1e-5 || bounds.longitudeSpan > 1e-5) {
                map.animateCamera(CameraUpdateFactory.newLatLngBounds(bounds, 80), 400)
            } else {
                map.animateCamera(
                    CameraUpdateFactory.newCameraPosition(
                        CameraPosition.Builder().target(bounds.center).zoom(16.0).build(),
                    ),
                    400,
                )
            }
        } else if (pts.size == 1) {
            val p = pts.first()
            map.animateCamera(
                CameraUpdateFactory.newCameraPosition(
                    CameraPosition.Builder()
                        .target(LatLng(p.latitude, p.longitude))
                        .zoom(16.0)
                        .build(),
                ),
                400,
            )
        }
    }

    AndroidView(
        factory = {
            mapView.getMapAsync { map ->
                if (!disposed) {
                    mapRef = map
                }
            }
            mapView
        },
        modifier = modifier,
    )
}

private fun projectForTile(lat: Double, lon: Double, usesGcj02: Boolean): CoreLatLng =
    if (usesGcj02) CoordTransform.wgs84ToGcj02(lat, lon) else CoreLatLng(lat, lon)

private fun modeKey(rec: TrajectoryRecord): String = when {
    rec.isVehicle -> "vehicle"
    rec.isPdrSource -> "pdr"
    rec.isStationaryLocked -> "still"
    else -> "gnss"
}

private fun collectDisplayPoints(
    records: List<TrajectoryRecord>,
    fallbackTrail: List<CoreLatLng>,
    cursorPoint: CoreLatLng?,
    usesGcj02: Boolean,
): List<CoreLatLng> {
    val valid = records.filter { it.isFixValid }
    if (valid.isNotEmpty()) {
        return valid.map { projectForTile(it.latitudeDeg, it.longitudeDeg, usesGcj02) }
    }
    if (fallbackTrail.isNotEmpty()) {
        return fallbackTrail.map { projectForTile(it.latitude, it.longitude, usesGcj02) }
    }
    return cursorPoint?.let { listOf(projectForTile(it.latitude, it.longitude, usesGcj02)) } ?: emptyList()
}

private fun buildSegmentsGeoJson(
    records: List<TrajectoryRecord>,
    fallbackTrail: List<CoreLatLng>,
    usesGcj02: Boolean,
): String {
    val sb = StringBuilder(2048)
    sb.append("{\"type\":\"FeatureCollection\",\"features\":[")
    val valid = records.filter { it.isFixValid }
    var firstFeature = true

    if (valid.size >= 2) {
        var segMode = modeKey(valid[0])
        val segCoords = ArrayList<CoreLatLng>()
        segCoords.add(projectForTile(valid[0].latitudeDeg, valid[0].longitudeDeg, usesGcj02))

        for (i in 1 until valid.size) {
            val pt = projectForTile(valid[i].latitudeDeg, valid[i].longitudeDeg, usesGcj02)
            val nextMode = modeKey(valid[i])
            segCoords.add(pt)
            if (nextMode != segMode || i == valid.lastIndex) {
                if (!firstFeature) sb.append(',')
                appendLineFeature(sb, segMode, segCoords)
                firstFeature = false
                segMode = nextMode
                segCoords.clear()
                segCoords.add(pt)
            }
        }
    } else if (fallbackTrail.size >= 2) {
        val pts = fallbackTrail.map { projectForTile(it.latitude, it.longitude, usesGcj02) }
        appendLineFeature(sb, "gnss", pts)
    }

    sb.append("]}")
    return sb.toString()
}

private fun appendLineFeature(sb: StringBuilder, mode: String, pts: List<CoreLatLng>) {
    sb.append("{\"type\":\"Feature\",\"properties\":{\"mode\":\"")
    sb.append(mode)
    sb.append("\"},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[")
    pts.forEachIndexed { idx, p ->
        if (idx > 0) sb.append(',')
        sb.append(String.format(Locale.US, "[%.7f,%.7f]", p.longitude, p.latitude))
    }
    sb.append("]}}")
}

private fun buildPoisGeoJson(records: List<TrajectoryRecord>, usesGcj02: Boolean): String {
    val sb = StringBuilder(512)
    sb.append("{\"type\":\"FeatureCollection\",\"features\":[")
    var first = true
    for (r in records) {
        if (!r.isPoi) continue
        val p = projectForTile(r.latitudeDeg, r.longitudeDeg, usesGcj02)
        if (!first) sb.append(',')
        sb.append(
            String.format(
                Locale.US,
                "{\"type\":\"Feature\",\"properties\":{\"seqId\":%d},\"geometry\":{\"type\":\"Point\",\"coordinates\":[%.7f,%.7f]}}",
                r.seqId,
                p.longitude,
                p.latitude,
            ),
        )
        first = false
    }
    sb.append("]}")
    return sb.toString()
}

private fun buildCursorGeoJson(cursor: CoreLatLng?, usesGcj02: Boolean): String {
    if (cursor == null) return "{\"type\":\"FeatureCollection\",\"features\":[]}"
    val p = projectForTile(cursor.latitude, cursor.longitude, usesGcj02)
    return String.format(
        Locale.US,
        "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"properties\":{},\"geometry\":{\"type\":\"Point\",\"coordinates\":[%.7f,%.7f]}}]}",
        p.longitude,
        p.latitude,
    )
}
