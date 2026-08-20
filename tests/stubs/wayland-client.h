#ifndef WAYLAND_CLIENT_H
#define WAYLAND_CLIENT_H
#include <stddef.h>
#include <stdint.h>
struct wl_display; struct wl_registry; struct wl_seat; struct wl_output;
struct wl_interface { const char *name; };
extern const struct wl_interface wl_seat_interface;
struct wl_array { size_t size; size_t alloc; void *data; };
#define wl_array_for_each(pos, array) \
    for ((pos) = (array)->data; (const char *)(pos) < ((const char *)(array)->data + (array)->size); (pos)++)
struct wl_registry_listener {
    void (*global)(void *, struct wl_registry *, uint32_t, const char *, uint32_t);
    void (*global_remove)(void *, struct wl_registry *, uint32_t);
};
struct wl_display *wl_display_connect(const char *name);
void wl_display_disconnect(struct wl_display *display);
struct wl_registry *wl_display_get_registry(struct wl_display *display);
int wl_registry_add_listener(struct wl_registry *, const struct wl_registry_listener *, void *);
void *wl_registry_bind(struct wl_registry *, uint32_t, const struct wl_interface *, uint32_t);
int wl_display_roundtrip(struct wl_display *);
int wl_display_get_fd(struct wl_display *);
int wl_display_prepare_read(struct wl_display *);
void wl_display_cancel_read(struct wl_display *);
int wl_display_read_events(struct wl_display *);
int wl_display_dispatch_pending(struct wl_display *);
int wl_display_dispatch(struct wl_display *);
int wl_display_flush(struct wl_display *);
#endif
