plugins {
    alias(libs.plugins.kotlin.jvm)
}

kotlin {
    jvmToolchain(17)
}

sourceSets {
    named("test") {
        resources.srcDir("../../../protocol/vectors")
    }
}

dependencies {
    testImplementation(libs.junit)
}
