#include <QDialog>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QApplication>
#include <QPalette>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

#include <obs-frontend-api.h>

#include "help-dialog.hpp"
#include "obfuscation.h"

static QDialog *g_help_dlg = nullptr;
static QTextBrowser *g_browser = nullptr;

struct HelpStrings {
	const char *title;
	const char *your_network;
	const char *local_ip_label;
	const char *external_ip_label;
	const char *port_fwd;
	const char *port_fwd_intro;
	const char *step1;
	const char *step2;
	const char *step3;
	const char *step4;
	const char *same_wifi_note;
	const char *duckdns_title;
	const char *duckdns_intro;
	const char *duck_step1;
	const char *duck_step2;
	const char *duck_step3;
	const char *duck_step4;
	const char *duck_step5;
	const char *duck_example;
	const char *faq_title;
	const char *faq_q1;
	const char *faq_a1;
	const char *faq_q2;
	const char *faq_a2;
	const char *faq_q3;
	const char *faq_a3;
	const char *faq_q4;
	const char *faq_a4;
	const char *faq_q5;
	const char *faq_a5;
	const char *faq_q6;
	const char *faq_a6;
	const char *srtla_title;
	const char *srtla_intro;
	const char *srtla_step1;
	const char *srtla_step2;
	const char *srtla_step3;
	const char *srtla_step4;
	const char *faq_q7;
	const char *faq_a7;
};

