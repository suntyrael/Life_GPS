// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.TrajectoryRecord
import java.io.StringWriter
import java.io.Writer
import java.time.Instant
import java.time.format.DateTimeFormatter
import java.util.Locale

data class PoiExportMeta(
    val title: String,
    val note: String = "",
)

enum class ExportFormat(val extension: String, val mimeType: String, val label: String) {
    GPX("gpx", "application/gpx+xml", "GPX 1.1"),
    KML("kml", "application/vnd.google-earth.kml+xml", "KML 2.2"),
    GEOJSON("geojson", "application/geo+json", "GeoJSON"),
    CSV("csv", "text/csv", "CSV"),
}

/**
 * Streaming trajectory exporters for GPX 1.1, KML 2.2, GeoJSON, and CSV.
 *
 * Strictly emits WGS-84 coordinates (never GCJ-02) per product contract.
 */
object TrackExporter {

    private val isoUtc = DateTimeFormatter.ISO_INSTANT

    fun export(
        format: ExportFormat,
        trackName: String,
        records: List<TrajectoryRecord>,
        writer: Writer,
        poiMetaBySeq: Map<Long, PoiExportMeta> = emptyMap(),
    ) {
        when (format) {
            ExportFormat.GPX -> exportGpx(trackName, records, writer, poiMetaBySeq)
            ExportFormat.KML -> exportKml(trackName, records, writer, poiMetaBySeq)
            ExportFormat.GEOJSON -> exportGeoJson(trackName, records, writer, poiMetaBySeq)
            ExportFormat.CSV -> exportCsv(records, writer)
        }
        writer.flush()
    }

    fun toGpx(
        trackName: String,
        records: List<TrajectoryRecord>,
        poiMetaBySeq: Map<Long, PoiExportMeta> = emptyMap(),
    ): String = StringWriter().also { export(ExportFormat.GPX, trackName, records, it, poiMetaBySeq) }.toString()

    fun toKml(
        trackName: String,
        records: List<TrajectoryRecord>,
        poiMetaBySeq: Map<Long, PoiExportMeta> = emptyMap(),
    ): String = StringWriter().also { export(ExportFormat.KML, trackName, records, it, poiMetaBySeq) }.toString()

    fun toGeoJson(
        trackName: String,
        records: List<TrajectoryRecord>,
        poiMetaBySeq: Map<Long, PoiExportMeta> = emptyMap(),
    ): String = StringWriter().also { export(ExportFormat.GEOJSON, trackName, records, it, poiMetaBySeq) }.toString()

    private fun resolvePoiTitle(r: TrajectoryRecord, poiMetaBySeq: Map<Long, PoiExportMeta>): String =
        poiMetaBySeq[r.seqId]?.title?.trim()?.takeIf { it.isNotBlank() } ?: "POI #${r.seqId}"

    private fun resolvePoiNote(r: TrajectoryRecord, poiMetaBySeq: Map<Long, PoiExportMeta>): String =
        poiMetaBySeq[r.seqId]?.note?.trim().orEmpty()

    private fun exportGpx(
        trackName: String,
        records: List<TrajectoryRecord>,
        w: Writer,
        poiMetaBySeq: Map<Long, PoiExportMeta>,
    ) {
        val safeName = xmlEscape(trackName)
        w.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
        w.append("<gpx version=\"1.1\" creator=\"Life_GPS\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n")
        w.append("  <metadata><name>$safeName</name></metadata>\n")
        for (r in records) {
            if (!r.isPoi) continue
            val ts = isoUtc.format(Instant.ofEpochMilli(r.unixEpochMs))
            val title = xmlEscape(resolvePoiTitle(r, poiMetaBySeq))
            val note = resolvePoiNote(r, poiMetaBySeq)
            val desc = xmlEscape(if (note.isNotEmpty()) "$note (floor=${r.floorLevel})" else "floor=${r.floorLevel}")
            w.append(
                String.format(
                    Locale.US,
                    "  <wpt lat=\"%.7f\" lon=\"%.7f\"><ele>%d</ele><time>%s</time><name>%s</name><desc>%s</desc></wpt>\n",
                    r.latitudeDeg,
                    r.longitudeDeg,
                    r.altitudeM,
                    ts,
                    title,
                    desc,
                ),
            )
        }
        w.append("  <trk>\n    <name>$safeName</name>\n    <trkseg>\n")
        for (r in records) {
            if (!r.isFixValid) continue
            val ts = isoUtc.format(Instant.ofEpochMilli(r.unixEpochMs))
            w.append(
                String.format(
                    Locale.US,
                    "      <trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%d</ele><time>%s</time><hdop>%.1f</hdop><sat>%d</sat></trkpt>\n",
                    r.latitudeDeg,
                    r.longitudeDeg,
                    r.altitudeM,
                    ts,
                    r.hdop,
                    r.satellites,
                ),
            )
        }
        w.append("    </trkseg>\n  </trk>\n</gpx>\n")
    }

