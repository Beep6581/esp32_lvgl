# Experimental ESP-IDF DPP Configuration Result patch

`esp-idf-v6.1-dpp-configuration-result.patch` applies to **ESP-IDF v6.1,
commit `fff9895c82d744c7237be8847347bdd1b07c6643`**. Prepared 2026-10-16.
This is an explicit SDK experiment, not a vendored component or an automatically
applied project dependency. Switching project branches does not undo the SDK patch.

## Behavior and scope

The patch constructs the missing authenticated DPP v2 Configuration Result and
uses IDF's existing action TX request and operation-ID matching. The validated
credential event is retained until `WIFI_ACTION_TX_DONE`; only then can the app
start connecting. Existing legacy-AKM authentication cleanup is also deferred.
A two-second watchdog and the existing failure/deinit paths release the retained
configuration if transmission fails, is cancelled, or its status never arrives.
The DPP v1 path and public APIs are unchanged.

The application continues testing candidates in RAM and persisting them only
after GOT_IP. Configuration Result means configuration acceptance, not proof
of AP connectivity or persistent storage.

**Known limitation:** the captured Pixel also requests a separate Connection
Status Result (`sendConnStatus`). This patch deliberately implements only the
requested Configuration Result exchange. It may resolve the two-second timeout
but expose a later Connection Status Result timeout. Full Pixel success is not
yet established. Completing that separate exchange would require retaining the
DPP session through station connection, sending the actual connection outcome,
and coordinating teardown. That extension is outside this isolated experiment.

The frame encoder follows the BSD-licensed hostap implementation available in
[AOSP's `dpp_build_conf_result()`](https://android.googlesource.com/platform/external/wpa_supplicant_8/+/refs/tags/android-mainline-12.0.0_r95/src/common/dpp.c),
using the same authenticated header, status, nonce and wrapped-data layout.
The patched IDF file already carries the hostap BSD notice; its license is in
`components/wpa_supplicant/README` in ESP-IDF. No new library is introduced.

## Apply and build

From this project root, after activating the existing ESP-IDF v6.1 environment:

```sh
git -C "$IDF_PATH" rev-parse HEAD
git -C "$IDF_PATH" status --short
```

The revision must match the base above. Stop if the SDK checkout has unrelated
changes. Then:

```sh
git -C "$IDF_PATH" apply --check "$PWD/patches/esp-idf-v6.1-dpp-configuration-result.patch"
git -C "$IDF_PATH" apply "$PWD/patches/esp-idf-v6.1-dpp-configuration-result.patch"
idf.py build
```

No project configuration changes or fullclean are required. The current local
`sdkconfig` selects Screen Diagnostics; repository defaults still select Air Quality.

Validation on 2026-10-16: `idf.py build` succeeded with the patched SDK and the
existing Screen Diagnostics configuration. The changed DPP files produced no
compiler warnings. Existing FreeRTOS unused-function and LVGL deprecation
warnings remain. Patch application and reverse-application checks passed.
Pixel/hardware validation is pending; no runtime regression claim is made.

## Simple Pixel test (no ADB)

1. Flash with `idf.py -p /dev/ttyACM0 flash` (do not erase NVS).
2. Open configuration on the display and choose Change / Configure Wi-Fi to
   deliberately open the DPP QR screen.
3. On the Pixel, select the desired 2.4 GHz Wi-Fi network and use Easy Connect /
   Add device to scan the QR code.
4. Wait for the final phone result. Success means the Pixel reports successful
   provisioning instead of "Couldn't add device". Note whether any failure now
   happens later than the original two-second timeout.
5. Verify that the ESP32 obtains an internal IP. Reboot it and verify automatic
   reconnection to the same network.
6. Repeat once through Configure Wi-Fi to exercise reconfiguration.

If it fails, report the phone result and approximate delay first. No new ADB
capture is required for this experiment.

## Remove the local SDK patch

From this project root in the same activated environment:

```sh
git -C "$IDF_PATH" apply --reverse --check "$PWD/patches/esp-idf-v6.1-dpp-configuration-result.patch"
git -C "$IDF_PATH" apply --reverse "$PWD/patches/esp-idf-v6.1-dpp-configuration-result.patch"
git -C "$IDF_PATH" status --short
idf.py build
```

If the reverse check fails because those SDK files were subsequently edited,
inspect the conflict instead of resetting the whole SDK. Flash the rebuilt
unpatched firmware if you also want to remove the experiment from the device.
