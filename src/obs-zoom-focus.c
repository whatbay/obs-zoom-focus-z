#include <obs-module.h>
#include <obs.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/dstr.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-focus", "ja-JP")

struct zoom_data {
    obs_source_t *source;
    gs_effect_t *effect;

    float zoom;
    float center_x;
    float center_y;

    bool click_focus;
    bool drag_move;
    bool wheel_zoom;
    bool reset_double_click;

    bool dragging;
    float last_x;
    float last_y;
};

static const char *zoom_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return "Zoom Focus Filter";
}

static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void zoom_reset(struct zoom_data *d)
{
    d->zoom = 1.0f;
    d->center_x = 0.5f;
    d->center_y = 0.5f;
    d->dragging = false;
}

static void zoom_update(void *data, obs_data_t *settings)
{
    struct zoom_data *d = data;

    d->zoom = (float)obs_data_get_double(settings, "zoom");
    d->center_x = (float)obs_data_get_double(settings, "center_x");
    d->center_y = (float)obs_data_get_double(settings, "center_y");

    d->click_focus = obs_data_get_bool(settings, "click_focus");
    d->drag_move = obs_data_get_bool(settings, "drag_move");
    d->wheel_zoom = obs_data_get_bool(settings, "wheel_zoom");
    d->reset_double_click = obs_data_get_bool(settings, "reset_double_click");

    d->zoom = clampf_local(d->zoom, 1.0f, 12.0f);
    d->center_x = clampf_local(d->center_x, 0.0f, 1.0f);
    d->center_y = clampf_local(d->center_y, 0.0f, 1.0f);
}

static void zoom_defaults(obs_data_t *settings)
{
    obs_data_set_default_double(settings, "zoom", 2.0);
    obs_data_set_default_double(settings, "center_x", 0.5);
    obs_data_set_default_double(settings, "center_y", 0.5);

    obs_data_set_default_bool(settings, "click_focus", true);
    obs_data_set_default_bool(settings, "drag_move", true);
    obs_data_set_default_bool(settings, "wheel_zoom", true);
    obs_data_set_default_bool(settings, "reset_double_click", true);
}

static obs_properties_t *zoom_properties(void *data)
{
    UNUSED_PARAMETER(data);

    obs_properties_t *p = obs_properties_create();

    obs_properties_add_float_slider(p, "zoom", "拡大倍率", 1.0, 12.0, 0.1);
    obs_properties_add_float_slider(p, "center_x", "中心 X", 0.0, 1.0, 0.01);
    obs_properties_add_float_slider(p, "center_y", "中心 Y", 0.0, 1.0, 0.01);

    obs_properties_add_bool(p, "click_focus", "クリックでフォーカス");
    obs_properties_add_bool(p, "drag_move", "ドラッグで移動");
    obs_properties_add_bool(p, "wheel_zoom", "ホイールで拡大縮小");
    obs_properties_add_bool(p, "reset_double_click", "ダブルクリックでリセット");

    return p;
}

static void *zoom_create(obs_data_t *settings, obs_source_t *source)
{
    struct zoom_data *d = bzalloc(sizeof(*d));
    d->source = source;

    char *effect_path = obs_module_file("zoom-focus.effect");
    d->effect = gs_effect_create_from_file(effect_path, NULL);
    bfree(effect_path);

    zoom_update(d, settings);
    return d;
}

static void zoom_destroy(void *data)
{
    struct zoom_data *d = data;
    if (!d)
        return;

    if (d->effect)
        gs_effect_destroy(d->effect);

    bfree(d);
}

static void zoom_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);

    struct zoom_data *d = data;
    if (!d || !d->effect)
        return;

    if (!obs_source_process_filter_begin(d->source, GS_RGBA,
                                         OBS_ALLOW_DIRECT_RENDERING))
        return;

    gs_eparam_t *zoom_param =
        gs_effect_get_param_by_name(d->effect, "zoom");
    gs_eparam_t *center_param =
        gs_effect_get_param_by_name(d->effect, "center");

    if (zoom_param)
        gs_effect_set_float(zoom_param, d->zoom);

    if (center_param) {
        struct vec2 center = {d->center_x, d->center_y};
        gs_effect_set_vec2(center_param, &center);
    }

    obs_source_process_filter_end(d->source, d->effect, 0, 0);
}

