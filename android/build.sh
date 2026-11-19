#!/bin/sh
# SPDX-License-Identifier: MIT
# SQ-80 Tablet's APK with the Android SDK's own tools, no Gradle: aapt2, javac, d8, zipalign, apksigner.
#   android/build.sh SITE_DIR VERSION [OUT.apk]
# SITE_DIR: the built site (its panel/, edit/ and fonts/ go into the app). ANDROID_HOME: the SDK. The key: android/
# sq80-tablet.keystore unless SQ80_KEYSTORE (and SQ80_KEYSTORE_PASS) say another; the same key every build, so an
# update installs over the last one.
set -e
SITE="$1"; VERSION="$2"; OUT="${3:-android/build/SQ80-Tablet.apk}"
[ -n "$SITE" ] && [ -n "$VERSION" ] || { echo "usage: android/build.sh SITE_DIR VERSION [OUT.apk]"; exit 2; }
SDK="${ANDROID_HOME:-$ANDROID_SDK_ROOT}"
BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
PLAT="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)"
JAR="$PLAT/android.jar"
D=android/build
rm -rf "$D" && mkdir -p "$D/gen" "$D/classes" "$D/dex" "$D/assets/site"
echo "sdk      $BT, $PLAT"

# the page and what it loads
for d in panel edit fonts; do cp -r "$SITE/$d" "$D/assets/site/"; done
rm -f "$D/assets/site/edit/index.html"

# version code from the version: 1.2.3 -> 10203
IFS=. read -r MA MI PA <<V
$VERSION
V
CODE=$((MA * 10000 + MI * 100 + ${PA:-0}))

"$BT/aapt2" compile --dir android/res -o "$D/res.zip"
"$BT/aapt2" link -I "$JAR" --manifest android/AndroidManifest.xml -o "$D/base.apk" "$D/res.zip" -A "$D/assets" \
    --java "$D/gen" --min-sdk-version 23 --target-sdk-version 30 --version-code "$CODE" --version-name "$VERSION"
javac -encoding UTF-8 --release 8 -Xlint:-options -classpath "$JAR" -d "$D/classes" \
    $(find android/src "$D/gen" -name '*.java') 2>&1 | sed 's/^\(android\/src\/[^:]*\):\([0-9]*\): error: \(.*\)/::error file=\1,line=\2::\3\n&/'
[ -n "$(find "$D/classes" -name MainActivity.class)" ] || { echo "javac failed"; exit 1; }
"$BT/d8" --release --min-api 23 --lib "$JAR" --output "$D/dex" $(find "$D/classes" -name '*.class')
(cd "$D/dex" && zip -q -u ../base.apk classes.dex)
"$BT/zipalign" -f -p 4 "$D/base.apk" "$D/aligned.apk"
KS="${SQ80_KEYSTORE:-android/sq80-tablet.keystore}"; PASS="${SQ80_KEYSTORE_PASS:-sq80tablet}"
mkdir -p "$(dirname "$OUT")"
"$BT/apksigner" sign --ks "$KS" --ks-pass "pass:$PASS" --out "$OUT" "$D/aligned.apk"
"$BT/apksigner" verify "$OUT"
echo "apk      $OUT ($(wc -c < "$OUT") B, $VERSION / $CODE)"
