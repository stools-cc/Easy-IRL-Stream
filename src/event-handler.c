#include "event-handler.h"
#include "webhook.h"
#include <util/platform.h>

static struct webhook_event_data snapshot_event_data(struct irl_source_data *data)
{
	struct webhook_event_data ed = {0};
	ed.bitrate_kbps = data->current_bitrate_kbps;
	ed.video_width = data->stats_video_width;
	ed.video_height = data->stats_video_height;
	ed.video_codec = data->stats_video_codec[0]
				 ? data->stats_video_codec
				 : NULL;

	uint64_t conn_ns = data->stats_connect_time_ns;
	if (conn_ns > 0) {
		uint64_t now = os_gettime_ns();
		ed.uptime_sec = (int64_t)((now - conn_ns) / 1000000000ULL);
	}
	return ed;
}

/* ---- queued tasks executed on the UI thread ---- */

struct scene_switch_ctx {
	char *scene_name;
};

static void task_switch_scene(void *param)
{
	struct scene_switch_ctx *ctx = param;
	obs_source_t *scene = obs_get_source_by_name(ctx->scene_name);
	if (scene) {
		obs_frontend_set_current_scene(scene);
		obs_source_release(scene);
	}
	bfree(ctx->scene_name);
	bfree(ctx);
}

struct overlay_ctx {
	char *source_name;
	bool visible;
};

static void task_set_overlay(void *param)
{
	struct overlay_ctx *ctx = param;

	obs_source_t *current = obs_frontend_get_current_scene();
	if (current) {
		obs_scene_t *scene = obs_scene_from_source(current);
		if (scene) {
			obs_sceneitem_t *item = obs_scene_find_source(
				scene, ctx->source_name);
			if (item)
				obs_sceneitem_set_visible(item, ctx->visible);
		}
		obs_source_release(current);
	}

	bfree(ctx->source_name);
	bfree(ctx);
}

struct recording_ctx {
	int action;
};

static void task_recording(void *param)
{
	struct recording_ctx *ctx = param;
	if (ctx->action == RECORDING_ACTION_START)
		obs_frontend_recording_start();
	else if (ctx->action == RECORDING_ACTION_STOP)
		obs_frontend_recording_stop();
	bfree(ctx);
}

/* ---- helpers ---- */

static void queue_scene_switch(const char *scene_name)
{
	if (!scene_name || !scene_name[0])
		return;
	struct scene_switch_ctx *ctx = bzalloc(sizeof(*ctx));
	ctx->scene_name = bstrdup(scene_name);
	obs_queue_task(OBS_TASK_UI, task_switch_scene, ctx, false);
}

static void queue_overlay(const char *source_name, bool visible)
{
	if (!source_name || !source_name[0])
		return;
	struct overlay_ctx *ctx = bzalloc(sizeof(*ctx));
	ctx->source_name = bstrdup(source_name);
	ctx->visible = visible;
	obs_queue_task(OBS_TASK_UI, task_set_overlay, ctx, false);
}

static void queue_recording(int action)
{
	if (action == RECORDING_ACTION_NONE)
		return;
	struct recording_ctx *ctx = bzalloc(sizeof(*ctx));
	ctx->action = action;
	obs_queue_task(OBS_TASK_UI, task_recording, ctx, false);
}

/* ---- fire low-quality / quality-recovered actions ---- */

static void fire_low_quality_actions(struct irl_source_data *data)
{
	pthread_mutex_lock(&data->mutex);
	if (data->low_quality_actions_fired) {
		pthread_mutex_unlock(&data->mutex);
		return;
	}
	data->low_quality_actions_fired = true;

	char *scene = data->low_quality_scene_name
			      ? bstrdup(data->low_quality_scene_name)
			      : NULL;
	char *overlay = data->low_quality_overlay_name
				? bstrdup(data->low_quality_overlay_name)
				: NULL;
	char *webhook = data->webhook_url ? bstrdup(data->webhook_url) : NULL;
	char *cmd = data->custom_command ? bstrdup(data->custom_command)
					 : NULL;
	const char *src_name = obs_source_get_name(data->source);
	char *src_copy = bstrdup(src_name ? src_name : "Easy IRL Stream");
	pthread_mutex_unlock(&data->mutex);

	blog(LOG_DEBUG, "[%s] Low quality detected (%lld kbps)", PLUGIN_NAME,
	     (long long)data->current_bitrate_kbps);

	queue_scene_switch(scene);
	queue_overlay(overlay, true);

	if (webhook && webhook[0]) {
		struct webhook_event_data ed = snapshot_event_data(data);
		webhook_send_async(webhook, "low_quality", src_copy, &ed);
	}
	if (cmd && cmd[0])
		webhook_execute_command_async(cmd);

	bfree(scene);
	bfree(overlay);
	bfree(webhook);
	bfree(cmd);
	bfree(src_copy);
}

