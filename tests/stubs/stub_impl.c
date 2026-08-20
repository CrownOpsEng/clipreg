#include "wayland-client.h"
#include "ext-data-control-v1-client-protocol.h"
#include "cosmic-toplevel-info-unstable-v1-client-protocol.h"
const struct wl_interface wl_seat_interface={"wl_seat"};
const struct wl_interface ext_data_control_manager_v1_interface={"ext_data_control_manager_v1"};
const struct wl_interface zcosmic_toplevel_info_v1_interface={"zcosmic_toplevel_info_v1"};
struct wl_display *wl_display_connect(const char*x){(void)x;return 0;} void wl_display_disconnect(struct wl_display*x){(void)x;}
struct wl_registry *wl_display_get_registry(struct wl_display*x){(void)x;return 0;} int wl_registry_add_listener(struct wl_registry*a,const struct wl_registry_listener*b,void*c){(void)a;(void)b;(void)c;return 0;}
void *wl_registry_bind(struct wl_registry*a,uint32_t b,const struct wl_interface*c,uint32_t d){(void)a;(void)b;(void)c;(void)d;return 0;} void wl_registry_destroy(struct wl_registry*x){(void)x;}
int wl_display_roundtrip(struct wl_display*x){(void)x;return -1;} int wl_display_dispatch_pending(struct wl_display*x){(void)x;return -1;} int wl_display_flush(struct wl_display*x){(void)x;return -1;} int wl_display_get_fd(struct wl_display*x){(void)x;return -1;} int wl_display_dispatch(struct wl_display*x){(void)x;return -1;} void wl_seat_destroy(struct wl_seat*x){(void)x;}
int ext_data_control_offer_v1_add_listener(struct ext_data_control_offer_v1*a,const struct ext_data_control_offer_v1_listener*b,void*c){(void)a;(void)b;(void)c;return 0;} void ext_data_control_offer_v1_destroy(struct ext_data_control_offer_v1*a){(void)a;} void ext_data_control_offer_v1_receive(struct ext_data_control_offer_v1*a,const char*b,int32_t c){(void)a;(void)b;(void)c;}
struct ext_data_control_device_v1 *ext_data_control_manager_v1_get_data_device(struct ext_data_control_manager_v1*a,struct wl_seat*b){(void)a;(void)b;return 0;} struct ext_data_control_source_v1 *ext_data_control_manager_v1_create_data_source(struct ext_data_control_manager_v1*a){(void)a;return 0;} void ext_data_control_manager_v1_destroy(struct ext_data_control_manager_v1*a){(void)a;}
int ext_data_control_device_v1_add_listener(struct ext_data_control_device_v1*a,const struct ext_data_control_device_v1_listener*b,void*c){(void)a;(void)b;(void)c;return 0;} void ext_data_control_device_v1_set_selection(struct ext_data_control_device_v1*a,struct ext_data_control_source_v1*b){(void)a;(void)b;} void ext_data_control_device_v1_set_primary_selection(struct ext_data_control_device_v1*a,struct ext_data_control_source_v1*b){(void)a;(void)b;} void ext_data_control_device_v1_destroy(struct ext_data_control_device_v1*a){(void)a;}
int ext_data_control_source_v1_add_listener(struct ext_data_control_source_v1*a,const struct ext_data_control_source_v1_listener*b,void*c){(void)a;(void)b;(void)c;return 0;} void ext_data_control_source_v1_offer(struct ext_data_control_source_v1*a,const char*b){(void)a;(void)b;} void ext_data_control_source_v1_destroy(struct ext_data_control_source_v1*a){(void)a;}
int zcosmic_toplevel_info_v1_add_listener(struct zcosmic_toplevel_info_v1*a,const struct zcosmic_toplevel_info_v1_listener*b,void*c){(void)a;(void)b;(void)c;return 0;} int zcosmic_toplevel_handle_v1_add_listener(struct zcosmic_toplevel_handle_v1*a,const struct zcosmic_toplevel_handle_v1_listener*b,void*c){(void)a;(void)b;(void)c;return 0;} void zcosmic_toplevel_handle_v1_destroy(struct zcosmic_toplevel_handle_v1*a){(void)a;} void zcosmic_toplevel_info_v1_stop(struct zcosmic_toplevel_info_v1*a){(void)a;}
int wl_display_prepare_read(struct wl_display*x){(void)x;return 0;}
void wl_display_cancel_read(struct wl_display*x){(void)x;}
int wl_display_read_events(struct wl_display*x){(void)x;return 0;}
static void *stub_offer_ud;
void ext_data_control_offer_v1_set_user_data(struct ext_data_control_offer_v1*a,void*b){(void)a;stub_offer_ud=b;}
void *ext_data_control_offer_v1_get_user_data(struct ext_data_control_offer_v1*a){(void)a;return stub_offer_ud;}