    private fun exportKml(
        trackName: String,
        records: List<TrajectoryRecord>,
        w: Writer,
        poiMetaBySeq: Map<Long, PoiExportMeta>,
    ) {
        val safeName = xmlEscape(trackName)
        w.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
        w.append("<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n")
        w.append("  <name>$safeName</name>\n")
        w.append("  <Placemark>\n    <name>$safeName</name>\n    <LineString>\n")
        w.append("      <altitudeMode>absolute</altitudeMode>\n      <coordinates>\n")
        for (r in records) {
            if (!r.isFixValid) continue
            w.append(
                String.format(
                    Locale.US,
                    "        %.7f,%.7f,%d\n",
                    r.longitudeDeg,
                    r.latitudeDeg,
                    r.altitudeM,
                ),
            )
        }
        w.append("      </coordinates>\n    </LineString>\n  </Placemark>\n")
        for (r in records) {
            if (!r.isPoi) continue
            val title = xmlEscape(resolvePoiTitle(r, poiMetaBySeq))
            val note = resolvePoiNote(r, poiMetaBySeq)
            w.append("  <Placemark>\n")
            w.append("    <name>$title</name>\n")
            if (note.isNotEmpty()) {
                w.append("    <description>${xmlEscape(note)}</description>\n")
            }
            w.append(
                String.format(
                    Locale.US,
                    "    <Point><coordinates>%.7f,%.7f,%d</coordinates></Point>\n",
                    r.longitudeDeg,
                    r.latitudeDeg,
                    r.altitudeM,
                ),
            )
            w.append("  </Placemark>\n")
        }
        w.append("</Document>\n</kml>\n")
    }

    private fun exportGeoJson(
        trackName: String,
        records: List<TrajectoryRecord>,
        w: Writer,
        poiMetaBySeq: Map<Long, PoiExportMeta>,
    ) {
        val valid = records.filter { it.isFixValid }
        val pois = records.filter { it.isPoi }
        w.append("{\"type\":\"FeatureCollection\",\"features\":[")
        var needComma = false
        if (valid.isNotEmpty()) {
            w.append("{\"type\":\"Feature\",\"properties\":{\"name\":\"${jsonEscape(trackName)}\",\"count\":${valid.size}},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[")
            valid.forEachIndexed { idx, r ->
                if (idx > 0) w.append(',')
                w.append(String.format(Locale.US, "[%.7f,%.7f,%d]", r.longitudeDeg, r.latitudeDeg, r.altitudeM))
            }
            w.append("]}}")
            needComma = true
        }
        for (r in pois) {
            if (needComma) w.append(',')
            val ts = isoUtc.format(Instant.ofEpochMilli(r.unixEpochMs))
            val title = jsonEscape(resolvePoiTitle(r, poiMetaBySeq))
            val note = jsonEscape(resolvePoiNote(r, poiMetaBySeq))
            w.append(
                String.format(
                    Locale.US,
                    "{\"type\":\"Feature\",\"properties\":{\"kind\":\"poi\",\"seqId\":%d,\"name\":\"%s\",\"note\":\"%s\",\"time\":\"%s\",\"floor\":%d},\"geometry\":{\"type\":\"Point\",\"coordinates\":[%.7f,%.7f,%d]}}",
                    r.seqId,
                    title,
                    note,
                    ts,
                    r.floorLevel,
                    r.longitudeDeg,
                    r.latitudeDeg,
                    r.altitudeM,
                ),
            )
            needComma = true
        }
        w.append("]}\n")
    }

    private fun exportCsv(records: List<TrajectoryRecord>, w: Writer) {
        w.append("seq_id,utc_iso,lat_wgs84,lon_wgs84,alt_m,speed_ms,heading_deg,steps,hdop,sats,flags_hex,battery_pct,floor,is_poi\n")
        for (r in records) {
            val ts = isoUtc.format(Instant.ofEpochMilli(r.unixEpochMs))
            w.append(
                String.format(
                    Locale.US,
                    "%d,%s,%.7f,%.7f,%d,%.2f,%.1f,%d,%.1f,%d,0x%02X,%d,%d,%d\n",
                    r.seqId,
                    ts,
                    r.latitudeDeg,
                    r.longitudeDeg,
                    r.altitudeM,
                    r.speedMs,
                    r.headingDeg,
                    r.stepCount,
                    r.hdop,
                    r.satellites,
                    r.statusFlags,
                    r.batteryLevel,
                    r.floorLevel,
                    if (r.isPoi) 1 else 0,
                ),
            )
        }
    }

    private fun xmlEscape(s: String): String =
        s.replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace("\"", "&quot;")
            .replace("'", "&apos;")

    private fun jsonEscape(s: String): String {
        val sb = StringBuilder(s.length)
        for (ch in s) {
            when (ch) {
                '\\' -> sb.append("\\\\")
                '"' -> sb.append("\\\"")
                '\n' -> sb.append("\\n")
                '\r' -> sb.append("\\r")
                '\t' -> sb.append("\\t")
                else -> if (ch.code < 0x20) {
                    sb.append(String.format(Locale.US, "\\u%04x", ch.code))
                } else {
                    sb.append(ch)
                }
            }
        }
        return sb.toString()
    }
}
