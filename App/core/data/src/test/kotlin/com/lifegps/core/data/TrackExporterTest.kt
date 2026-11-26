// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.TrajectoryRecord
import java.io.StringWriter
import org.junit.Assert.assertTrue
import org.junit.Test

class TrackExporterTest {

    private fun sampleRecords(): List<TrajectoryRecord> {
        val stream = requireNotNull(javaClass.classLoader?.getResourceAsStream("trajectory_basic.bin"))
        return TrajectoryRecord.decodeStream(stream.use { it.readBytes() })
    }

    @Test
    fun exportsAllFourFormatsInWgs84() {
        val recs = sampleRecords()

        val gpx = StringWriter().also { TrackExporter.export(ExportFormat.GPX, "2026-07-05", recs, it) }.toString()
        assertTrue(gpx.contains("<gpx version=\"1.1\""))
        assertTrue(gpx.contains("<wpt "))
        assertTrue(gpx.contains("<trkpt "))

        val kml = StringWriter().also { TrackExporter.export(ExportFormat.KML, "2026-07-05", recs, it) }.toString()
        assertTrue(kml.contains("<kml xmlns=\"http://www.opengis.net/kml/2.2\">"))
        assertTrue(kml.contains("<LineString>"))

        val geoJson = StringWriter().also { TrackExporter.export(ExportFormat.GEOJSON, "2026-07-05", recs, it) }.toString()
        assertTrue(geoJson.contains("\"FeatureCollection\""))
        assertTrue(geoJson.contains("\"LineString\""))
        assertTrue(geoJson.contains("\"kind\":\"poi\""))

        val csv = StringWriter().also { TrackExporter.export(ExportFormat.CSV, "2026-07-05", recs, it) }.toString()
        assertTrue(csv.startsWith("seq_id,utc_iso,lat_wgs84,lon_wgs84"))
        assertTrue(csv.lines().count { it.isNotBlank() } == 4)
    }

    @Test
    fun simplifierPreservesEndpointsAndPois() {
        val recs = sampleRecords()
        val simplified = TrackSimplifier.simplify(recs, toleranceMeters = 50.0)
        assertTrue(simplified.size >= 2)
        assertTrue(simplified.first() == recs.first())
        assertTrue(simplified.last() == recs.last())
        assertTrue(simplified.any { it.isPoi })
    }
}
