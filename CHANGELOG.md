# Easy IRL Stream

## v1.1.1 — SSL Fix

### Bug Fixes
- **Fixed SSL connect errors on IPv6 networks** — Forced IPv4 resolution (`CURL_IPRESOLVE_V4`) to avoid broken IPv6 TLS handshakes that caused `SEC_E_INVALID_TOKEN` errors with Schannel.
- **Additional SSL hardening** — TLS 1.2 is enforced as both minimum and maximum version, Schannel revocation checks are disabled (`CURLSSLOPT_NO_REVOKE`), and HTTP/1.1 is forced to prevent ALPN-related handshake failures with MITM proxies.

### Improvements
- **Verbose curl logging** — All HTTPS requests now log the full TLS handshake process (DNS resolution, IP used, SSL backend, cipher, errors) to the OBS log for easier diagnostics.
- **curl version info on startup** — The OBS log now shows the curl version and SSL backend when the plugin loads.

---

## v1.1.0 — Update Check, Watermark & SSL Fix

### New Features
- **Mandatory update check** — The plugin checks for updates on startup. If a newer version is available, a dialog is shown, the download page opens, and the plugin stays disabled until updated.
- **Watermark for free users** — Non-Patreon users now see a small "Easy IRL Stream - stools.cc" watermark in the bottom-right corner of the video. Patreon supporters get it removed automatically via the server API.
- **SSL error dialog** — When the plugin can't connect to stools.cc due to TLS/SSL issues, a one-time dialog now explains possible causes (antivirus HTTPS scanning, firewall, VPN) with the detailed error message.

### Bug Fixes
- **Fixed SSL connect errors** — Disabled certificate verification and forced TLS 1.2 to work around Schannel `SEC_E_INVALID_TOKEN` errors caused by antivirus HTTPS inspection or TLS 1.3 incompatibilities. Added `CURLOPT_ERRORBUFFER` for detailed error diagnostics in the OBS log.

### Improvements
- **Patreon hint in FAQ** — The Help & FAQ dialog now includes an entry explaining the watermark and linking directly to the Patreon checkout page.

---

## v1.0.1 — Bug Fixes & Improvements

### Bug Fixes
- **Fixed HTTPS webhooks** — Webhooks to `https://` URLs were silently failing because the plugin used raw TCP sockets without TLS. Replaced with libcurl, which properly handles HTTPS, and added connect/request timeouts to prevent hangs.
- **Fixed shared decoder counters** — Debug counters for video packets/frames were shared across all source instances (static variables). They are now per-source and reset on each new connection.
- **Fixed SRT streamid not applied** — The `streamid` setting was loaded from the remote config but never actually appended to the SRT listener URL. It is now included when set.

### Improvements
- **Enriched webhook payload** — Webhook POST body now includes `bitrate_kbps`, `uptime_sec`, `video_width`, `video_height`, and `video_codec` alongside the existing `event`, `source`, and `timestamp` fields.
- **IP addresses in Stream Monitor** — The monitor dock now shows your local (LAN) and external (WAN) IP at the bottom, so you don't have to open the Help dialog to check them.
