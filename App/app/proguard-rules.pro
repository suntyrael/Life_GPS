# Life_GPS Android R8 / ProGuard rules (prodRelease & devRelease)

# Preserve protocol & domain model classes across R8 shrinking
-keep class com.lifegps.core.protocol.** { *; }
-keep class com.lifegps.core.model.** { *; }
-keep class com.lifegps.core.data.** { *; }

# Nordic MCUmgr / SMP over BLE + Jackson CBOR reflection models
-keep class io.runtime.mcumgr.** { *; }
-keep class no.nordicsemi.android.ble.** { *; }
-keep class com.fasterxml.jackson.** { *; }
-keepattributes *Annotation*,Signature,InnerClasses,EnclosingMethod
-dontwarn com.fasterxml.jackson.**
-dontwarn org.slf4j.**
-dontwarn java.beans.**
-dontwarn javax.xml.**
-dontwarn org.w3c.dom.**

# MapLibre Native Android JNI bindings
-keep class org.maplibre.android.** { *; }
-keep class org.maplibre.geojson.** { *; }
-dontwarn org.maplibre.**
