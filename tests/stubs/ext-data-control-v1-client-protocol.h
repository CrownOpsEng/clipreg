#ifndef EXT_DATA_CONTROL_STUB_H
#define EXT_DATA_CONTROL_STUB_H
#include <stdint.h>
#include "wayland-client.h"
struct ext_data_control_manager_v1; struct ext_data_control_device_v1; struct ext_data_control_source_v1; struct ext_data_control_offer_v1;
extern const struct wl_interface ext_data_control_manager_v1_interface;
struct ext_data_control_offer_v1_listener { void (*offer)(void *, struct ext_data_control_offer_v1 *, const char *); };
struct ext_data_control_source_v1_listener {
    void (*send)(void *, struct ext_data_control_source_v1 *, const char *, int32_t);
    void (*cancelled)(void *, struct ext_data_control_source_v1 *);
};
struct ext_data_control_device_v1_listener {
    void (*data_offer)(void *, struct ext_data_control_device_v1 *, struct ext_data_control_offer_v1 *);
    void (*selection)(void *, struct ext_data_control_device_v1 *, struct ext_data_control_offer_v1 *);
    void (*finished)(void *, struct ext_data_control_device_v1 *);
    void (*primary_selection)(void *, struct ext_data_control_device_v1 *, struct ext_data_control_offer_v1 *);
};
int ext_data_control_offer_v1_add_listener(struct ext_data_control_offer_v1 *, const struct ext_data_control_offer_v1_listener *, void *);
void ext_data_control_offer_v1_set_user_data(struct ext_data_control_offer_v1 *, void *);
void *ext_data_control_offer_v1_get_user_data(struct ext_data_control_offer_v1 *);
void ext_data_control_offer_v1_destroy(struct ext_data_control_offer_v1 *);
void ext_data_control_offer_v1_receive(struct ext_data_control_offer_v1 *, const char *, int32_t);
struct ext_data_control_source_v1 *ext_data_control_manager_v1_create_data_source(struct ext_data_control_manager_v1 *);
struct ext_data_control_device_v1 *ext_data_control_manager_v1_get_data_device(struct ext_data_control_manager_v1 *, struct wl_seat *);
int ext_data_control_source_v1_add_listener(struct ext_data_control_source_v1 *, const struct ext_data_control_source_v1_listener *, void *);
void ext_data_control_source_v1_offer(struct ext_data_control_source_v1 *, const char *);
void ext_data_control_source_v1_destroy(struct ext_data_control_source_v1 *);
void ext_data_control_device_v1_set_selection(struct ext_data_control_device_v1 *, struct ext_data_control_source_v1 *);
void ext_data_control_device_v1_set_primary_selection(struct ext_data_control_device_v1 *, struct ext_data_control_source_v1 *);
int ext_data_control_device_v1_add_listener(struct ext_data_control_device_v1 *, const struct ext_data_control_device_v1_listener *, void *);
#endif
