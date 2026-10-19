# Experimental ESP-IDF DPP completion patches

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

The first patch alone was tested: the ESP32 connected in about one second, but
the Pixel failed after approximately ten seconds. That is consistent with a
later Connection Status Result timeout, not proof of it without another capture.

`esp-idf-v6.1-dpp-connection-status-result.patch` is the 2026-10-19 follow-up.
Apply it **after** the first patch. It parses the authenticated `sendConnStatus`
request and retains the session keys/peer while stopping provisioning listen.
The optional `esp_supp_dpp_report_connection_status()` API verifies that the
station is associated with an accepted SSID, then sends the encrypted Connection
Status Result on the original DPP channel. Its payload contains the Enrollee
Nonce and connection-status JSON (`result: 0`, `ssid64`).

The project's existing background DPP cleanup task calls that API after GOT_IP,
before stop/deinit. It waits for matching action TX status (at most two seconds),
without holding the DPP lock or blocking the Wi-Fi event loop/UI. Cancellation
wakes the wait; failure/timeout does not undo the working connection or saved
credentials. Incoming action processing cannot free auth during the final TX.
The project hook is feature-macro guarded so stock ESP-IDF still builds.

This is a success-path interoperability experiment, not a complete upstream
connection-outcome API: failed AP attempts still use the existing Retry/Cancel
flow, and no failure Connection Status Result is synthesized. Cancel/deinit frees
the retained session. Slow DHCP could exceed the phone's completion deadline;
reporting at authenticated association instead would require a separate lifecycle
hook. A matching TX completion is not proof that Android accepted the payload.
Pixel success with this follow-up is not yet verified.

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
git -C "$IDF_PATH" apply --check "$PWD/patches/esp-idf-v6.1-dpp-connection-status-result.patch"
git -C "$IDF_PATH" apply "$PWD/patches/esp-idf-v6.1-dpp-connection-status-result.patch"
idf.py build
```

No project configuration changes or fullclean are required. The current local
`sdkconfig` selects Screen Diagnostics; repository defaults still select Air Quality.

Validation on 2026-10-16: `idf.py build` succeeded with the patched SDK and the
existing Screen Diagnostics configuration. The changed DPP files produced no
compiler warnings. Existing FreeRTOS unused-function and LVGL deprecation
warnings remain. Patch application and reverse-application checks passed.
Validation on 2026-10-19: the follow-up also built successfully with the existing
Screen Diagnostics configuration, with no compiler warnings in that incremental
build. Forward/reverse patch checks passed. Pixel/hardware validation of the
follow-up is pending; no runtime regression claim is made.

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
git -C "$IDF_PATH" apply --reverse --check "$PWD/patches/esp-idf-v6.1-dpp-connection-status-result.patch"
git -C "$IDF_PATH" apply --reverse "$PWD/patches/esp-idf-v6.1-dpp-connection-status-result.patch"
git -C "$IDF_PATH" apply --reverse --check "$PWD/patches/esp-idf-v6.1-dpp-configuration-result.patch"
git -C "$IDF_PATH" apply --reverse "$PWD/patches/esp-idf-v6.1-dpp-configuration-result.patch"
git -C "$IDF_PATH" status --short
idf.py build
```

If the reverse check fails because those SDK files were subsequently edited,
inspect the conflict instead of resetting the whole SDK. Flash the rebuilt
unpatched firmware if you also want to remove the experiment from the device.
Reverse only the follow-up patch to compare against the first experiment. The
guarded application hook needs no source change when either SDK patch is removed.
