#!/bin/bash
# Сборка ULTRA POWDER TOY в APK. Запуск: bash /home/user/sandbox/apk/build.sh
# Скрипт самодостаточный: доставляет JDK, Android SDK, NDK и исходники
# SDL2, если рабочая область их потеряла между сессиями.
set -e
HERE=/home/user/sandbox/apk
cd $HERE

VER_NAME=${VER_NAME:-1.4.0}
VER_CODE=${VER_CODE:-1}

# Издание и ветка.
#   EDITION=alco  — полная химия, варка алкоголя (по умолчанию)
#   EDITION=base  — только базовые вещества, без таблицы Менделеева
#   ADMIN=1       — ветка UptA: менеджер сохранений и прочее служебное
# Ставятся рядом друг с другом: у каждой своё имя пакета.
EDITION=${EDITION:-alco}
ADMIN=${ADMIN:-0}

UPT_FLAGS="-DUPT_WITH_SDL -DUPT_VERSION_RAW=$VER_NAME"
case "$EDITION" in
  base) PKG=com.krillxz2362.uptbase; APPNAME="ULTRA POWDER TOY BASE"
        UPT_FLAGS="$UPT_FLAGS -DUPT_BASE"; SUF="-base" ;;
  alco) PKG=com.krillxz2362.upt;     APPNAME="ULTRA POWDER TOY"; SUF="" ;;
  *)    echo "неизвестное издание: $EDITION (нужно alco или base)"; exit 1 ;;
esac
if [ "$ADMIN" = "1" ]; then
  PKG=com.krillxz2362.upta
  APPNAME="ULTRA POWDER TOY UPTA"
  UPT_FLAGS="$UPT_FLAGS -DUPT_ADMIN"
  SUF="$SUF-upta"
fi
OUT=/home/user/UPT-$VER_NAME$SUF.apk
echo ">> издание $EDITION, админ $ADMIN, пакет $PKG"

#--- 1. JDK 17 -------------------------------------------------------
if [ ! -x /home/user/jdk17/bin/java ]; then
  echo ">> ставлю JDK 17"
  curl -sL -o /tmp/jdk17.tgz "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.13%2B11/OpenJDK17U-jdk_x64_linux_hotspot_17.0.13_11.tar.gz"
  mkdir -p /home/user/jdk17 && tar xzf /tmp/jdk17.tgz -C /home/user/jdk17 --strip-components=1