static void event_to_uv(struct zoom_data *d,
                        const struct obs_mouse_event *event,
                        float *x, float *y)
{
    uint32_t w = obs_source_get_width(d->source);
    uint32_t h = obs_source_get_height(d->source);

    if (!w || !h) {
        *x = 0.5f;
        *y = 0.5f;
        return;
    }

    *x = clampf_local((float)event->x / (float)w, 0.0f, 1.0f);
    *y = clampf_local((float)event->y / (float)h, 0.0f, 1.0f);
}

static void zoom_mouse_click(void *data,
                             const struct obs_mouse_event *event,
                             int32_t type,
                             bool mouse_up,
                             uint32_t click_count)
{
    struct zoom_data *d = data;

    /* OBS_BUTTON_LEFT is defined by OBS for the primary mouse button. */
    if (type != OBS_MOUSE_LEFT)
        return;

    float x, y;
    event_to_uv(d, event, &x, &y);

    if (mouse_up) {
        d->dragging = false;

        if (d->reset_double_click && click_count >= 2) {
            zoom_reset(d);
        }
        return;
    }

    d->dragging = d->drag_move;

    if (d->click_focus) {
        d->center_x = x;
        d->center_y = y;
    }

    d->last_x = x;
    d->last_y = y;
}

static void zoom_mouse_move(void *data,
                            const struct obs_mouse_event *event,
                            bool mouse_leave)
{
    struct zoom_data *d = data;

    if (mouse_leave) {
        d->dragging = false;
        return;
    }

    if (!d->dragging || !d->drag_move)
        return;

    float x, y;
    event_to_uv(d, event, &x, &y);

    float dx = x - d->last_x;
    float dy = y - d->last_y;

    /*
     * Move the selected area in the opposite direction of the mouse
     * movement, so the image follows the drag gesture naturally.
     */
    d->center_x = clampf_local(d->center_x - dx / d->zoom, 0.0f, 1.0f);
    d->center_y = clampf_local(d->center_y - dy / d->zoom, 0.0f, 1.0f);

    d->last_x = x;
    d->last_y = y;
}

static void zoom_mouse_wheel(void *data,
                             const struct obs_mouse_event *event,
                             int x_delta,
                             int y_delta)
{
    UNUSED_PARAMETER(event);
    UNUSED_PARAMETER(x_delta);

    struct zoom_data *d = data;

    if (!d->wheel_zoom)
        return;

    if (y_delta > 0)
        d->zoom += 0.25f;
    else if (y_delta < 0)
        d->zoom -= 0.25f;

    d->zoom = clampf_local(d->zoom, 1.0f, 12.0f);
}

static void zoom_key_click(void *data,
                           const struct obs_key_event *event,
                           bool key_up)
{
    struct zoom_data *d = data;

    if (key_up)
        return;

    if (event->native_vkey == 0x1B) { /* Escape */
        zoom_reset(d);
    }
}

static struct obs_source_info zoom_info = {
    .id = "obs_zoom_focus_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION,

    .get_name = zoom_get_name,
    .create = zoom_create,
    .destroy = zoom_destroy,
    .get_defaults = zoom_defaults,
    .get_properties = zoom_properties,
    .update = zoom_update,
    .video_render = zoom_render,

    .mouse_click = zoom_mouse_click,
    .mouse_move = zoom_mouse_move,
    .mouse_wheel = zoom_mouse_wheel,
    .key_click = zoom_key_click,
};

bool obs_module_load(void)
{
    obs_register_source(&zoom_info);
    blog(LOG_INFO, "[Zoom Focus] practical edition loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[Zoom Focus] unloaded");
}
