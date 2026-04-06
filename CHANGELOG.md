# Easy IRL Stream

## v1.1.0 — Update Check, Watermark & SSL Fix

### New Features
- **Mandatory update check** — The plugin now checks for updates on startup. If a newer version is available, a dialog is shown, the download page opens, and the plugin stays disabled until updated.
- **Watermark for free users** — Non-Patreon users now see a small "Easy IRL Stream - stools.cc" watermark in the bottom-right corner of the video. Patreon supporters get it removed automatically.

### Bug Fixes
- **Fixed SSL connect errors** — API calls to `stools.cc` failed with "SSL connect error" because libcurl (built with OpenSSL) couldn't find CA certificates on Windows. Added `CURLSSLOPT_NATIVE_CA` to all curl calls so OpenSSL uses the Windows certificate store.

---

## v1.0.1 — Bug Fixes & Improvements

### Bug Fixes
- **Fixed HTTPS webhooks** — Webhooks to `https://` URLs were silently failing because the plugin used raw TCP sockets without TLS. Replaced with libcurl, which properly handles HTTPS, and added connect/request timeouts to prevent hangs.
- **Fixed shared decoder counters** — Debug counters for video packets/frames were shared across all source instances (static variables). They are now per-source and reset on each new connection.
- **Fixed SRT streamid not applied** — The `streamid` setting was loaded from the remote config but never actually appended to the SRT listener URL. It is now included when set.

### Improvements
- **Enriched webhook payload** — Webhook POST body now includes `bitrate_kbps`, `uptime_sec`, `video_width`, `video_height`, and `video_codec` alongside the existing `event`, `source`, and `timestamp` fields.
- **IP addresses in Stream Monitor** — The monitor dock now shows your local (LAN) and external (WAN) IP at the bottom, so you don't have to open the Help dialog to check them.
