# Easy IRL Stream

OBS Studio plugin for IRL streamers. Receives an RTMP or SRT stream directly in OBS and automatically reacts to connection events (e.g. scene switch on disconnect or low quality).

## Features

- **Built-in RTMP/SRT Server** — No external server needed. The plugin listens directly on a configurable port.
- **Automatic Scene Switching** — Switches to a configured scene on disconnect or low quality. Automatically switches back on reconnect / recovery.
- **Overlay Control** — Any source can be shown/hidden automatically (e.g. a warning banner on bad connection).
- **Connection Quality Monitoring** — Monitors incoming bitrate and reacts when it drops below a threshold.
- **Recording Control** — Automatically start or stop recording on connection events.
- **Webhook & Custom Commands** — Trigger HTTP POST webhooks and shell commands on events.
- **Grace Period** — Configurable delay before actions are triggered (prevents false alarms on short dropouts).
- **SRTLA Support** — Built-in SRTLA proxy for apps like Moblin to bond WiFi + mobile data simultaneously.
- **Stream Monitor** — Real-time dock widget showing bitrate, FPS, uptime, codec info and more.
- **DuckDNS Integration** — Automatic dynamic DNS updates for changing external IPs.
- **Cross-Platform** — Windows, macOS and Linux.

## Building

### Windows (recommended: `build.ps1`)

The included build script downloads all dependencies automatically:

```powershell
.\build.ps1
```

The script:
1. Clones OBS Studio headers from GitHub
2. Downloads OBS dependencies (FFmpeg etc.)
3. Generates import libraries from OBS DLLs
4. Builds the plugin
5. Asks whether to install directly to OBS

**Requirements:** Visual Studio 2022+ (Community is fine), Git

### Manual Build

```bash
mkdir build && cd build
cmake .. -DOBS_SOURCE_DIR=/path/to/obs-studio \
         -DOBS_LIB_DIR=/path/to/obs-libs \
         -DFFMPEG_DIR=/path/to/ffmpeg-dev
cmake --build . --config RelWithDebInfo
```

### Linux

```bash
sudo apt install libobs-dev obs-studio-dev libavformat-dev libavcodec-dev \
    libavutil-dev libswscale-dev libswresample-dev
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
cmake --build .
sudo cmake --install .
```

## Installation

Copy the built plugin file to the OBS plugin directory:

| Platform | Plugin Path |
|----------|-------------|
| Windows  | `C:\Program Files\obs-studio\obs-plugins\64bit\` |
| macOS    | `~/Library/Application Support/obs-studio/plugins/` |
| Linux    | `/usr/lib/obs-plugins/` |

The `data/locale/` files must be copied to the corresponding data directory
(e.g. `C:\Program Files\obs-studio\data\obs-plugins\easy-irl-stream\locale\`).

## Usage

### 1. Add Source

In OBS: **Sources** → **+** → **Easy IRL Stream**

### 2. Configure Server

- **Protocol**: Choose RTMP or SRT
- **Port**: Default is 1935 (RTMP) or 9000 (SRT)
- **Stream Key**: Freely configurable (RTMP only)

The properties dialog shows the connection URL that needs to be entered on your phone.

### 3. Connect Your Phone

The plugin automatically detects your local and external IP and displays them in the source settings. You'll find the ready-to-use URL there.

#### Same WiFi (Local Network)

No port forwarding needed. Use the **local IP** shown in the settings.

#### On the Go (Mobile Data)

Your phone needs to reach your PC over the internet. This requires **port forwarding** in your router (see [Setting Up Port Forwarding](#setting-up-port-forwarding)). Use the **external IP** shown in the settings.

#### RTMP (e.g. Larix Broadcaster, Streamlabs Mobile)

- Server URL: `rtmp://<YOUR_IP>:1935/live`
- Stream Key: The configured key (default: `stream`)

#### SRT (e.g. Larix Broadcaster, IRL Pro, Moblin)

- URL: `srt://<YOUR_IP>:9000`
- Mode: Caller
- **Passphrase**: Optional, but must be **10–79 characters** long (SRT protocol requirement). Shorter passphrases are ignored.

