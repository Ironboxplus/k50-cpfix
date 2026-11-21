plugins {
    id("com.android.application") version "9.5.2"
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.21"
}

// Release signing comes from CI secrets (see .github/workflows/ci.yml); store and key share one password.
val keystore = System.getenv("SIGNING_KEYSTORE")?.let(::file)?.takeIf { it.isFile }

android {
    namespace = "top.ironbox.cpfix"
    compileSdk {
        version = release(37) { minorApiLevel = 2 }
    }

    defaultConfig {
        applicationId = "top.ironbox.cpfix"
        minSdk = 34
        targetSdk = 37
        versionCode = (System.getenv("VERSION_CODE") ?: "1").toInt()
        versionName = System.getenv("VERSION_NAME") ?: "dev"
    }

    signingConfigs {
        if (keystore != null) {
            create("release") {
                storeFile = keystore
                storePassword = System.getenv("SIGNING_STORE_PASSWORD")
                keyAlias = System.getenv("SIGNING_KEY_ALIAS")
                keyPassword = System.getenv("SIGNING_STORE_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release")
        }
    }

    buildFeatures {
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions {
        // ParamsTest reads ../kernel/cpfix_policy.h relative to this directory
        unitTests.all { it.workingDir = projectDir }
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2026.11.01")
    implementation(composeBom)
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("com.github.topjohnwu.libsu:core:6.0.0")
    testImplementation("junit:junit:4.13.2")
}