static void fire_quality_recovered_actions(struct irl_source_data *data)
{
	pthread_mutex_lock(&data->mutex);
	data->low_quality_actions_fired = false;
	data->low_quality_active = false;

	char *scene = data->reconnect_scene_name
			      ? bstrdup(data->reconnect_scene_name)
			      : NULL;
	char *overlay = data->low_quality_overlay_name
				? bstrdup(data->low_quality_overlay_name)
				: NULL;
	char *webhook = data->webhook_url ? bstrdup(data->webhook_url) : NULL;
	const char *src_name = obs_source_get_name(data->source);
	char *src_copy = bstrdup(src_name ? src_name : "Easy IRL Stream");
	pthread_mutex_unlock(&data->mutex);

	blog(LOG_DEBUG, "[%s] Quality recovered (%lld kbps)", PLUGIN_NAME,
	     (long long)data->current_bitrate_kbps);

	queue_scene_switch(scene);
	queue_overlay(overlay, false);

	if (webhook && webhook[0]) {
		struct webhook_event_data ed = snapshot_event_data(data);
		webhook_send_async(webhook, "quality_recovered", src_copy, &ed);
	}

	bfree(scene);
	bfree(overlay);
	bfree(webhook);
	bfree(src_copy);
}

/* ---- fire disconnect / reconnect actions ---- */

static void fire_disconnect_actions(struct irl_source_data *data)
{
	pthread_mutex_lock(&data->mutex);
	if (data->disconnect_actions_fired) {
		pthread_mutex_unlock(&data->mutex);
		return;
	}
	data->disconnect_actions_fired = true;

	char *scene = data->disconnect_scene_name
			      ? bstrdup(data->disconnect_scene_name)
			      : NULL;
	char *overlay = data->overlay_source_name
				? bstrdup(data->overlay_source_name)
				: NULL;
	int rec_action = data->disconnect_recording_action;
	char *webhook = data->webhook_url ? bstrdup(data->webhook_url) : NULL;
	char *cmd = data->custom_command ? bstrdup(data->custom_command)
					 : NULL;
	const char *src_name = obs_source_get_name(data->source);
	char *src_copy = bstrdup(src_name ? src_name : "Easy IRL Stream");
	pthread_mutex_unlock(&data->mutex);

	blog(LOG_DEBUG, "[%s] Firing disconnect actions", PLUGIN_NAME);

	queue_scene_switch(scene);
	queue_overlay(overlay, true);
	queue_recording(rec_action);

	if (webhook && webhook[0]) {
		struct webhook_event_data ed = snapshot_event_data(data);
		webhook_send_async(webhook, "disconnect", src_copy, &ed);
	}
	if (cmd && cmd[0])
		webhook_execute_command_async(cmd);

	/* Clear last video frame so OBS shows nothing */
	obs_source_output_video(data->source, NULL);

	bfree(scene);
	bfree(overlay);
	bfree(webhook);
	bfree(cmd);
	bfree(src_copy);
}

static void fire_reconnect_actions(struct irl_source_data *data)
{
	pthread_mutex_lock(&data->mutex);
	char *scene = data->reconnect_scene_name
			      ? bstrdup(data->reconnect_scene_name)
			      : NULL;
	char *overlay = data->overlay_source_name
				? bstrdup(data->overlay_source_name)
				: NULL;
	char *webhook = data->webhook_url ? bstrdup(data->webhook_url) : NULL;
	char *cmd = data->custom_command ? bstrdup(data->custom_command)
					 : NULL;
	const char *src_name = obs_source_get_name(data->source);
	char *src_copy = bstrdup(src_name ? src_name : "Easy IRL Stream");
	pthread_mutex_unlock(&data->mutex);

	blog(LOG_DEBUG, "[%s] Firing reconnect actions", PLUGIN_NAME);

	queue_scene_switch(scene);
	queue_overlay(overlay, false);

	if (webhook && webhook[0]) {
		struct webhook_event_data ed = snapshot_event_data(data);
		webhook_send_async(webhook, "reconnect", src_copy, &ed);
	}
	if (cmd && cmd[0])
		webhook_execute_command_async(cmd);

	bfree(scene);
	bfree(overlay);
	bfree(webhook);
	bfree(cmd);
	bfree(src_copy);
}