fi
chmod +x /home/user/jdk17/bin/* 2>/dev/null || true
export JAVA_HOME=/home/user/jdk17
export PATH=$JAVA_HOME/bin:$PATH

#--- 2. Android SDK и NDK --------------------------------------------
export ANDROID_SDK_ROOT=/home/user/androidsdk
BT=$ANDROID_SDK_ROOT/build-tools/34.0.0
AJ=$ANDROID_SDK_ROOT/platforms/android-34/android.jar
NDK=$ANDROID_SDK_ROOT/ndk/26.3.11579264
if [ ! -f "$AJ" ] || [ ! -d "$NDK" ]; then
  echo ">> ставлю Android SDK и NDK"
  mkdir -p $ANDROID_SDK_ROOT && cd $ANDROID_SDK_ROOT
  if [ ! -x cmdline-tools/latest/bin/sdkmanager ]; then
    curl -sL -o cmd.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
    unzip -q -o cmd.zip && mkdir -p cmdline-tools/latest
    mv cmdline-tools/bin cmdline-tools/lib cmdline-tools/NOTICE.txt \
       cmdline-tools/source.properties cmdline-tools/latest/ 2>/dev/null || true
  fi
  chmod +x cmdline-tools/latest/bin/* 2>/dev/null || true
  yes | ./cmdline-tools/latest/bin/sdkmanager --licenses >/dev/null 2>&1 || true
  ./cmdline-tools/latest/bin/sdkmanager "platforms;android-34" "build-tools;34.0.0" \
      "ndk;26.3.11579264" >/dev/null
  cd $HERE
fi
# права на запуск слетают вместе со снимком рабочей области
chmod +x $BT/* $NDK/ndk-build 2>/dev/null || true
chmod -R +x $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin 2>/dev/null || true
chmod +x $NDK/prebuilt/linux-x86_64/bin/* 2>/dev/null || true

#--- 3. Исходники SDL2 -----------------------------------------------
if [ ! -f SDL2/Android.mk ]; then
  echo ">> качаю SDL2"
  rm -rf SDL2
  curl -sL -o /tmp/SDL2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
  tar xzf /tmp/SDL2.tar.gz && mv SDL2-2.30.9 SDL2
fi

#--- 4. Дерево сборки ------------------------------------------------
# obj и libs тоже чистим: ndk-build не следит за сменой признаков
# сборки, и издание Base собралось бы из объектных файлов Alcoholic —
# три разных APK вышли бы одинаковыми внутри.
rm -rf jni work src res obj libs
mkdir -p jni/app res/mipmap res/values src/$(echo $PKG | tr . /) work

# ndk-build обходит подпапки jni: кладём туда и SDL2, и нашу часть
ln -sfn ../SDL2 jni/SDL2
cat > jni/Android.mk <<'EOF'
include $(call all-subdir-makefiles)
EOF

cat > jni/Application.mk <<'EOF'
# arm64 — основной, 32 бита держим для старых телефонов.
APP_ABI := arm64-v8a armeabi-v7a
APP_PLATFORM := android-21
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -O2 -fexceptions -frtti
APP_CFLAGS := -O2
EOF

cat > jni/app/Android.mk <<'EOF'
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := main
CORE := $(LOCAL_PATH)/../../../cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../SDL2/include $(CORE)/include
LOCAL_SRC_FILES := $(CORE)/src/world.cpp $(CORE)/src/contact.cpp \
                   $(CORE)/src/motion.cpp $(CORE)/src/state.cpp \
                   $(CORE)/src/chem.cpp $(CORE)/src/ui.cpp \
                   $(CORE)/src/save.cpp $(CORE)/src/main_sdl.cpp
LOCAL_CPPFLAGS += $(UPT_FLAGS)
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid
include $(BUILD_SHARED_LIBRARY)
EOF

#--- 5. Java ---------------------------------------------------------
JAVADIR=src/$(echo $PKG | tr . /)
cat > $JAVADIR/MainActivity.java <<EOF
package $PKG;

import org.libsdl.app.SDLActivity;

// Всё окно и ввод держит SDL. Здесь только список библиотек: сперва
// сам SDL, потом наша, иначе загрузчик не найдёт символы.
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
EOF

cat > res/values/strings.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<resources><string name="app_name">$APPNAME</string></resources>
EOF

cp /home/user/sandbox/res/icon_192.png res/mipmap/ic_launcher.png

cat > AndroidManifest.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="$PKG"
    android:versionCode="$VER_CODE"
    android:versionName="$VER_NAME">

    <uses-sdk android:minSdkVersion="21" android:targetSdkVersion="34" />
    <uses-feature android:glEsVersion="0x00020000" android:required="true" />

    <application
        android:label="@string/app_name"
        android:icon="@mipmap/ic_launcher"
        android:allowBackup="true"
        android:hardwareAccelerated="true"
        android:hasCode="true">
        <activity
            android:name=".MainActivity"
            android:label="@string/app_name"
            android:exported="true"
            android:launchMode="singleInstance"
            android:screenOrientation="sensorLandscape"
            android:configChanges="layoutDirection|locale|orientation|uiMode|screenLayout|screenSize|smallestScreenSize|keyboard|keyboardHidden|navigation">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF

#--- 6. Нативная часть -----------------------------------------------
echo ">> собираю нативную часть (это долго)"
$NDK/ndk-build NDK_PROJECT_PATH=$HERE APP_BUILD_SCRIPT=$HERE/jni/Android.mk \
  NDK_APPLICATION_MK=$HERE/jni/Application.mk UPT_FLAGS="$UPT_FLAGS" -j2 2>&1 | tail -5

#--- 7. Упаковка -----------------------------------------------------
echo ">> упаковываю"
$BT/aapt2 compile --dir res -o work/res.zip
$BT/aapt2 link -o work/base.apk -I $AJ --manifest AndroidManifest.xml \
  -R work/res.zip --java work/gen --auto-add-overlay \
  --min-sdk-version 21 --target-sdk-version 34

SDLJAVA=$(find SDL2/android-project -name "*.java" -path "*org/libsdl/app*")
# Без -bootclasspath: в android.jar нет механики лямбд, и javac на ней
# падает. Компилируем на JDK, а d8 потом переписывает лямбды под
# Android сам (десахаризация), для того и даём ему --lib.
javac -encoding UTF-8 -source 11 -target 11 -nowarn \
  -classpath $AJ -d work/obj \
  $SDLJAVA $JAVADIR/MainActivity.java $(find work/gen -name "*.java") 2>&1 \
  | grep -vE "^Note|bootstrap class path|warning" || true
# Проверка: молча пропустить сломанную сборку Java нельзя — APK тогда
# ставится, но падает при запуске.
test -f work/obj/org/libsdl/app/SDLActivity.class || { echo "ОШИБКА: Java не собралась"; exit 1; }
$BT/d8 --release --min-api 21 --lib $AJ --output work/ $(find work/obj -name "*.class")
test -f work/classes.dex || { echo "ОШИБКА: classes.dex не собрался"; exit 1; }

cd work
cp base.apk unsigned.apk
zip -q -j unsigned.apk classes.dex
cd $HERE
# библиотеки кладём внутрь APK руками: gradle здесь не используется
mkdir -p work/payload
cp -r libs/* work/payload/ 2>/dev/null || cp -r obj/local/* work/payload/
cd work/payload
rm -rf */objs* 2>/dev/null || true
find . -name "*.so" | sed 's|^\./||' > /tmp/solist.txt
mkdir -p ../libtree
while read f; do
  abi=$(dirname $f)
  mkdir -p ../libtree/lib/$abi
  cp $f ../libtree/lib/$abi/
done < /tmp/solist.txt
cd ../libtree
# .so должны лежать без сжатия и выровненными — иначе их не загрузить
zip -q -r -0 ../unsigned.apk lib
cd $HERE

$BT/zipalign -f -p 4 work/unsigned.apk work/aligned.apk

#--- 8. Подпись ------------------------------------------------------
rm -f $OUT $OUT.idsig
$BT/apksigner sign --ks /home/user/uh-key.jks --ks-pass pass:ultrahtml \
  --key-pass pass:ultrahtml --out $OUT work/aligned.apk
$BT/apksigner verify --print-certs $OUT | head -3
echo
unzip -l $OUT | grep -E "\.so|classes.dex" | head
ls -lh $OUT
echo "ГОТОВ: $OUT"