static const HelpStrings LANG_DE = {
	"Easy IRL Stream",
	"Deine Netzwerk-Informationen",
	"Lokale IP (im gleichen WLAN)",
	"Externe IP (f&uuml;r Mobilfunk / unterwegs)",
	"Port-Weiterleitung einrichten",
	"Damit dein Handy <b>von unterwegs</b> (Mobilfunk) streamen kann, "
	"muss der Port im Router weitergeleitet werden:",
	"<b>Router-Konfiguration &ouml;ffnen</b><br>"
	"Fritz!Box: <code>http://fritz.box</code><br>"
	"Telekom: <code>http://192.168.2.1</code><br>"
	"Andere: <code>http://192.168.1.1</code>",
	"<b>Port-Weiterleitung einrichten</b><br>"
	"Externer Port: Dein Plugin-Port (Standard: <code>1935</code> / <code>9000</code>)<br>"
	"Interner Port: Der gleiche Port<br>"
	"Protokoll: <b>TCP</b> (RTMP) oder <b>UDP</b> (SRT)<br>"
	"Ziel-IP: <code>%1</code> (dieser PC)",
	"<b>Windows-Firewall pr&uuml;fen</b><br>"
	"Beim ersten Start fragt Windows nach. Falls nicht:<br>"
	"Windows-Suche &rarr; <i>Windows Defender Firewall</i> &rarr; "
	"<i>Erweiterte Einstellungen</i> &rarr; <i>Eingehende Regeln</i> &rarr; "
	"<i>Neue Regel</i> &rarr; Port &rarr; TCP/UDP &rarr; Port eingeben &rarr; Zulassen",
	"<b>Am Handy verbinden</b><br>"
	"Als Server-IP die externe IP verwenden: <code>%1</code>",
	"<b>Im gleichen WLAN?</b> Keine Port-Weiterleitung n&ouml;tig! "
	"Einfach die lokale IP verwenden: <code>%1</code>",
	"DuckDNS (Dynamisches DNS)",
	"Deine externe IP &auml;ndert sich regelm&auml;&szlig;ig. "
	"Mit <a href='https://www.duckdns.org'>DuckDNS</a> bekommst du eine feste Adresse:",
	"Gehe zu <a href='https://www.duckdns.org'>duckdns.org</a> und erstelle ein Konto",
	"Erstelle eine Subdomain (z.B. <code>meinstream</code>)",
	"Kopiere deinen <b>Token</b>",
	"Trage Subdomain + Token auf <a href='https://stools.cc/dashboard/plugin'>stools.cc</a> unter <i>DuckDNS</i> ein",
	"Das Plugin aktualisiert deine IP automatisch!",
	"Dein Handy verbindet sich dann z.B. mit:",
	"H&auml;ufige Fragen",
	"Mein Handy kann sich nicht verbinden &ndash; was tun?",
	"1. Plugin in OBS aktiv? (Quelle muss in einer Szene sein)<br>"
	"2. Im gleichen WLAN? &rarr; Lokale IP verwenden<br>"
	"3. &Uuml;ber Mobilfunk? &rarr; Port-Weiterleitung einrichten<br>"
	"4. Windows-Firewall &rarr; Port freigeben<br>"
	"5. Port + Protokoll korrekt? RTMP = TCP:1935, SRT = UDP:9000",
	"Was ist besser &ndash; RTMP oder SRT?",
	"<b>SRT</b> ist besser f&uuml;r Mobilfunk (eingebaute Fehlerkorrektur, konfigurierbare Latenz).<br>"
	"<b>RTMP</b> ist einfacher und wird von mehr Streaming-Apps unterst&uuml;tzt.<br>"
	"<i>Empfehlung:</i> SRT f&uuml;r IRL-Streaming, RTMP als Fallback.<br>"
	"<b>Hinweis:</b> Die SRT-Passphrase muss <b>10&ndash;79 Zeichen</b> lang sein (SRT-Protokoll-Vorgabe).",
	"Wie funktionieren Overlays?",
	"Erstelle eine Quelle (Bild/Text) in deiner Szene &rarr; Blende sie mit dem "
	"<b>Auge-Symbol</b> aus &rarr; W&auml;hle sie im Plugin als Overlay-Quelle aus &rarr; "
	"Das Plugin blendet sie automatisch ein/aus.",
	"Was bedeutet &bdquo;Schwellenwert (kbps)&ldquo;?",
	"Die minimale Bitrate, ab der die Verbindung als &bdquo;schlecht&ldquo; gilt. "
	"Standard: <code>500 kbps</code>. Liegt die Bitrate darunter, werden die "
	"konfigurierten Qualit&auml;ts-Aktionen ausgel&ouml;st (Overlay, Szenenwechsel&hellip;).",
	"Unterschied Disconnect vs. schlechte Qualit&auml;t?",
	"<b>Disconnect:</b> Verbindung komplett weg &ndash; kein Stream kommt an.<br>"
	"<b>Schlechte Qualit&auml;t:</b> Stream kommt noch an, aber Bitrate ist zu niedrig.<br>"
	"F&uuml;r beide k&ouml;nnen unterschiedliche Aktionen und Overlays konfiguriert werden.",
	"Meine externe IP &auml;ndert sich st&auml;ndig?",
	"Nutze DuckDNS (siehe oben). Dann hast du eine feste Adresse wie "
	"<code>meinstream.duckdns.org</code>.",
	"SRTLA (Link Aggregation)",
	"SRTLA erm&ouml;glicht Apps wie <b>Moblin</b>, WLAN und Mobilfunk <b>gleichzeitig</b> "
	"zu nutzen. Die Verbindung wird dadurch deutlich stabiler &ndash; f&auml;llt ein Netzwerk aus, "
	"l&auml;uft der Stream &uuml;ber das andere weiter.",
	"Auf <a href='https://stools.cc/dashboard/plugin'>stools.cc</a>: <b>SRT</b> als Protokoll w&auml;hlen und <b>SRTLA aktivieren</b>",
	"SRTLA-Port merken (Standard: <code>5000</code>)",
	"In <b>Moblin</b>: Protokoll auf <b>SRT(LA)</b> stellen",
	"Als Server-Adresse <code>&lt;DEINE_IP&gt;:5000</code> eingeben "
	"(den SRTLA-Port, <b>nicht</b> den SRT-Port!)",
	"Was ist SRTLA?",
	"<b>SRTLA</b> (SRT Link Aggregation) b&uuml;ndelt mehrere Netzwerkverbindungen "
	"(z.B. WLAN + Mobilfunk) zu einer einzigen. Das Plugin startet einen SRTLA-Proxy, "
	"der die Pakete entgegennimmt und an den internen SRT-Server weiterleitet.<br>"
	"<b>Standard-Ports:</b> SRTLA = UDP <code>5000</code>, SRT = UDP <code>9000</code><br>"
	"<b>Wichtig:</b> In Moblin den <b>SRTLA-Port</b> (5000) angeben, nicht den SRT-Port (9000)!",
};

