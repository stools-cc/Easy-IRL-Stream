#include <QDockWidget>
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QFont>
#include <QPalette>

#include <obs-frontend-api.h>
#include <util/platform.h>

#include "stats-dialog.hpp"

extern "C" {
#include "irl-source.h"
}

/* ---- helpers ---- */

static QString fmt_bytes(uint64_t b)
{
	if (b < 1024)
		return QString("%1 B").arg(b);
	if (b < 1024ULL * 1024)
		return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
	if (b < 1024ULL * 1024 * 1024)
		return QString("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 1);
	return QString("%1 GB").arg(b / (1024.0 * 1024.0 * 1024.0), 0, 'f',
				    2);
}

static QString fmt_bitrate(int64_t kbps)
{
	if (kbps <= 0)
		return "-";
	if (kbps < 1000)
		return QString("%1 kbps").arg(kbps);
	return QString("%1 Mbps").arg(kbps / 1000.0, 0, 'f', 1);
}

static QString fmt_uptime(uint64_t start_ns)
{
	if (!start_ns)
		return "-";
	uint64_t now = os_gettime_ns();
	uint64_t sec = (now > start_ns) ? (now - start_ns) / 1000000000ULL : 0;
	int h = (int)(sec / 3600);
	int m = (int)((sec % 3600) / 60);
	int s = (int)(sec % 60);
	return QString("%1:%2:%3")
		.arg(h, 2, 10, QChar('0'))
		.arg(m, 2, 10, QChar('0'))
		.arg(s, 2, 10, QChar('0'));
}

/* ---- widget ---- */

static const char *status_colors[] = {"#888888", "#e0a020", "#20c040",
				      "#e04040"};

class StreamStatsWidget : public QWidget {
public:
	StreamStatsWidget(bool is_de, QWidget *parent = nullptr)
		: QWidget(parent),
		  m_de(is_de)
	{
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
		buildUI();

		m_timer = new QTimer(this);
		QObject::connect(m_timer, &QTimer::timeout,
				 [this]() { refresh(); });
		m_timer->start(500);
		refresh();
	}

private:
	bool m_de;
	QLabel *m_dot, *m_status;
	QLabel *m_lbls[4], *m_vals[4];
	QLabel *m_videoLine, *m_audioLine, *m_serverLine, *m_ipLine;
	QTimer *m_timer;
	int64_t m_prevFrames = 0;
	uint64_t m_prevTime = 0;
	double m_fps = 0.0;

	QLabel *makeLabel(const QString &text, int ptDelta, bool bold,
			  const QString &color)
	{
		auto *l = new QLabel(text, this);
		QFont f = font();
		f.setPointSize(f.pointSize() + ptDelta);
		f.setBold(bold);
		l->setFont(f);
		if (!color.isEmpty())
			l->setStyleSheet(
				QString("QLabel{color:%1}").arg(color));
		l->setTextInteractionFlags(Qt::NoTextInteraction);
		return l;
	}

	void buildUI()
	{
		QString dim = palette()
				      .color(QPalette::PlaceholderText)
				      .name();
		QString acc = palette().color(QPalette::Highlight).name();

		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(12, 10, 12, 10);
		root->setSpacing(8);

		/* Row 1 — status */
		auto *row1 = new QHBoxLayout();
		row1->setSpacing(8);

		m_dot = new QLabel(this);
		m_dot->setFixedSize(10, 10);
		m_dot->setStyleSheet(
			"QLabel{background:#888;border-radius:5px;"
			"min-width:10px;min-height:10px}");
		row1->addWidget(m_dot, 0, Qt::AlignVCenter);

		m_status = makeLabel(m_de ? "Inaktiv" : "Idle", 1, true, "");
		row1->addWidget(m_status, 0, Qt::AlignVCenter);
		row1->addStretch();
		root->addLayout(row1);

		/* Row 2 — stats grid */
		auto *grid = new QGridLayout();
		grid->setHorizontalSpacing(12);
		grid->setVerticalSpacing(2);
		for (int c = 0; c < 4; c++)
			grid->setColumnStretch(c, 1);

		QFont lblFont = font();
		lblFont.setPointSize(lblFont.pointSize() - 2);
		lblFont.setBold(true);

		QFont valFont("Consolas", font().pointSize() + 2);
		valFont.setBold(true);

		QString headers[4] = {"BITRATE", "FPS",
				      m_de ? "UPTIME" : "UPTIME",
				      m_de ? "DATEN" : "DATA"};

		for (int c = 0; c < 4; c++) {
			m_lbls[c] = new QLabel(headers[c], this);
			m_lbls[c]->setFont(lblFont);
			m_lbls[c]->setStyleSheet(
				QString("QLabel{color:%1}").arg(dim));
			m_lbls[c]->setTextInteractionFlags(
				Qt::NoTextInteraction);
			m_lbls[c]->setAlignment(Qt::AlignLeft |
						Qt::AlignBottom);
			grid->addWidget(m_lbls[c], 0, c);

			m_vals[c] = new QLabel("-", this);
			m_vals[c]->setFont(valFont);
			m_vals[c]->setStyleSheet(
				QString("QLabel{color:%1}").arg(acc));
			m_vals[c]->setTextInteractionFlags(
				Qt::NoTextInteraction);
			m_vals[c]->setAlignment(Qt::AlignLeft |
						Qt::AlignTop);
			grid->addWidget(m_vals[c], 1, c);
		}
		root->addLayout(grid);

		/* Separator */
		auto *sep = new QFrame(this);
		sep->setFrameShape(QFrame::HLine);
		sep->setFrameShadow(QFrame::Sunken);
		root->addWidget(sep);

		/* Info lines */
		m_videoLine = makeLabel("-", -1, false, "");
		m_audioLine = makeLabel("-", -1, false, "");
		m_serverLine = makeLabel("-", -1, false, dim);
		m_ipLine = makeLabel("-", -1, false, dim);
		root->addWidget(m_videoLine);
		root->addWidget(m_audioLine);
		root->addWidget(m_serverLine);
		root->addWidget(m_ipLine);

		root->addStretch();
	}

	void refresh()
	{
		struct irl_source_data *d = nullptr;
		for (int i = 0; i < g_irl_source_count && i < MAX_IRL_SOURCES;
		     i++) {
			if (g_irl_sources[i]) {
				d = g_irl_sources[i];
				break;
			}
		}

		if (!d) {
			setNoSource();
			return;
		}

		long state = os_atomic_load_long(&d->connection_state);
		if (state < 0 || state > 3)
			state = 0;
		bool conn = (state == CONN_STATE_CONNECTED);

		static const char *de[] = {"Inaktiv", "Wartet\xe2\x80\xa6",
					   "Verbunden", "Getrennt"};
		static const char *en[] = {"Idle", "Listening\xe2\x80\xa6",
					   "Connected", "Disconnected"};

		m_dot->setStyleSheet(
			QString("QLabel{background:%1;border-radius:5px;"
				"min-width:10px;min-height:10px}")
				.arg(status_colors[state]));
		m_status->setText(m_de ? de[state] : en[state]);

		/* Bitrate */
		m_vals[0]->setText(
			conn ? fmt_bitrate(d->current_bitrate_kbps) : "-");

		/* FPS */
		uint64_t now = os_gettime_ns();
		int64_t frames = d->stats_total_frames;
		if (m_prevTime > 0 && now > m_prevTime) {
			double dt = (double)(now - m_prevTime) / 1e9;
			int64_t df = frames - m_prevFrames;
			if (dt > 0.05 && df >= 0)
				m_fps = df / dt;
		}
		m_prevFrames = frames;
		m_prevTime = now;
		m_vals[1]->setText(conn && m_fps > 0.1
					   ? QString::number(m_fps, 'f', 1)
					   : "-");

		/* Uptime */
		m_vals[2]->setText(
			conn ? fmt_uptime(d->stats_connect_time_ns) : "-");

		/* Data */
		uint64_t tb = d->stats_total_bytes;
		m_vals[3]->setText(tb > 0 ? fmt_bytes(tb) : "-");

		/* Video */
		if (d->stats_video_codec[0]) {
			QString v = QString("Video:  %1  %2\u00d7%3")
					    .arg(QString(d->stats_video_codec)
							 .toUpper())
					    .arg(d->stats_video_width)
					    .arg(d->stats_video_height);
			if (d->stats_video_pixfmt[0])
				v += QString("  %1").arg(d->stats_video_pixfmt);
			v += QString("  \u00b7  Frames: %L1").arg(frames);
			m_videoLine->setText(v);
		} else {
			m_videoLine->setText("-");
		}

		/* Audio */
		if (d->stats_audio_codec[0])
			m_audioLine->setText(
				QString("Audio:  %1  %2 Hz")
					.arg(QString(d->stats_audio_codec)
						     .toUpper())
					.arg(d->stats_audio_sample_rate));
		else
			m_audioLine->setText("-");

		/* Server */
		pthread_mutex_lock(&d->mutex);
		int proto = d->protocol;
		int port = d->port;
		bool srtla = d->srtla_enabled;
		int srtla_p = d->srtla_port;
		pthread_mutex_unlock(&d->mutex);

		QString s = QString("%1 \u00b7 Port %2")
				    .arg(proto == PROTOCOL_RTMP ? "RTMP"
							       : "SRT")
				    .arg(port);
		if (srtla)
			s += QString(" \u00b7 SRTLA \u2713 (:%1)")
				     .arg(srtla_p);
		m_serverLine->setText(s);

		QString ip_text;
		if (g_local_ip[0] && g_external_ip[0])
			ip_text = QString("LAN: %1  \u00b7  WAN: %2")
					  .arg(g_local_ip)
					  .arg(g_external_ip);
		else if (g_local_ip[0])
			ip_text = QString("LAN: %1").arg(g_local_ip);
		else
			ip_text = "-";
		m_ipLine->setText(ip_text);
	}

	void setNoSource()
	{
		m_dot->setStyleSheet(
			"QLabel{background:#888;border-radius:5px;"
			"min-width:10px;min-height:10px}");
		m_status->setText(m_de ? "Keine Quelle" : "No source");
		for (int c = 0; c < 4; c++)
			m_vals[c]->setText("-");
		m_videoLine->setText("-");
		m_audioLine->setText("-");
		m_serverLine->setText("-");
		m_ipLine->setText("-");
		m_fps = 0;
		m_prevFrames = 0;
		m_prevTime = 0;
	}
};

/* ---- dock creation ---- */

static QDockWidget *g_dock = nullptr;

extern "C" void stats_dialog_show(const char *locale)
{
	if (g_dock) {
		g_dock->setVisible(!g_dock->isVisible());
		if (g_dock->isVisible()) {
			g_dock->raise();
			g_dock->activateWindow();
		}
		return;
	}

	bool is_de = locale && (strncmp(locale, "de", 2) == 0);

	QMainWindow *main = (QMainWindow *)obs_frontend_get_main_window();
	if (!main)
		return;

	g_dock = new QDockWidget(
		QString("Easy IRL Stream \u2014 Monitor"), main);
	g_dock->setObjectName("EasyIRLStreamMonitorDock");
	g_dock->setWidget(new StreamStatsWidget(is_de, g_dock));
	g_dock->setAllowedAreas(Qt::AllDockWidgetAreas);
	g_dock->setFeatures(QDockWidget::DockWidgetMovable |
			    QDockWidget::DockWidgetFloatable |
			    QDockWidget::DockWidgetClosable);

	QObject::connect(g_dock, &QDockWidget::destroyed,
			 []() { g_dock = nullptr; });

	main->addDockWidget(Qt::BottomDockWidgetArea, g_dock);
	g_dock->setFloating(true);
	g_dock->resize(480, 230);
	g_dock->show();
}
