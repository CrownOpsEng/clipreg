#ifndef COSMIC_TOPLEVEL_STUB_H
#define COSMIC_TOPLEVEL_STUB_H
#include "wayland-client.h"
struct zcosmic_toplevel_info_v1; struct zcosmic_toplevel_handle_v1; struct zcosmic_workspace_handle_v1;
extern const struct wl_interface zcosmic_toplevel_info_v1_interface;
enum zcosmic_toplevel_handle_v1_state {
    ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED=0,
    ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED=1,
    ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED=2,
    ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN=3
};
struct zcosmic_toplevel_info_v1_listener {
    void (*toplevel)(void *, struct zcosmic_toplevel_info_v1 *, struct zcosmic_toplevel_handle_v1 *);
    void (*finished)(void *, struct zcosmic_toplevel_info_v1 *);
};
struct zcosmic_toplevel_handle_v1_listener {
    void (*closed)(void *, struct zcosmic_toplevel_handle_v1 *);
    void (*done)(void *, struct zcosmic_toplevel_handle_v1 *);
    void (*title)(void *, struct zcosmic_toplevel_handle_v1 *, const char *);
    void (*app_id)(void *, struct zcosmic_toplevel_handle_v1 *, const char *);
    void (*output_enter)(void *, struct zcosmic_toplevel_handle_v1 *, struct wl_output *);
    void (*output_leave)(void *, struct zcosmic_toplevel_handle_v1 *, struct wl_output *);
    void (*workspace_enter)(void *, struct zcosmic_toplevel_handle_v1 *, struct zcosmic_workspace_handle_v1 *);
    void (*workspace_leave)(void *, struct zcosmic_toplevel_handle_v1 *, struct zcosmic_workspace_handle_v1 *);
    void (*state)(void *, struct zcosmic_toplevel_handle_v1 *, struct wl_array *);
};
int zcosmic_toplevel_info_v1_add_listener(struct zcosmic_toplevel_info_v1 *, const struct zcosmic_toplevel_info_v1_listener *, void *);
int zcosmic_toplevel_handle_v1_add_listener(struct zcosmic_toplevel_handle_v1 *, const struct zcosmic_toplevel_handle_v1_listener *, void *);
void zcosmic_toplevel_handle_v1_destroy(struct zcosmic_toplevel_handle_v1 *);
#endif