static const HelpStrings LANG_EN = {
	"Easy IRL Stream",
	"Your Network Information",
	"Local IP (same WiFi network)",
	"External IP (for mobile / remote)",
	"Port Forwarding Setup",
	"For your phone to stream <b>remotely</b> (mobile data), "
	"you need to set up port forwarding in your router:",
	"<b>Open router configuration</b><br>"
	"Common addresses: <code>http://192.168.1.1</code> or <code>http://192.168.0.1</code>",
	"<b>Set up port forwarding</b><br>"
	"External port: Your plugin port (default: <code>1935</code> / <code>9000</code>)<br>"
	"Internal port: Same port<br>"
	"Protocol: <b>TCP</b> (RTMP) or <b>UDP</b> (SRT)<br>"
	"Target IP: <code>%1</code> (this PC)",
	"<b>Check Windows Firewall</b><br>"
	"Windows should ask on first launch. If not:<br>"
	"Windows Search &rarr; <i>Windows Defender Firewall</i> &rarr; "
	"<i>Advanced Settings</i> &rarr; <i>Inbound Rules</i> &rarr; "
	"<i>New Rule</i> &rarr; Port &rarr; TCP/UDP &rarr; Enter port &rarr; Allow",
	"<b>Connect your phone</b><br>"
	"Use the external IP as server address: <code>%1</code>",
	"<b>Same WiFi?</b> No port forwarding needed! "
	"Just use the local IP: <code>%1</code>",
	"DuckDNS (Dynamic DNS)",
	"Your external IP changes regularly. "
	"With <a href='https://www.duckdns.org'>DuckDNS</a> you get a fixed address:",
	"Go to <a href='https://www.duckdns.org'>duckdns.org</a> and create an account",
	"Create a subdomain (e.g. <code>mystream</code>)",
	"Copy your <b>Token</b>",
	"Enter subdomain + token on <a href='https://stools.cc/dashboard/plugin'>stools.cc</a> under <i>DuckDNS</i>",
	"The plugin updates your IP automatically!",
	"Your phone then connects to e.g.:",
	"Frequently Asked Questions",
	"My phone can&apos;t connect &ndash; what to do?",
	"1. Plugin active in OBS? (source must be in a scene)<br>"
	"2. Same WiFi? &rarr; Use local IP<br>"
	"3. On mobile data? &rarr; Set up port forwarding<br>"
	"4. Windows Firewall &rarr; Allow the port<br>"
	"5. Port + protocol correct? RTMP = TCP:1935, SRT = UDP:9000",
	"Which is better &ndash; RTMP or SRT?",
	"<b>SRT</b> is better for mobile (built-in error correction, configurable latency).<br>"
	"<b>RTMP</b> is simpler and supported by more streaming apps.<br>"
	"<i>Recommendation:</i> SRT for IRL streaming, RTMP as fallback.<br>"
	"<b>Note:</b> The SRT passphrase must be <b>10&ndash;79 characters</b> long (SRT protocol requirement).",
	"How do overlays work?",
	"Create a source (image/text) in your scene &rarr; Hide it with the "
	"<b>eye icon</b> &rarr; Select it as overlay source in the plugin &rarr; "
	"The plugin shows/hides it automatically.",
	"What does &quot;threshold (kbps)&quot; mean?",
	"The minimum bitrate below which the connection is considered &quot;bad&quot;. "
	"Default: <code>500 kbps</code>. If the bitrate drops below this, the "
	"configured quality actions are triggered (overlay, scene switch&hellip;).",
	"Difference between disconnect and bad quality?",
	"<b>Disconnect:</b> Connection completely lost &ndash; no stream arriving.<br>"
	"<b>Bad quality:</b> Stream still arriving, but bitrate is too low.<br>"
	"Different actions and overlays can be configured for each.",
	"My external IP keeps changing?",
	"Use DuckDNS (see above). Then you have a fixed address like "
	"<code>mystream.duckdns.org</code>.",
	"SRTLA (Link Aggregation)",
	"SRTLA allows apps like <b>Moblin</b> to use WiFi and mobile data <b>simultaneously</b>. "
	"This makes the connection much more stable &ndash; if one network drops, "
	"the stream continues over the other.",
	"On <a href='https://stools.cc/dashboard/plugin'>stools.cc</a>: Select <b>SRT</b> as protocol and <b>enable SRTLA</b>",
	"Note the SRTLA port (default: <code>5000</code>)",
	"In <b>Moblin</b>: Set protocol to <b>SRT(LA)</b>",
	"Enter <code>&lt;YOUR_IP&gt;:5000</code> as server address "
	"(the SRTLA port, <b>not</b> the SRT port!)",
	"What is SRTLA?",
	"<b>SRTLA</b> (SRT Link Aggregation) bonds multiple network connections "
	"(e.g. WiFi + mobile data) into one. The plugin runs an SRTLA proxy that "
	"receives the packets and forwards them to the internal SRT server.<br>"
	"<b>Default ports:</b> SRTLA = UDP <code>5000</code>, SRT = UDP <code>9000</code><br>"
	"<b>Important:</b> In Moblin, enter the <b>SRTLA port</b> (5000), not the SRT port (9000)!",
};