#### SRTLA / SRT(LA) (e.g. Moblin)

SRTLA (SRT Link Aggregation) bonds WiFi and mobile data simultaneously for a more stable connection. If one network drops, the stream continues over the other.

1. In OBS: Select **SRT** as protocol → **Enable SRTLA** (under *SRTLA (Link Aggregation)*)
2. Note the SRTLA port (default: `5000`)
3. In **Moblin**: Set protocol to **SRT(LA)**
4. Server address: `<YOUR_IP>:5000` (the **SRTLA port**, not the SRT port!)

> **Important:** The SRTLA proxy runs on its own UDP port (default: 5000) and forwards packets internally to the SRT server (port 9000). Always enter the **SRTLA port** in the streaming app!

> **Note on passphrase with SRTLA:** When using SRTLA (e.g. with Moblin), the SRT passphrase must be configured identically in both the plugin and the streaming app. If Moblin does not send a passphrase, remove it from the plugin settings as well — otherwise the SRT handshake will fail.

### 4. Configure Disconnect Events

Under **Event Actions** you can define what happens when the phone connection is completely lost:

| Setting | Description |
|---------|-------------|
| **Disconnect Grace Period** | Wait time in seconds before actions are triggered (default: 5). Prevents false alarms on short dropouts. |
| **Scene on Disconnect** | OBS switches to this scene when the connection is lost (e.g. a "Please wait" scene). |
| **Scene on Reconnect** | OBS switches back to this scene when the phone reconnects (e.g. the live scene). |
| **Overlay Source** | A source within the current scene that is shown on disconnect (see [How Overlays Work](#how-overlays-work)). |
| **Recording on Disconnect** | Automatically start or stop recording. |

### 5. Monitor Connection Quality

Under **Connection Quality** the plugin can monitor incoming bitrate and react **before** the connection drops completely:

| Setting | Description |
|---------|-------------|
| **Monitor quality** | Enable the feature (default: off). |
| **Threshold (kbps)** | Bitrate limit below which the connection is considered "bad" (default: 500 kbps). |
| **Grace period (sec)** | How long the bitrate must stay below the threshold before actions are triggered (default: 3 sec). |
| **Scene on low quality** | OBS switches to this scene (optional). |
| **Overlay on low quality** | A source that is shown (e.g. an "Unstable connection" banner). |

**Important:** On low quality the stream keeps running — only the warning is shown. Once quality recovers (bitrate back above threshold), actions are automatically reversed (overlay hidden, scene switched back).

### How Overlays Work

Overlays are regular OBS sources (images, text, browser sources etc.) that the plugin shows and hides automatically. Here's how to set them up:

1. **Create an overlay source**: Add a new source to your scene, e.g. an image with a warning banner or a text source saying "Reconnecting...".

2. **Hide the source**: Click the **eye icon** next to the source in the source list to hide it. The source must be initially **invisible**.

3. **Select in the plugin**: Choose the source in the plugin under "Overlay Source" (disconnect) or "Overlay on low quality".

4. **Automatic control**: The plugin shows the source automatically when the event occurs and hides it again when the connection recovers.

**Tip:** You can use different overlays for disconnect and low quality — e.g. a subtle warning banner for low quality and a full-screen "Stream will resume" image for a complete disconnect.

### 6. Configure Webhooks

Under **Webhook / Command** external systems can be notified:

- **Webhook URL**: Receives an HTTP POST with JSON on every event.
- **Custom Command**: A shell command executed on events.

#### Webhook Format

```json
{
  "event": "disconnect",
  "source": "Easy IRL Stream",
  "timestamp": 1711360000
}
```

Possible events: `disconnect`, `reconnect`, `low_quality`, `quality_recovered`

### 7. Stream Monitor

Open the real-time stream monitor via **Tools** → **Easy IRL Stream - Stream Monitor**. It shows:

- Connection status with color indicator
- Input bitrate (kbps / Mbps)
- FPS (frames per second)
- Connection uptime
- Total data received
- Video codec, resolution and pixel format
- Audio codec and sample rate
- Server protocol, port and SRTLA status

The monitor updates every 500ms and can be docked anywhere in the OBS interface.

## Help & FAQ

### Tools Menu

In OBS under **Tools** → **Easy IRL Stream - Help** you'll find an overview with:

- Your local and external IP
- Port forwarding instructions
- Frequently asked questions

### Setting Up Port Forwarding

For your phone to stream remotely (mobile data), the port must be forwarded in your router:

1. **Open router configuration** — Enter your router's IP in the browser. Common addresses:
   - `http://192.168.1.1` or `http://192.168.0.1`

2. **Set up port forwarding** — Look for "Port Forwarding" or "NAT":
   | Setting | Value |
   |---------|-------|
   | **External Port** | The port configured in the plugin (default: 1935 for RTMP, 9000 for SRT) |
   | **Internal Port** | Same port |
   | **Protocol** | **TCP** for RTMP, **UDP** for SRT |
   | **Target IP / Device** | Your local PC IP (shown in the plugin) |

3. **Check Windows Firewall** — Windows blocks incoming connections by default. On first launch you should get a firewall prompt. If not, allow the port manually:
   - Windows Search → "Windows Defender Firewall" → "Advanced Settings"
   - "Inbound Rules" → "New Rule" → Port → TCP/UDP → Enter port → Allow

4. **Connect your phone** — Use your **external IP** as server address (shown in the plugin and under Tools → Easy IRL Stream).

> **Note:** Your external IP may change if your ISP doesn't assign a static IP. Check it before each stream via the Tools menu or source settings.

### Frequently Asked Questions

**My phone can't connect — what to do?**

1. Check if the plugin is active in OBS (source visible in the scene)
2. Same WiFi? Use the local IP, no port forwarding needed
3. On mobile data? Set up port forwarding in your router (see above)
4. Windows Firewall: Make sure the port isn't blocked
5. Correct port and protocol? RTMP = TCP port 1935, SRT = UDP port 9000

**Do I need port forwarding on the same WiFi?**

No. As long as phone and PC are on the same network, the local IP is sufficient. Port forwarding is only needed when the phone connects via mobile data (from outside).

**Which is better — RTMP or SRT?**

| | RTMP | SRT |
|---|---|---|
| **Mobile stability** | Medium | Very good (error correction) |
| **Latency** | Low | Configurable (default 120ms) |
| **App support** | Very broad | Growing |
| **Setup** | Simple (URL + key) | Simple (URL only) |

**Recommendation:** SRT for IRL streaming over mobile data, RTMP if the streaming app doesn't support SRT.

> **Note on SRT passphrase:** The passphrase must be **10–79 characters** long. This is a requirement of the SRT protocol. Shorter passphrases are ignored by the plugin and the server starts without encryption.

**What does the threshold (kbps) mean?**

The minimum bitrate below which the connection is considered "bad". When the incoming bitrate drops below this value, the configured quality actions are triggered. 500 kbps is a good starting value — at this bitrate the image is usually already heavily pixelated.

**What's the difference between disconnect and low quality?**

- **Disconnect**: The connection is completely lost. No stream data is arriving.
- **Low quality**: The stream is still arriving, but the bitrate is too low. The image is still there but quality is poor.

Different actions and overlays can be configured for each case.

**How do overlays work?**

Overlays are regular OBS sources (images, text etc.) that are invisible by default. The plugin automatically shows them when an event occurs and hides them again when the connection recovers. See [How Overlays Work](#how-overlays-work).

**What is SRTLA and how do I set it up?**

SRTLA (SRT Link Aggregation) bonds multiple network connections (e.g. WiFi + mobile data) into one. The plugin includes a built-in SRTLA proxy:

1. Select SRT as protocol and enable SRTLA in the settings
2. In the streaming app (e.g. Moblin) select SRT(LA) as protocol
3. Enter the **SRTLA port** (default: 5000) as server address, **not** the SRT port (9000)
4. Firewall: UDP port 5000 must be open

**My external IP keeps changing — what to do?**

You can use a dynamic DNS service (e.g. DuckDNS, No-IP). The plugin has built-in DuckDNS support — enter your subdomain and token in the settings and it will update your IP automatically. Then use a fixed domain like `mystream.duckdns.org` instead of the IP.

## License

MIT License