/* ---- public API ---- */

void event_handler_on_connect(struct irl_source_data *data)
{
	blog(LOG_DEBUG, "[%s] Client connected", PLUGIN_NAME);

	bool was_disconnected;
	pthread_mutex_lock(&data->mutex);
	was_disconnected = data->disconnect_actions_fired;
	data->low_quality_active = false;
	data->low_quality_actions_fired = false;
	data->last_bitrate_check_ns = 0;
	data->current_bitrate_kbps = 0;
	pthread_mutex_unlock(&data->mutex);

	os_atomic_set_long(&data->bytes_window, 0);

	if (was_disconnected) {
		fire_reconnect_actions(data);
		pthread_mutex_lock(&data->mutex);
		data->disconnect_actions_fired = false;
		pthread_mutex_unlock(&data->mutex);
	}
}

void event_handler_on_disconnect(struct irl_source_data *data)
{
	blog(LOG_DEBUG, "[%s] Client disconnected", PLUGIN_NAME);

	pthread_mutex_lock(&data->mutex);
	data->disconnect_time_ns = os_gettime_ns();
	int timeout = data->disconnect_timeout_sec;
	pthread_mutex_unlock(&data->mutex);

	if (timeout <= 0)
		fire_disconnect_actions(data);
}

static void check_quality(struct irl_source_data *data)
{
	uint64_t now = os_gettime_ns();

	if (data->last_bitrate_check_ns == 0) {
		data->last_bitrate_check_ns = now;
		return;
	}

	uint64_t elapsed = now - data->last_bitrate_check_ns;
	if (elapsed < 1000000000ULL)
		return;

	long bytes = os_atomic_exchange_long(&data->bytes_window, 0);
	double seconds = (double)elapsed / 1000000000.0;
	data->current_bitrate_kbps =
		(int64_t)((bytes * 8.0) / (seconds * 1000.0));
	data->last_bitrate_check_ns = now;

	pthread_mutex_lock(&data->mutex);
	bool enabled = data->low_quality_enabled;
	int threshold = data->low_quality_bitrate_kbps;
	int timeout = data->low_quality_timeout_sec;
	bool was_active = data->low_quality_active;
	bool was_fired = data->low_quality_actions_fired;
	pthread_mutex_unlock(&data->mutex);

	if (!enabled)
		return;

	bool is_low = data->current_bitrate_kbps < threshold &&
		      data->current_bitrate_kbps > 0;

	if (is_low) {
		if (!was_active) {
			pthread_mutex_lock(&data->mutex);
			data->low_quality_active = true;
			data->low_quality_start_ns = now;
			pthread_mutex_unlock(&data->mutex);
		} else if (!was_fired) {
			pthread_mutex_lock(&data->mutex);
			uint64_t lq_elapsed =
				now - data->low_quality_start_ns;
			pthread_mutex_unlock(&data->mutex);

			uint64_t timeout_ns =
				(uint64_t)timeout * 1000000000ULL;
			if (lq_elapsed >= timeout_ns)
				fire_low_quality_actions(data);
		}
	} else if (was_active) {
		if (was_fired)
			fire_quality_recovered_actions(data);
		else {
			pthread_mutex_lock(&data->mutex);
			data->low_quality_active = false;
			pthread_mutex_unlock(&data->mutex);
		}
	}
}

void event_handler_tick(struct irl_source_data *data)
{
	long state = os_atomic_load_long(&data->connection_state);

	if (state == CONN_STATE_CONNECTED)
		check_quality(data);

	if (state != CONN_STATE_DISCONNECTED)
		return;

	pthread_mutex_lock(&data->mutex);
	bool already_fired = data->disconnect_actions_fired;
	uint64_t disc_time = data->disconnect_time_ns;
	int timeout = data->disconnect_timeout_sec;
	pthread_mutex_unlock(&data->mutex);

	if (already_fired || timeout <= 0)
		return;

	uint64_t elapsed_ns = os_gettime_ns() - disc_time;
	uint64_t timeout_ns = (uint64_t)timeout * 1000000000ULL;

	if (elapsed_ns >= timeout_ns)
		fire_disconnect_actions(data);
}

int64_t event_handler_get_bitrate(struct irl_source_data *data)
{
	return data->current_bitrate_kbps;
}