static QString build_html(const char *local_ip, const char *external_ip,
			  const char *version, const HelpStrings &L)
{
	QString lip = local_ip && local_ip[0] ? local_ip : "?.?.?.?";
	QString eip = external_ip && external_ip[0]
			      ? external_ip
			      : "...";

	QWidget *w = QApplication::activeWindow();
	QPalette pal = w ? w->palette() : QApplication::palette();

	QString bg = pal.color(QPalette::Base).name();
	QString fg = pal.color(QPalette::Text).name();
	QString bg2 = pal.color(QPalette::AlternateBase).name();
	QString accent = pal.color(QPalette::Highlight).name();
	QString dimmed = pal.color(QPalette::PlaceholderText).name();
	QString link = pal.color(QPalette::Link).name();

	return QString(
		"<!DOCTYPE html>"
		"<html><head><meta charset='utf-8'><style>"
		"body { font-family: sans-serif; font-size: 13px; "
		"  background: %1; color: %2; padding: 16px 20px; line-height: 1.55; }"
		"h1 { font-size: 18px; font-weight: 600; margin: 0 0 2px 0; }"
		".ver { color: %3; font-size: 11px; margin-bottom: 18px; }"
		"h2 { font-size: 13px; font-weight: 700; text-transform: uppercase; "
		"  letter-spacing: 1px; color: %3; border-bottom: 1px solid %4; "
		"  padding-bottom: 4px; margin: 22px 0 10px 0; }"
		".ip-row { background: %4; border-radius: 4px; padding: 8px 12px; "
		"  margin-bottom: 6px; }"
		".ip-label { font-size: 11px; color: %3; }"
		".ip-val { font-family: monospace; font-size: 15px; font-weight: 700; "
		"  color: %5; }"
		"ol { padding-left: 22px; margin: 6px 0; }"
		"li { margin-bottom: 6px; }"
		"code { background: %4; padding: 1px 5px; border-radius: 3px; font-size: 12px; }"
		".note { background: %4; border-left: 3px solid %5; "
		"  padding: 8px 12px; border-radius: 0 4px 4px 0; margin: 10px 0; font-size: 12px; }"
		".q { font-weight: 700; color: %5; margin-top: 12px; }"
		".a { margin-bottom: 8px; padding-left: 12px; font-size: 12px; color: %2; }"
		"a { color: %6; }"
		"</style></head><body>")
		       .arg(bg, fg, dimmed, bg2, accent, link)

	       + QString("<h1>%1</h1><div class='ver'>Version %2</div>").arg(L.title).arg(version)

	       + QString("<h2>%1</h2>").arg(L.your_network)
	       + QString("<div class='ip-row'><div class='ip-label'>%1</div>"
			 "<div class='ip-val'>%2</div></div>")
			 .arg(L.local_ip_label)
			 .arg(lip)
	       + QString("<div class='ip-row'><div class='ip-label'>%1</div>"
			 "<div class='ip-val'>%2</div></div>")
			 .arg(L.external_ip_label)
			 .arg(eip)

	       + QString("<h2>%1</h2><p>%2</p>").arg(L.port_fwd).arg(L.port_fwd_intro)
	       + QString("<ol>"
			 "<li>%1</li>"
			 "<li>%2</li>"
			 "<li>%3</li>"
			 "<li>%4</li>"
			 "</ol>")
			 .arg(L.step1)
			 .arg(QString(L.step2).arg(lip))
			 .arg(L.step3)
			 .arg(QString(L.step4).arg(eip))
	       + QString("<div class='note'>%1</div>").arg(QString(L.same_wifi_note).arg(lip))

	       + QString("<h2>%1</h2><p>%2</p>").arg(L.duckdns_title).arg(L.duckdns_intro)
	       + QString("<ol><li>%1</li><li>%2</li><li>%3</li><li>%4</li><li>%5</li></ol>")
			 .arg(L.duck_step1)
			 .arg(L.duck_step2)
			 .arg(L.duck_step3)
			 .arg(L.duck_step4)
			 .arg(L.duck_step5)
	       + QString("<p>%1<br><code>rtmp://meinstream.duckdns.org:1935/live</code></p>").arg(L.duck_example)

	       + QString("<h2>%1</h2><p>%2</p>").arg(L.srtla_title).arg(L.srtla_intro)
	       + QString("<ol><li>%1</li><li>%2</li><li>%3</li><li>%4</li></ol>")
			 .arg(L.srtla_step1)
			 .arg(L.srtla_step2)
			 .arg(L.srtla_step3)
			 .arg(L.srtla_step4)

	       + QString("<h2>%1</h2>").arg(L.faq_title)
	       + QString("<div class='q'>%1</div><div class='a'>%2</div>").arg(L.faq_q1).arg(L.faq_a1)
	       + QString("<div class='q'>%1</div><div class='a'>%2</div>").arg(L.faq_q2).arg(L.faq_a2)
	       + QString("<div class='q'>%1</div><div class='a'>%2</div>").arg(L.faq_q3).arg(L.faq_a3)
	       + QString("<div class='q'>%1</div><div class='a'>%2</div>").arg(L.faq_q4).arg(L.faq_a4)
	       + QString("<div class='q'>%1</div><div class='a'>%2</div>").arg(L.faq_q5).arg(L.faq_a5)
	       + QString("<div class='q'>%1</div><div class='a'>%2</div>").arg(L.faq_q6).arg(L.faq_a6)
	       + QString("<div class='q'>%1</div><div class='a'>%2</div>").arg(L.faq_q7).arg(L.faq_a7)

	       + "</body></html>";
}

extern "C" void help_dialog_show(const char *local_ip,
				 const char *external_ip,
				 const char *version,
				 const char *locale)
{
	bool is_de = locale && (strncmp(locale, "de", 2) == 0);
	const HelpStrings &L = is_de ? LANG_DE : LANG_EN;

	if (g_help_dlg) {
		g_browser->setHtml(
			build_html(local_ip, external_ip, version, L));
		g_help_dlg->show();
		g_help_dlg->raise();
		g_help_dlg->activateWindow();
		return;
	}

	QWidget *parent = (QWidget *)obs_frontend_get_main_window();

	g_help_dlg = new QDialog(parent);
	g_help_dlg->setWindowTitle(
		QString("Easy IRL Stream %1 Help & FAQ")
			.arg(QChar(0x2014)));
	g_help_dlg->resize(580, 700);
	g_help_dlg->setAttribute(Qt::WA_DeleteOnClose);
	QObject::connect(g_help_dlg, &QDialog::destroyed, []() {
		g_help_dlg = nullptr;
		g_browser = nullptr;
	});

	g_browser = new QTextBrowser(g_help_dlg);
	g_browser->setOpenExternalLinks(true);
	g_browser->setHtml(build_html(local_ip, external_ip, version, L));

	QVBoxLayout *layout = new QVBoxLayout(g_help_dlg);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(g_browser);

	g_help_dlg->show();
}

static void open_url(const char *url)
{
	QDesktopServices::openUrl(QUrl(QString::fromUtf8(url)));
}

extern "C" void update_dialog_show(const char *new_version, const char *locale)
{
	bool is_de = locale && (strncmp(locale, "de", 2) == 0);

	QWidget *parent = (QWidget *)obs_frontend_get_main_window();

	QString title = is_de ? QString::fromUtf8("Update verf\xc3\xbc""gbar")
			      : "Update Available";

	QString text = is_de
		? QString::fromUtf8("Eine neue Version (%1) von Easy IRL Stream "
				    "ist verf\xc3\xbc""gbar!\n\n"
				    "M\xc3\xb6""chtest du die Download-Seite "
				    "\xc3\xb6""ffnen?")
			  .arg(new_version)
		: QString("A new version (%1) of Easy IRL Stream is available!"
			  "\n\nWould you like to open the download page?")
			  .arg(new_version);

	QMessageBox::StandardButton reply = QMessageBox::information(
		parent, title, text,
		QMessageBox::Ok | QMessageBox::Cancel);

	if (reply == QMessageBox::Ok) {
		char url[256];
		snprintf(url, sizeof(url), "%s%s%s",
			 obf_https_prefix(), obf_stools_host(),
			 obf_dash_downloads_path());
		open_url(url);
	}
}
