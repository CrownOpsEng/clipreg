/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 CrownOpsEng */
#define _GNU_SOURCE
#include <wayland-client.h>
#include "ext-data-control-v1-client-protocol.h"
#include "cosmic-toplevel-info-unstable-v1-client-protocol.h"

#include <ctype.h>
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <limits.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define CLIPREG_VERSION "0.2.0-dev.2"
#define MAGIC "CLPRG002"
#define MAGIC_LEN 8
#define OWNER_MIME "application/x-crownops-clipreg-owner"
#define COPYQ_SECRET "application/x-copyq-secret"
#define KDE_SECRET "x-kde-passwordmanagerhint"
#define MAX_REGISTER_NAME 32
#define MAX_APP_ID 256
#define MAX_CHORD 96
#define MAX_REPLY 65536
#define MAX_MIMES 256
#define MAX_MIME_NAME 4096
#define RECOVERY_MAX_AGE_MS 10000ULL
#define RECOVERY_MAGIC "CLRCV001"

struct config {
    int hotkey_release_ms;
    int safe_probe_timeout_ms;
    int copy_timeout_ms;
    int paste_timeout_ms;
    int transfer_timeout_ms;
    int request_quiet_ms;
    size_t max_payload_bytes;
    bool suppress_copyq;
    bool stage_primary_for_paste;
    char safe_copy_chord[MAX_CHORD];
    char safe_paste_chord[MAX_CHORD];
};

struct daemon_state;

struct blob {
    char *mime;
    uint8_t *data;
    size_t len;
};

struct item {
    struct blob *blobs;
    size_t count;
    uint64_t created_ms;
    uint32_t source_kind;
    char app_id[MAX_APP_ID];
};

enum sel_kind { SEL_CLIPBOARD = 0, SEL_PRIMARY = 1 };
enum recovery_kind { RECOVERY_NONE = 0, RECOVERY_GRAB = 1, RECOVERY_PASTE = 2 };

struct recovery_state {
    enum recovery_kind kind;
    uint64_t started_realtime_ms;
    bool clipboard_present;
    bool primary_present;
    bool primary_supported;
    bool copyq_was_monitoring;
};

struct offer_state {
    struct ext_data_control_offer_v1 *proxy;
    char **mimes;
    size_t mime_count;
    bool assigned;
    struct offer_state *next;
};

struct source_state {
    struct daemon_state *daemon;
    struct ext_data_control_source_v1 *proxy;
    struct item item;
    enum sel_kind kind;
    bool add_owner_marker;
    uint64_t send_count;
    uint64_t send_fail_count;
    bool cancelled;
    struct source_state *next;
};

struct top_state {
    struct zcosmic_toplevel_handle_v1 *proxy;
    char app_id[MAX_APP_ID];
    bool activated;
    struct top_state *next;
};

struct app_profile {
    char pattern[256];
    char copy_chord[MAX_CHORD];
    char paste_chord[MAX_CHORD];
    char grab_strategy[32];
    struct app_profile *next;
};

struct daemon_state {
    struct config cfg;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct ext_data_control_manager_v1 *dc_manager;
    struct ext_data_control_device_v1 *dc_device;
    struct zcosmic_toplevel_info_v1 *tl_info;

    struct offer_state *offers;
    struct offer_state *clipboard_offer;
    struct offer_state *primary_offer;
    uint64_t clipboard_generation;
    uint64_t primary_generation;

    struct source_state *sources;
    struct source_state *active_clipboard_source;
    struct source_state *active_primary_source;
    struct top_state *tops;
    char active_app[MAX_APP_ID];
    uint64_t active_app_generation;
    char primary_affinity_app[MAX_APP_ID];
    uint64_t primary_affinity_focus_generation;
    bool primary_affinity_valid;
    bool primary_supported;

    struct app_profile *profiles;
    int uinput_fd;
    int listen_fd;
    char socket_path[PATH_MAX];
    char runtime_dir[PATH_MAX];
    char data_dir[PATH_MAX];
    char config_dir[PATH_MAX];
    bool running;
    bool restart_requested;
    bool in_transaction;
    bool copyq_was_monitoring;
};

static struct daemon_state *G;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static uint64_t deadline_after_ms(int ms) {
    return now_ms() + (uint64_t)(ms > 0 ? ms : 0);
}

static uint64_t realtime_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

static void logmsg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "clipregd: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int path_suffix(char *out, size_t cap, const char *base, const char *suffix) {
    size_t a = strlen(base), b = strlen(suffix);
    if (a > cap || b > cap || a + b + 1 > cap) return -ENAMETOOLONG;
    memcpy(out, base, a);
    memcpy(out + a, suffix, b + 1);
    return 0;
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    if (strlen(path) >= sizeof(tmp)) return -ENAMETOOLONG;
    strcpy(tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -errno;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -errno;
    chmod(tmp, mode);
    return 0;
}

static void item_init(struct item *it) { memset(it, 0, sizeof(*it)); }

static void item_free(struct item *it) {
    if (!it) return;
    for (size_t i = 0; i < it->count; i++) {
        free(it->blobs[i].mime);
        free(it->blobs[i].data);
    }
    free(it->blobs);
    item_init(it);
}

static int item_add(struct item *it, const char *mime, const void *data, size_t len) {
    if (!mime || !*mime || strlen(mime) > MAX_MIME_NAME) return -EINVAL;
    if (it->count >= MAX_MIMES) return -E2BIG;
    struct blob *nb = realloc(it->blobs, (it->count + 1) * sizeof(*nb));
    if (!nb) return -ENOMEM;
    it->blobs = nb;
    struct blob *b = &it->blobs[it->count];
    memset(b, 0, sizeof(*b));
    b->mime = strdup(mime);
    if (!b->mime) return -ENOMEM;
    if (len) {
        b->data = malloc(len);
        if (!b->data) { free(b->mime); b->mime = NULL; return -ENOMEM; }
        memcpy(b->data, data, len);
    }
    b->len = len;
    it->count++;
    return 0;
}

static const struct blob *item_find(const struct item *it, const char *mime) {
    for (size_t i = 0; i < it->count; i++)
        if (!strcasecmp(it->blobs[i].mime, mime)) return &it->blobs[i];
    return NULL;
}

static bool item_content_equal(const struct item *a, const struct item *b) {
    if (a->count != b->count) return false;
    for (size_t i = 0; i < a->count; i++) {
        const struct blob *x = &a->blobs[i];
        const struct blob *y = item_find(b, x->mime);
        if (!y || x->len != y->len) return false;
        if (x->len && memcmp(x->data, y->data, x->len) != 0) return false;
    }
    return true;
}

static int item_clone(const struct item *src, struct item *dst) {
    item_init(dst);
    dst->created_ms = src->created_ms;
    dst->source_kind = src->source_kind;
    snprintf(dst->app_id, sizeof(dst->app_id), "%s", src->app_id);
    for (size_t i = 0; i < src->count; i++) {
        int r = item_add(dst, src->blobs[i].mime, src->blobs[i].data, src->blobs[i].len);
        if (r < 0) { item_free(dst); return r; }
    }
    return 0;
}

static bool is_secret_item(const struct item *it) {
    if (item_find(it, COPYQ_SECRET)) return true;
    for (size_t i = 0; i < it->count; i++) {
        const struct blob *b = &it->blobs[i];
        if (!strcasecmp(b->mime, KDE_SECRET) || !strcasecmp(b->mime, "application/x-kde-passwordmanagerhint")) {
            char tmp[32];
            size_t n = b->len < sizeof(tmp)-1 ? b->len : sizeof(tmp)-1;
            if (n != 0) {
                if (b->data == NULL) return true;
                memcpy(tmp, b->data, n);
            }
            tmp[n] = 0;
            for (size_t j = 0; j < n; j++) tmp[j] = (char)tolower((unsigned char)tmp[j]);
            if (strstr(tmp, "secret")) return true;
        }
    }
    return false;
}

static bool register_metadata_mime(const char *mime) {
    if (!strcmp(mime, OWNER_MIME)) return true;
    if (!strncasecmp(mime, "application/x-copyq-", 20)) return true;
    return false;
}

static int item_content_copy(const struct item *src, struct item *dst) {
    item_init(dst);
    dst->created_ms = realtime_ms();
    dst->source_kind = src->source_kind;
    snprintf(dst->app_id, sizeof(dst->app_id), "%s", src->app_id);
    for (size_t i = 0; i < src->count; i++) {
        if (register_metadata_mime(src->blobs[i].mime)) continue;
        int r = item_add(dst, src->blobs[i].mime, src->blobs[i].data, src->blobs[i].len);
        if (r < 0) { item_free(dst); return r; }
    }
    return dst->count ? 0 : -ENODATA;
}

static int valid_register_name(const char *s) {
    if (!s || !*s || strlen(s) > MAX_REGISTER_NAME) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (!(isalnum(*p) || *p == '.' || *p == '_' || *p == '-')) return 0;
    return 1;
}

static void default_config(struct config *c) {
    memset(c, 0, sizeof(*c));
    c->hotkey_release_ms = 180;
    c->safe_probe_timeout_ms = 350;
    c->copy_timeout_ms = 1800;
    c->paste_timeout_ms = 1800;
    c->transfer_timeout_ms = 3000;
    c->request_quiet_ms = 120;
    c->max_payload_bytes = 256ULL * 1024ULL * 1024ULL;
    c->suppress_copyq = true;
    c->stage_primary_for_paste = true;
    snprintf(c->safe_copy_chord, sizeof(c->safe_copy_chord), "CTRL+INSERT");
    snprintf(c->safe_paste_chord, sizeof(c->safe_paste_chord), "SHIFT+INSERT");
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static bool parse_bool(const char *s, bool def) {
    if (!strcasecmp(s,"true") || !strcmp(s,"1") || !strcasecmp(s,"yes")) return true;
    if (!strcasecmp(s,"false") || !strcmp(s,"0") || !strcasecmp(s,"no")) return false;
    return def;
}

static bool parse_int_value(const char *s, int *out) {
    if (!s || !*s || !out) return false;
    errno = 0;
    char *end = NULL;
    long value = strtol(s, &end, 10);
    if (errno || end == s || *trim(end) != '\0' || value < INT_MIN || value > INT_MAX) return false;
    *out = (int)value;
    return true;
}

static void load_config_file(struct daemon_state *d) {
    default_config(&d->cfg);
    char path[PATH_MAX];
    if (path_suffix(path, sizeof(path), d->config_dir, "/clipreg.conf") < 0) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        char *eq = strchr(p, '='); if (!eq) continue;
        *eq++ = 0; char *k = trim(p), *v = trim(eq);
        if (!strcmp(k,"hotkey_release_ms")) { int n; if (parse_int_value(v, &n)) d->cfg.hotkey_release_ms = n; }
        else if (!strcmp(k,"safe_probe_timeout_ms")) { int n; if (parse_int_value(v, &n)) d->cfg.safe_probe_timeout_ms = n; }
        else if (!strcmp(k,"copy_timeout_ms")) { int n; if (parse_int_value(v, &n)) d->cfg.copy_timeout_ms = n; }
        else if (!strcmp(k,"paste_timeout_ms")) { int n; if (parse_int_value(v, &n)) d->cfg.paste_timeout_ms = n; }
        else if (!strcmp(k,"transfer_timeout_ms")) { int n; if (parse_int_value(v, &n)) d->cfg.transfer_timeout_ms = n; }
        else if (!strcmp(k,"request_quiet_ms")) { int n; if (parse_int_value(v, &n)) d->cfg.request_quiet_ms = n; }
        else if (!strcmp(k,"max_payload_mib")) {
            char *end = NULL; unsigned long long mib = strtoull(v, &end, 10);
            if (end && *trim(end) == 0 && mib >= 1ULL && mib <= 2048ULL)
                d->cfg.max_payload_bytes = (size_t)mib * 1024ULL * 1024ULL;
        }
        else if (!strcmp(k,"suppress_copyq")) d->cfg.suppress_copyq = parse_bool(v,d->cfg.suppress_copyq);
        else if (!strcmp(k,"stage_primary_for_paste")) d->cfg.stage_primary_for_paste = parse_bool(v,d->cfg.stage_primary_for_paste);
        else if (!strcmp(k,"safe_copy_chord")) snprintf(d->cfg.safe_copy_chord,sizeof(d->cfg.safe_copy_chord),"%s",v);
        else if (!strcmp(k,"safe_paste_chord")) snprintf(d->cfg.safe_paste_chord,sizeof(d->cfg.safe_paste_chord),"%s",v);
    }
    fclose(f);
    struct config def; default_config(&def);
#define CLAMP_CFG(field, lo, hi) do { if (d->cfg.field < (lo) || d->cfg.field > (hi)) { \
        logmsg("invalid %s in clipreg.conf; using default", #field); d->cfg.field = def.field; } } while (0)
    CLAMP_CFG(hotkey_release_ms, 0, 2000);
    CLAMP_CFG(safe_probe_timeout_ms, 50, 5000);
    CLAMP_CFG(copy_timeout_ms, 100, 10000);
    CLAMP_CFG(paste_timeout_ms, 100, 10000);
    CLAMP_CFG(transfer_timeout_ms, 100, 30000);
    CLAMP_CFG(request_quiet_ms, 0, 2000);
#undef CLAMP_CFG
}

static void free_profiles(struct app_profile *p) {
    while (p) { struct app_profile *n=p->next; free(p); p=n; }
}

static void add_builtin_profile(struct daemon_state *d, const char *pat, const char *copy, const char *paste, const char *grab) {
    struct app_profile *p = calloc(1,sizeof(*p)); if (!p) return;
    snprintf(p->pattern,sizeof(p->pattern),"%s",pat);
    snprintf(p->copy_chord,sizeof(p->copy_chord),"%s",copy);
    snprintf(p->paste_chord,sizeof(p->paste_chord),"%s",paste);
    snprintf(p->grab_strategy,sizeof(p->grab_strategy),"%s",grab);
    p->next=d->profiles; d->profiles=p;
}

static void load_profiles(struct daemon_state *d) {
    free_profiles(d->profiles); d->profiles=NULL;
    /* Added in reverse precedence; user file is prepended below. */
    /* Unknown applications fail closed after the safe Ctrl+Insert/Shift+Insert probes.
     * We never inject Ctrl+C into an unclassified app because it could be a terminal. */
    add_builtin_profile(d, "*", "-", "-", "copy");
    /* Common GUI applications where Ctrl+C/Ctrl+V are explicitly safe fallbacks. */
    add_builtin_profile(d, "*firefox*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*chromium*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*chrome*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*brave*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*libreoffice*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*cosmicfiles*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*CosmicFiles*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*nautilus*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*thunar*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*dolphin*", "CTRL+C", "CTRL+V", "copy");
    add_builtin_profile(d, "*xterm*", "-", "SHIFT+INSERT", "primary-only");
    add_builtin_profile(d, "*org.gnome.Terminal*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*terminal*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*foot*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*wezterm*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*kitty*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*alacritty*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*konsole*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*gnome-terminal*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*cosmicterm*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    add_builtin_profile(d, "*CosmicTerm*", "CTRL+SHIFT+C", "CTRL+SHIFT+V", "primary-first");
    /* Apps with embedded terminals: never fall back to Ctrl+C for Copy. */
    add_builtin_profile(d, "*zed*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*webstorm*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*goland*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*clion*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*pycharm*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*idea*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*jetbrains*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*codium*", "CTRL+INSERT", "SHIFT+INSERT", "copy");
    add_builtin_profile(d, "*code*", "CTRL+INSERT", "SHIFT+INSERT", "copy");

    char path[PATH_MAX]; if (path_suffix(path, sizeof(path), d->config_dir, "/app-profiles.conf") < 0) return;
    FILE *f=fopen(path,"r"); if(!f) return;
    char line[1024];
    struct app_profile *head=NULL,*tail=NULL;
    while(fgets(line,sizeof(line),f)) {
        char *s=trim(line); if(!*s||*s=='#') continue;
        char *fields[4]={0}; int nf=0; char *save=NULL;
        for(char *tok=strtok_r(s,"|",&save); tok && nf<4; tok=strtok_r(NULL,"|",&save)) fields[nf++]=trim(tok);
        if(nf<4) continue;
        if (strcmp(fields[3], "copy") && strcmp(fields[3], "primary-first") && strcmp(fields[3], "primary-only")) {
            logmsg("ignoring app profile with invalid grab strategy: %s", fields[3]);
            continue;
        }
        struct app_profile *p=calloc(1,sizeof(*p)); if(!p) break;
        snprintf(p->pattern,sizeof(p->pattern),"%s",fields[0]);
        snprintf(p->copy_chord,sizeof(p->copy_chord),"%s",fields[1]);
        snprintf(p->paste_chord,sizeof(p->paste_chord),"%s",fields[2]);
        snprintf(p->grab_strategy,sizeof(p->grab_strategy),"%s",fields[3]);
        if(!head) head=p; else tail->next=p; tail=p;
    }
    fclose(f);
    if (tail) tail->next=d->profiles;
    if (head) d->profiles=head;
}

static const struct app_profile *profile_for(struct daemon_state *d, const char *app) {
    for (struct app_profile *p=d->profiles;p;p=p->next)
        if (fnmatch(p->pattern, app && *app ? app : "unknown", FNM_CASEFOLD)==0) return p;
    return NULL;
}

static int flush_wayland(struct daemon_state *d, int timeout_ms) {
    uint64_t deadline = deadline_after_ms(timeout_ms);
    for (;;) {
        if (wl_display_flush(d->display) >= 0) return 0;
        if (errno != EAGAIN) return -errno;
        uint64_t now = now_ms();
        if (now >= deadline) return -ETIMEDOUT;
        struct pollfd p = {.fd = wl_display_get_fd(d->display), .events = POLLOUT};
        int r = poll(&p, 1, (int)(deadline - now));
        if (r < 0) { if (errno == EINTR) continue; return -errno; }
        if (r == 0) return -ETIMEDOUT;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) return -EIO;
    }
}

static int dispatch_once(struct daemon_state *d, int timeout_ms) {
    int fd = wl_display_get_fd(d->display);
    while (wl_display_prepare_read(d->display) != 0) {
        if (wl_display_dispatch_pending(d->display) < 0) return -EIO;
    }
    int fr = wl_display_flush(d->display);
    if (fr < 0 && errno != EAGAIN) { wl_display_cancel_read(d->display); return -errno; }
    struct pollfd p = {.fd = fd, .events = POLLIN};
    int r = poll(&p, 1, timeout_ms);
    if (r < 0) { wl_display_cancel_read(d->display); return errno == EINTR ? 0 : -errno; }
    if (r == 0) { wl_display_cancel_read(d->display); return 0; }
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { wl_display_cancel_read(d->display); return -EIO; }
    if (!(p.revents & POLLIN)) { wl_display_cancel_read(d->display); return 0; }
    if (wl_display_read_events(d->display) < 0) return -EIO;
    if (wl_display_dispatch_pending(d->display) < 0) return -EIO;
    return 1;
}

static struct offer_state *offer_from_proxy(struct ext_data_control_offer_v1 *p) {
    return p ? ext_data_control_offer_v1_get_user_data(p) : NULL;
}

static void offer_destroy(struct offer_state *o) {
    if(!o) return;
    if(o->proxy) ext_data_control_offer_v1_destroy(o->proxy);
    for(size_t i=0;i<o->mime_count;i++) free(o->mimes[i]);
    free(o->mimes); free(o);
}

static void offer_event_mime(void *data, struct ext_data_control_offer_v1 *proxy, const char *mime) {
    (void)proxy; struct offer_state *o=data;
    if (!mime || !*mime || strlen(mime) > MAX_MIME_NAME) return;
    for (size_t i = 0; i < o->mime_count; i++)
        if (!strcmp(o->mimes[i], mime)) return;
    if(o->mime_count>=MAX_MIMES) return;
    char **n=realloc(o->mimes,(o->mime_count+1)*sizeof(char*)); if(!n) return;
    o->mimes=n; o->mimes[o->mime_count]=strdup(mime); if(o->mimes[o->mime_count]) o->mime_count++;
}
static const struct ext_data_control_offer_v1_listener offer_listener={ .offer=offer_event_mime };

static void device_data_offer(void *data, struct ext_data_control_device_v1 *dev, struct ext_data_control_offer_v1 *proxy) {
    (void)dev; struct daemon_state *d=data;
    struct offer_state *o=calloc(1,sizeof(*o)); if(!o) return;
    o->proxy=proxy; o->next=d->offers; d->offers=o;
    ext_data_control_offer_v1_set_user_data(proxy,o);
    ext_data_control_offer_v1_add_listener(proxy,&offer_listener,o);
}

static void unlink_offer(struct daemon_state *d, struct offer_state *o) {
    struct offer_state **pp=&d->offers;
    while(*pp){ if(*pp==o){*pp=o->next;return;} pp=&(*pp)->next; }
}

static void set_offer_slot(struct daemon_state *d, struct offer_state **slot, struct ext_data_control_offer_v1 *proxy, bool primary) {
    struct offer_state *old=*slot;
    struct offer_state *nw=offer_from_proxy(proxy);
    if(nw) nw->assigned=true;
    *slot=nw;
    if(old && old!=nw){ unlink_offer(d,old); offer_destroy(old); }
    if(primary){
        d->primary_generation++;
        if(d->active_app[0]){
            snprintf(d->primary_affinity_app,sizeof(d->primary_affinity_app),"%s",d->active_app);
            d->primary_affinity_focus_generation=d->active_app_generation;
            d->primary_affinity_valid=true;
        } else d->primary_affinity_valid=false;
    }else d->clipboard_generation++;
}
static void device_selection(void *data, struct ext_data_control_device_v1 *dev, struct ext_data_control_offer_v1 *proxy){(void)dev;set_offer_slot(data,&((struct daemon_state*)data)->clipboard_offer,proxy,false);}
static void device_primary(void *data, struct ext_data_control_device_v1 *dev, struct ext_data_control_offer_v1 *proxy){(void)dev;((struct daemon_state*)data)->primary_supported=true;set_offer_slot(data,&((struct daemon_state*)data)->primary_offer,proxy,true);}
static void device_finished(void *data, struct ext_data_control_device_v1 *dev){(void)dev;struct daemon_state*d=data;if(d){logmsg("Wayland data-control device finished; restarting daemon");d->restart_requested=true;d->running=false;}}
static const struct ext_data_control_device_v1_listener device_listener={.data_offer=device_data_offer,.selection=device_selection,.finished=device_finished,.primary_selection=device_primary};

static int write_all(int fd,const void *buf,size_t len){const uint8_t*p=buf;while(len){ssize_t n=write(fd,p,len);if(n<0){if(errno==EINTR)continue;return -errno;}if(n==0)return -EIO;size_t written=(size_t)n;p+=written;len-=written;}return 0;}

static int write_all_timeout(int fd, const void *buf, size_t len, int timeout_ms) {
    const uint8_t *p = buf; uint64_t deadline = deadline_after_ms(timeout_ms);
    int flags = fcntl(fd, F_GETFL, 0); if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n > 0) { p += (size_t)n; len -= (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            uint64_t now = now_ms(); if (now >= deadline) return -ETIMEDOUT;
            struct pollfd po = { .fd = fd, .events = POLLOUT };
            int pr = poll(&po, 1, (int)(deadline - now));
            if (pr < 0) { if (errno == EINTR) continue; return -errno; }
            if (pr == 0) return -ETIMEDOUT;
            if (po.revents & (POLLERR | POLLHUP | POLLNVAL)) return -EPIPE;
            continue;
        }
        return n == 0 ? -EPIPE : -errno;
    }
    return 0;
}

static const struct blob *source_blob(const struct source_state *s,const char *mime){return item_find(&s->item,mime);}
static void source_send(void *data, struct ext_data_control_source_v1 *proxy, const char *mime, int32_t fd){
    (void)proxy; struct source_state *s=data;
    bool internal = strcmp(mime, OWNER_MIME) == 0;
    if (!internal) s->send_count++;
    const struct blob *b=source_blob(s,mime);
    int r = 0;
    if (b) r = write_all_timeout(fd, b->data, b->len, s->daemon->cfg.transfer_timeout_ms);
    if (r < 0 && !internal) s->send_fail_count++;
    close(fd);
}
static void source_cancel(void *data, struct ext_data_control_source_v1 *proxy){
    struct source_state *s = data;
    s->cancelled = true;
    if (s->daemon) {
        struct source_state **active = s->kind == SEL_CLIPBOARD
            ? &s->daemon->active_clipboard_source : &s->daemon->active_primary_source;
        if (*active == s) *active = NULL;
    }
    if (proxy) { ext_data_control_source_v1_destroy(proxy); s->proxy = NULL; }
}
static const struct ext_data_control_source_v1_listener source_listener={.send=source_send,.cancelled=source_cancel};

static struct source_state *publish_item(struct daemon_state *d, enum sel_kind kind, const struct item *it, bool marker) {
    struct source_state*s=calloc(1,sizeof(*s)); if(!s)return NULL;
    if(item_clone(it,&s->item)<0){free(s);return NULL;}
    s->daemon=d; s->kind=kind;s->add_owner_marker=marker;
    s->proxy=ext_data_control_manager_v1_create_data_source(d->dc_manager);
    if(!s->proxy){item_free(&s->item);free(s);return NULL;}
    ext_data_control_source_v1_add_listener(s->proxy,&source_listener,s);
    for(size_t i=0;i<it->count;i++) ext_data_control_source_v1_offer(s->proxy,it->blobs[i].mime);
    if(marker) ext_data_control_source_v1_offer(s->proxy,OWNER_MIME);
    if(kind==SEL_CLIPBOARD) {
        ext_data_control_device_v1_set_selection(d->dc_device,s->proxy);
        d->active_clipboard_source = s;
    } else {
        ext_data_control_device_v1_set_primary_selection(d->dc_device,s->proxy);
        d->active_primary_source = s;
    }
    (void)wl_display_flush(d->display);
    s->next=d->sources;d->sources=s;
    return s;
}

static void clear_selection(struct daemon_state*d,enum sel_kind kind){if(kind==SEL_CLIPBOARD)ext_data_control_device_v1_set_selection(d->dc_device,NULL);else ext_data_control_device_v1_set_primary_selection(d->dc_device,NULL);(void)wl_display_flush(d->display);}

static int offer_has_mime(const struct offer_state*o,const char*m){if(!o)return 0;for(size_t i=0;i<o->mime_count;i++)if(!strcmp(o->mimes[i],m))return 1;return 0;}

static int read_pipe_capped(int fd,uint8_t **out,size_t *outlen,size_t cap,int timeout_ms){
    uint8_t*buf=NULL;size_t len=0,alloc=0;uint64_t deadline=deadline_after_ms(timeout_ms);
    for(;;){
        int remain=(int)(deadline>now_ms()?deadline-now_ms():0); if(remain<=0){free(buf);return -ETIMEDOUT;}
        struct pollfd p={.fd=fd,.events=POLLIN|POLLHUP};int pr=poll(&p,1,remain);
        if(pr<0){if(errno==EINTR)continue;free(buf);return -errno;} if(pr==0){free(buf);return -ETIMEDOUT;}
        uint8_t tmp[65536];ssize_t n=read(fd,tmp,sizeof(tmp));
        if(n<0){if(errno==EINTR||errno==EAGAIN)continue;free(buf);return -errno;}
        if(n==0)break;
        if(len+(size_t)n>cap){free(buf);return -EFBIG;}
        if(len+(size_t)n>alloc){size_t na=alloc?alloc*2:65536;while(na<len+(size_t)n)na*=2;if(na>cap)na=cap;uint8_t*nb=realloc(buf,na);if(!nb){free(buf);return -ENOMEM;}buf=nb;alloc=na;}
        memcpy(buf+len,tmp,(size_t)n);len+=(size_t)n;
    }
    *out=buf;*outlen=len;return 0;
}

static int snapshot_offer(struct daemon_state*d,struct offer_state*o,struct item*out){
    item_init(out); if(!o) return 0;
    uint64_t total=0;
    for(size_t i=0;i<o->mime_count;i++){
        int pfd[2]; if(pipe2(pfd,O_CLOEXEC)<0){item_free(out);return -errno;}
        ext_data_control_offer_v1_receive(o->proxy,o->mimes[i],pfd[1]);
        int flush_r = flush_wayland(d, d->cfg.transfer_timeout_ms);
        close(pfd[1]);
        if (flush_r < 0) { close(pfd[0]); item_free(out); return flush_r; }
        uint8_t*data=NULL;size_t len=0;size_t cap=d->cfg.max_payload_bytes>total?d->cfg.max_payload_bytes-total:0;
        int r=read_pipe_capped(pfd[0],&data,&len,cap,d->cfg.transfer_timeout_ms);close(pfd[0]);
        if(r<0){free(data);item_free(out);return r;} total+=len;
        r=item_add(out,o->mimes[i],data,len);free(data);if(r<0){item_free(out);return r;}
    }
    return 0;
}

static int snapshot_current(struct daemon_state *d, enum sel_kind kind, struct item *out) {
    /* Never round-trip a selection back through Wayland when ClipReg itself is
     * the source: source_send() is dispatched by this same event loop, so a
     * synchronous receive would deadlock waiting for our own callback. */
    struct source_state *owned = kind == SEL_CLIPBOARD
        ? d->active_clipboard_source : d->active_primary_source;
    if (owned && !owned->cancelled) return item_clone(&owned->item, out);
    return snapshot_offer(d, kind == SEL_CLIPBOARD ? d->clipboard_offer : d->primary_offer, out);
}

static int drain_wayland(struct daemon_state *d);

static int stable_snapshot_current(struct daemon_state *d, enum sel_kind kind, struct item *out,
                                   bool *present, uint64_t *generation) {
    int r = drain_wayland(d); /* prototype below; declared before use by compiler via declaration */
    if (r < 0) return r;
    uint64_t g = kind == SEL_CLIPBOARD ? d->clipboard_generation : d->primary_generation;
    struct offer_state *o = kind == SEL_CLIPBOARD ? d->clipboard_offer : d->primary_offer;
    bool p = o != NULL;
    r = snapshot_offer(d, o, out); if (r < 0) return r;
    r = drain_wayland(d); if (r < 0) { item_free(out); return r; }
    uint64_t after = kind == SEL_CLIPBOARD ? d->clipboard_generation : d->primary_generation;
    if (after != g) { item_free(out); return -EAGAIN; }
    if (present) *present = p;
    if (generation) *generation = g;
    return 0;
}

static int wait_generation(struct daemon_state*d,enum sel_kind kind,uint64_t old,int timeout_ms){uint64_t deadline=deadline_after_ms(timeout_ms);while(now_ms()<deadline){uint64_t g=kind==SEL_CLIPBOARD?d->clipboard_generation:d->primary_generation;if(g>old)return 0;int r=dispatch_once(d,(int)(deadline-now_ms()));if(r<0)return r;}return -ETIMEDOUT;}

static int wait_marker_offer(struct daemon_state*d,enum sel_kind kind,uint64_t old,int timeout_ms){int r=wait_generation(d,kind,old,timeout_ms);if(r<0)return r;struct offer_state*o=kind==SEL_CLIPBOARD?d->clipboard_offer:d->primary_offer;return offer_has_mime(o,OWNER_MIME)?0:-EAGAIN;}

static int wait_external_generation(struct daemon_state *d, enum sel_kind kind, uint64_t old, int timeout_ms) {
    uint64_t deadline = deadline_after_ms(timeout_ms);
    while (now_ms() < deadline) {
        uint64_t g = kind == SEL_CLIPBOARD ? d->clipboard_generation : d->primary_generation;
        struct offer_state *o = kind == SEL_CLIPBOARD ? d->clipboard_offer : d->primary_offer;
        if (g > old && !offer_has_mime(o, OWNER_MIME)) return 0;
        int remain = (int)(deadline - now_ms());
        int r = dispatch_once(d, remain > 0 ? remain : 0);
        if (r < 0) return r;
    }
    return -ETIMEDOUT;
}


static int wait_source_quiet(struct daemon_state*d,struct source_state*s,int quiet_ms,int max_ms){uint64_t deadline=deadline_after_ms(max_ms);uint64_t quiet=deadline_after_ms(quiet_ms);uint64_t count=s->send_count;while(now_ms()<deadline){int to=(int)((quiet<deadline?quiet:deadline)-now_ms());if(to<0)to=0;int r=dispatch_once(d,to);if(r<0)return r;if(s->send_count!=count){count=s->send_count;quiet=deadline_after_ms(quiet_ms);}if(now_ms()>=quiet)return 0;}return 0;}

static int drain_wayland(struct daemon_state *d) {
    for (int i = 0; i < 64; i++) {
        int r = dispatch_once(d, 0);
        if (r < 0) return r;
        if (r == 0) return 0;
    }
    return 0;
}

static bool selection_has_owner_marker(struct daemon_state *d, enum sel_kind kind) {
    struct offer_state *o = kind == SEL_CLIPBOARD ? d->clipboard_offer : d->primary_offer;
    return offer_has_mime(o, OWNER_MIME) != 0;
}

static void refresh_active_app(struct daemon_state*d);
static void top_closed(void *data, struct zcosmic_toplevel_handle_v1 *p){struct top_state*t=data;t->activated=false;zcosmic_toplevel_handle_v1_destroy(p);t->proxy=NULL;if(G)refresh_active_app(G);}
static void top_done(void*data,struct zcosmic_toplevel_handle_v1*p){(void)p;(void)data;if(G)refresh_active_app(G);}
static void top_title(void*data,struct zcosmic_toplevel_handle_v1*p,const char*s){(void)data;(void)p;(void)s;}
static void top_app(void*data,struct zcosmic_toplevel_handle_v1*p,const char*s){(void)p;struct top_state*t=data;snprintf(t->app_id,sizeof(t->app_id),"%s",s?s:"");if(G)refresh_active_app(G);}
static void top_output_enter(void*d,struct zcosmic_toplevel_handle_v1*p,struct wl_output*o){(void)d;(void)p;(void)o;}
static void top_output_leave(void*d,struct zcosmic_toplevel_handle_v1*p,struct wl_output*o){(void)d;(void)p;(void)o;}
static void top_workspace_enter(void*d,struct zcosmic_toplevel_handle_v1*p,struct zcosmic_workspace_handle_v1*w){(void)d;(void)p;(void)w;}
static void top_workspace_leave(void*d,struct zcosmic_toplevel_handle_v1*p,struct zcosmic_workspace_handle_v1*w){(void)d;(void)p;(void)w;}
static void refresh_active_app(struct daemon_state*d){
    char newapp[MAX_APP_ID]="";for(struct top_state*t=d->tops;t;t=t->next)if(t->activated&&t->app_id[0]){snprintf(newapp,sizeof(newapp),"%s",t->app_id);break;}
    if(strcmp(newapp,d->active_app)){snprintf(d->active_app,sizeof(d->active_app),"%s",newapp);d->active_app_generation++;d->primary_affinity_valid=false;}
}
static void top_state_evt(void*data,struct zcosmic_toplevel_handle_v1*p,struct wl_array*a){(void)p;struct top_state*t=data;bool active=false;uint32_t*v;wl_array_for_each(v,a)if(*v==ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)active=true;t->activated=active;if(G)refresh_active_app(G);}
static const struct zcosmic_toplevel_handle_v1_listener top_listener={.closed=top_closed,.done=top_done,.title=top_title,.app_id=top_app,.output_enter=top_output_enter,.output_leave=top_output_leave,.workspace_enter=top_workspace_enter,.workspace_leave=top_workspace_leave,.state=top_state_evt};
static void info_toplevel(void*data,struct zcosmic_toplevel_info_v1*i,struct zcosmic_toplevel_handle_v1*p){(void)i;struct daemon_state*d=data;struct top_state*t=calloc(1,sizeof(*t));if(!t)return;t->proxy=p;t->next=d->tops;d->tops=t;zcosmic_toplevel_handle_v1_add_listener(p,&top_listener,t);}
static void info_finished(void*d,struct zcosmic_toplevel_info_v1*i){(void)d;(void)i;}
static const struct zcosmic_toplevel_info_v1_listener info_listener={.toplevel=info_toplevel,.finished=info_finished};

static void registry_global(void*data,struct wl_registry*r,uint32_t name,const char*iface,uint32_t ver){struct daemon_state*d=data;
    if(!strcmp(iface,wl_seat_interface.name)&&!d->seat)d->seat=wl_registry_bind(r,name,&wl_seat_interface,ver<1?ver:1);
    else if(!strcmp(iface,ext_data_control_manager_v1_interface.name)&&!d->dc_manager)d->dc_manager=wl_registry_bind(r,name,&ext_data_control_manager_v1_interface,1);
    else if(!strcmp(iface,zcosmic_toplevel_info_v1_interface.name)&&!d->tl_info){d->tl_info=wl_registry_bind(r,name,&zcosmic_toplevel_info_v1_interface,1);zcosmic_toplevel_info_v1_add_listener(d->tl_info,&info_listener,d);}
}
static void registry_remove(void*d,struct wl_registry*r,uint32_t name){(void)d;(void)r;(void)name;}
static const struct wl_registry_listener registry_listener={.global=registry_global,.global_remove=registry_remove};

static int setup_wayland(struct daemon_state*d){
    d->display=wl_display_connect(NULL);if(!d->display)return -ENOTCONN;
    d->registry=wl_display_get_registry(d->display);wl_registry_add_listener(d->registry,&registry_listener,d);
    if(wl_display_roundtrip(d->display)<0)return -EIO;
    if(!d->seat||!d->dc_manager)return -ENOTSUP;
    d->dc_device=ext_data_control_manager_v1_get_data_device(d->dc_manager,d->seat);if(!d->dc_device)return -EIO;
    ext_data_control_device_v1_add_listener(d->dc_device,&device_listener,d);
    if(wl_display_roundtrip(d->display)<0)return -EIO;
    if(wl_display_roundtrip(d->display)<0)return -EIO;
    refresh_active_app(d);return 0;
}

static int setup_uinput(void){
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -errno;
    if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0){int e=-errno;close(fd);return e;}
    if(ioctl(fd,UI_SET_EVBIT,EV_SYN)<0){int e=-errno;close(fd);return e;}
    /* Least privilege: expose only keys the ClipReg chord parser can emit. */
    const int keys[] = {
        KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT, KEY_LEFTMETA,
        KEY_INSERT, KEY_DELETE, KEY_SPACE, KEY_ENTER,
        KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,
        KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z,
        KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,
        KEY_F1,KEY_F2,KEY_F3,KEY_F4,KEY_F5,KEY_F6,KEY_F7,KEY_F8,KEY_F9,KEY_F10,KEY_F11,KEY_F12,
    };
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++)
        if(ioctl(fd,UI_SET_KEYBIT,keys[i])<0){int e=-errno;close(fd);return e;}
    struct uinput_setup us={0};snprintf(us.name,UINPUT_MAX_NAME_SIZE,"CrownOps ClipReg Virtual Keyboard");us.id.bustype=BUS_USB;us.id.vendor=0x1209;us.id.product=0xC17E;us.id.version=2;
    if(ioctl(fd,UI_DEV_SETUP,&us)<0){int e=-errno;close(fd);return e;}
    if(ioctl(fd,UI_DEV_CREATE)<0){int e=-errno;close(fd);return e;}
    sleep_ms(120);return fd;
}

static int emit_event(int fd,uint16_t type,uint16_t code,int32_t value){struct input_event e={0};(void)gettimeofday(&e.time,NULL);e.type=type;e.code=code;e.value=value;return write_all(fd,&e,sizeof(e));}
static int emit_key(int fd,int code,int down){int r=emit_event(fd,EV_KEY,(uint16_t)code,down);if(r<0)return r;return emit_event(fd,EV_SYN,SYN_REPORT,0);}

static int keycode_from_name(const char*s){
    if(!strcasecmp(s,"INSERT"))return KEY_INSERT;
    if(!strcasecmp(s,"DELETE"))return KEY_DELETE;
    if(!strcasecmp(s,"SPACE"))return KEY_SPACE;
    if(!strcasecmp(s,"ENTER")||!strcasecmp(s,"RETURN"))return KEY_ENTER;
    if(strlen(s)==1){
        switch(toupper((unsigned char)s[0])){
            case 'A':return KEY_A; case 'B':return KEY_B; case 'C':return KEY_C;
            case 'D':return KEY_D; case 'E':return KEY_E; case 'F':return KEY_F;
            case 'G':return KEY_G; case 'H':return KEY_H; case 'I':return KEY_I;
            case 'J':return KEY_J; case 'K':return KEY_K; case 'L':return KEY_L;
            case 'M':return KEY_M; case 'N':return KEY_N; case 'O':return KEY_O;
            case 'P':return KEY_P; case 'Q':return KEY_Q; case 'R':return KEY_R;
            case 'S':return KEY_S; case 'T':return KEY_T; case 'U':return KEY_U;
            case 'V':return KEY_V; case 'W':return KEY_W; case 'X':return KEY_X;
            case 'Y':return KEY_Y; case 'Z':return KEY_Z;
            case '0':return KEY_0; case '1':return KEY_1; case '2':return KEY_2;
            case '3':return KEY_3; case '4':return KEY_4; case '5':return KEY_5;
            case '6':return KEY_6; case '7':return KEY_7; case '8':return KEY_8;
            case '9':return KEY_9;
        }
    }
    if((s[0]=='F'||s[0]=='f')&&isdigit((unsigned char)s[1])){
        int n=atoi(s+1); switch(n){
            case 1:return KEY_F1;case 2:return KEY_F2;case 3:return KEY_F3;case 4:return KEY_F4;
            case 5:return KEY_F5;case 6:return KEY_F6;case 7:return KEY_F7;case 8:return KEY_F8;
            case 9:return KEY_F9;case 10:return KEY_F10;case 11:return KEY_F11;case 12:return KEY_F12;
        }
    }
    return -1;
}

static int inject_chord(struct daemon_state *d, const char *spec) {
    if (!spec || !*spec || !strcmp(spec, "-")) return -ENOTSUP;
    if (strlen(spec) >= MAX_CHORD) return -EINVAL;
    char tmp[MAX_CHORD]; snprintf(tmp, sizeof(tmp), "%s", spec);
    int mods[4], nm = 0, key = -1; char *save = NULL;
    for (char *t = strtok_r(tmp, "+", &save); t; t = strtok_r(NULL, "+", &save)) {
        t = trim(t); int code = -1; bool is_mod = true;
        if (!strcasecmp(t, "CTRL")) code = KEY_LEFTCTRL;
        else if (!strcasecmp(t, "SHIFT")) code = KEY_LEFTSHIFT;
        else if (!strcasecmp(t, "ALT")) code = KEY_LEFTALT;
        else if (!strcasecmp(t, "SUPER") || !strcasecmp(t, "META")) code = KEY_LEFTMETA;
        else { is_mod = false; code = keycode_from_name(t); }
        if (code < 0) return -EINVAL;
        if (is_mod) {
            if (nm >= (int)(sizeof(mods) / sizeof(mods[0]))) return -EINVAL;
            for (int i = 0; i < nm; i++) if (mods[i] == code) return -EINVAL;
            mods[nm++] = code;
        } else {
            if (key >= 0) return -EINVAL;
            key = code;
        }
    }
    if (key < 0) return -EINVAL;
    for (int i = 0; i < nm; i++) if (emit_key(d->uinput_fd, mods[i], 1) < 0) return -EIO;
    if (emit_key(d->uinput_fd, key, 1) < 0) return -EIO;
    if (emit_key(d->uinput_fd, key, 0) < 0) return -EIO;
    for (int i = nm - 1; i >= 0; i--) if (emit_key(d->uinput_fd, mods[i], 0) < 0) return -EIO;
    return 0;
}

static void hotkey_release_guard(struct daemon_state*d,uint64_t command_received_ms){uint64_t ready=command_received_ms+(uint64_t)d->cfg.hotkey_release_ms;uint64_t n=now_ms();if(n<ready)sleep_ms((int)(ready-n));}

static int exec_capture_timeout(char *const argv[], char *buf, size_t cap, int timeout_ms) {
    if (!buf || cap == 0) return -EINVAL;
    buf[0] = '\0';
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return -errno;
    pid_t pid = fork();
    if (pid < 0) { int e = -errno; close(p[0]); close(p[1]); return e; }
    if (pid == 0) {
        int flags = fcntl(p[1], F_GETFL, 0);
        if (flags >= 0) (void)fcntl(p[1], F_SETFL, flags & ~O_NONBLOCK);
        (void)dup2(p[1], STDOUT_FILENO);
        (void)dup2(p[1], STDERR_FILENO);
        close(p[0]); close(p[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    uint64_t deadline = deadline_after_ms(timeout_ms);
    size_t used = 0;
    bool eof = false, exited = false;
    int status = 0;
    while (!eof || !exited) {
        for (;;) {
            char tmp[512];
            ssize_t n = read(p[0], tmp, sizeof(tmp));
            if (n > 0) {
                size_t take = (size_t)n;
                if (take > cap - 1U - used) take = cap - 1U - used;
                if (take) { memcpy(buf + used, tmp, take); used += take; buf[used] = '\0'; }
                continue;
            }
            if (n == 0) eof = true;
            else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) eof = true;
            break;
        }
        if (!exited) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) exited = true;
            else if (w < 0 && errno != EINTR) { close(p[0]); return -errno; }
        }
        if (eof && exited) break;
        uint64_t now = now_ms();
        if (now >= deadline) {
            (void)kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            close(p[0]);
            return -ETIMEDOUT;
        }
        struct pollfd po = {.fd = p[0], .events = POLLIN | POLLHUP};
        int wait_ms = (int)(deadline - now);
        if (wait_ms > 50) wait_ms = 50;
        int pr = poll(&po, 1, wait_ms);
        if (pr < 0 && errno != EINTR) {
            (void)kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            close(p[0]);
            return -errno;
        }
    }
    close(p[0]);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -EIO;
}

static int exec_capture(char *const argv[], char *buf, size_t cap) {
    return exec_capture_timeout(argv, buf, cap, 5000);
}

static bool copyq_monitoring(void){char out[64];char*argv[]={"copyq","eval","monitoring() ? '1' : '0'",NULL};int r=exec_capture_timeout(argv,out,sizeof(out),750);return r==0&&strchr(out,'1');}
static void copyq_set(bool enable){char out[64];char*argv[]={"copyq","eval",enable?"enable()":"disable()",NULL};(void)exec_capture_timeout(argv,out,sizeof(out),1000);}
static bool command_exists(const char*cmd){char*path=getenv("PATH");if(!path)return false;char*p=strdup(path);if(!p)return false;bool ok=false;char*save=NULL;for(char*d=strtok_r(p,":",&save);d;d=strtok_r(NULL,":",&save)){char f[PATH_MAX];snprintf(f,sizeof(f),"%s/%s",d,cmd);if(access(f,X_OK)==0){ok=true;break;}}free(p);return ok;}

static bool copyq_should_suspend(struct daemon_state *d) {
    return d->cfg.suppress_copyq && command_exists("copyq") && copyq_monitoring();
}

static int restore_selection(struct daemon_state*d,enum sel_kind kind,const struct item*orig){
    uint64_t g=kind==SEL_CLIPBOARD?d->clipboard_generation:d->primary_generation;
    if(orig->count==0){struct offer_state *current=kind==SEL_CLIPBOARD?d->clipboard_offer:d->primary_offer;if(!current)return 0;clear_selection(d,kind);return wait_generation(d,kind,g,700);}
    struct source_state*marked=publish_item(d,kind,orig,true);if(!marked)return -ENOMEM;
    int r=wait_marker_offer(d,kind,g,500);if(r<0)return r;
    g=kind==SEL_CLIPBOARD?d->clipboard_generation:d->primary_generation;
    struct source_state*exact=publish_item(d,kind,orig,false);if(!exact)return -ENOMEM;
    return wait_generation(d,kind,g,500);
}

static int fsync_parent_dir(const char *path) {
    char dir[PATH_MAX];
    if (strlen(path) >= sizeof(dir)) return -ENAMETOOLONG;
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return 0;
    if (slash == dir) slash[1] = '\0'; else *slash = '\0';
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return -errno;
    int r = fsync(dfd) < 0 ? -errno : 0;
    close(dfd);
    return r;
}

static int open_atomic_temp(const char *path, char *tmp, size_t tmp_cap) {
    if (snprintf(tmp, tmp_cap, "%s.tmp.XXXXXX", path) >= (int)tmp_cap) return -ENAMETOOLONG;
    int fd = mkostemp(tmp, O_CLOEXEC);
    if (fd < 0) return -errno;
    if (fchmod(fd, 0600) < 0) {
        int r = -errno;
        close(fd);
        unlink(tmp);
        return r;
    }
    return fd;
}

static int write_item_path(struct daemon_state *d, const char *path, const struct item *it) {
    (void)d;
    char tmp[PATH_MAX];
    int fd = open_atomic_temp(path, tmp, sizeof(tmp));
    if (fd < 0) return fd;
    uint32_t ver = htole32(2), cnt = htole32((uint32_t)it->count), src = htole32(it->source_kind);
    uint32_t alen = htole32((uint32_t)strlen(it->app_id));
    uint64_t tm = htole64(it->created_ms);
    int r = 0;
    if (write_all(fd, MAGIC, MAGIC_LEN) < 0 || write_all(fd, &ver, 4) < 0 ||
        write_all(fd, &cnt, 4) < 0 || write_all(fd, &tm, 8) < 0 ||
        write_all(fd, &src, 4) < 0 || write_all(fd, &alen, 4) < 0 ||
        write_all(fd, it->app_id, strlen(it->app_id)) < 0) r = -EIO;
    for (size_t i = 0; r == 0 && i < it->count; i++) {
        uint32_t nl = htole32((uint32_t)strlen(it->blobs[i].mime));
        uint64_t dl = htole64(it->blobs[i].len);
        if (write_all(fd, &nl, 4) < 0 || write_all(fd, &dl, 8) < 0 ||
            write_all(fd, it->blobs[i].mime, strlen(it->blobs[i].mime)) < 0 ||
            write_all(fd, it->blobs[i].data, it->blobs[i].len) < 0) r = -EIO;
    }
    if (r == 0 && fsync(fd) < 0) r = -errno;
    if (close(fd) < 0 && r == 0) r = -errno;
    if (r < 0) { unlink(tmp); return r; }
    if (rename(tmp, path) < 0) { r = -errno; unlink(tmp); return r; }
    (void)chmod(path, 0600);
    return fsync_parent_dir(path);
}

static int read_exact(int fd, void *buf, size_t n) {
    uint8_t *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r == 0) return -EIO;
        if (r < 0) { if (errno == EINTR) continue; return -errno; }
        p += (size_t)r; n -= (size_t)r;
    }
    return 0;
}

static int read_item_path(struct daemon_state *d, const char *path, struct item *it) {
    item_init(it);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    char magic[8]; uint32_t ver = 0, cnt = 0, src = 0, alen = 0; uint64_t tm = 0;
    int r = read_exact(fd, magic, sizeof(magic));
    if (r < 0 || memcmp(magic, MAGIC, sizeof(magic)) != 0) { close(fd); return -EINVAL; }
    if ((r = read_exact(fd, &ver, sizeof(ver))) < 0 ||
        (r = read_exact(fd, &cnt, sizeof(cnt))) < 0 ||
        (r = read_exact(fd, &tm, sizeof(tm))) < 0 ||
        (r = read_exact(fd, &src, sizeof(src))) < 0 ||
        (r = read_exact(fd, &alen, sizeof(alen))) < 0) { close(fd); return r; }
    ver = le32toh(ver); cnt = le32toh(cnt); tm = le64toh(tm); src = le32toh(src); alen = le32toh(alen);
    if (ver != 2 || cnt > MAX_MIMES || alen >= MAX_APP_ID) { close(fd); return -EINVAL; }
    if ((r = read_exact(fd, it->app_id, alen)) < 0) { close(fd); return r; }
    it->app_id[alen] = 0; it->created_ms = tm; it->source_kind = src;
    size_t total = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t nl = 0; uint64_t dl = 0;
        if ((r = read_exact(fd, &nl, sizeof(nl))) < 0 || (r = read_exact(fd, &dl, sizeof(dl))) < 0) { item_free(it); close(fd); return r; }
        nl = le32toh(nl); dl = le64toh(dl);
        if (!nl || nl > MAX_MIME_NAME || dl > (uint64_t)d->cfg.max_payload_bytes ||
            dl > SIZE_MAX || total > d->cfg.max_payload_bytes - (size_t)dl) {
            item_free(it); close(fd); return -EFBIG;
        }
        char *m = malloc((size_t)nl + 1U);
        uint8_t *data = dl ? malloc((size_t)dl) : NULL;
        if (!m || (dl && !data)) { free(m); free(data); item_free(it); close(fd); return -ENOMEM; }
        if ((r = read_exact(fd, m, nl)) < 0 || (dl && (r = read_exact(fd, data, (size_t)dl)) < 0)) {
            free(m); free(data); item_free(it); close(fd); return r;
        }
        m[nl] = 0;
        r = item_add(it, m, data, (size_t)dl);
        free(m); free(data);
        if (r < 0) { item_free(it); close(fd); return r; }
        total += (size_t)dl;
    }
    unsigned char extra = 0;
    ssize_t tail;
    do { tail = read(fd, &extra, 1); } while (tail < 0 && errno == EINTR);
    close(fd);
    if (tail != 0) { item_free(it); return tail < 0 ? -errno : -EBADMSG; }
    return 0;
}

static int save_register_file(struct daemon_state *d, const char *name, const struct item *it) {
    if (!valid_register_name(name)) return -EINVAL;
    char dir[PATH_MAX], path[PATH_MAX];
    int r = path_suffix(dir, sizeof(dir), d->data_dir, "/registers"); if (r < 0) return r;
    r = mkdir_p(dir, 0700); if (r < 0) return r;
    if (snprintf(path, sizeof(path), "%s/%s.clipreg", dir, name) >= (int)sizeof(path)) return -ENAMETOOLONG;
    return write_item_path(d, path, it);
}

static int load_register_file(struct daemon_state *d, const char *name, struct item *it) {
    if (!valid_register_name(name)) return -EINVAL;
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/registers/%s.clipreg", d->data_dir, name) >= (int)sizeof(path)) return -ENAMETOOLONG;
    return read_item_path(d, path, it);
}


static int recovery_paths(struct daemon_state *d, char *state, size_t state_cap,
                          char *clip, size_t clip_cap, char *pri, size_t pri_cap) {
    int r = path_suffix(state, state_cap, d->runtime_dir, "/recovery.state");
    if (r < 0) return r;
    r = path_suffix(clip, clip_cap, d->runtime_dir, "/recovery-clipboard.clipreg");
    if (r < 0) return r;
    return path_suffix(pri, pri_cap, d->runtime_dir, "/recovery-primary.clipreg");
}

static int recovery_grab_result_path(struct daemon_state *d, char *path, size_t cap) {
    return path_suffix(path, cap, d->runtime_dir, "/recovery-grab-result.clipreg");
}

static int write_recovery_state(struct daemon_state *d, const struct recovery_state *st) {
    char path[PATH_MAX], clip[PATH_MAX], pri[PATH_MAX], tmp[PATH_MAX];
    int pr = recovery_paths(d, path, sizeof(path), clip, sizeof(clip), pri, sizeof(pri));
    if (pr < 0) return pr;
    (void)clip; (void)pri;
    int fd = open_atomic_temp(path, tmp, sizeof(tmp));
    if (fd < 0) return fd;
    uint32_t ver = htole32(1), kind = htole32((uint32_t)st->kind), flags = 0;
    if (st->clipboard_present) flags |= 1U;
    if (st->primary_present) flags |= 2U;
    if (st->primary_supported) flags |= 4U;
    if (st->copyq_was_monitoring) flags |= 8U;
    uint32_t leflags = htole32(flags); uint64_t started = htole64(st->started_realtime_ms);
    int r = 0;
    if (write_all(fd, RECOVERY_MAGIC, 8) < 0 || write_all(fd, &ver, 4) < 0 ||
        write_all(fd, &kind, 4) < 0 || write_all(fd, &leflags, 4) < 0 ||
        write_all(fd, &started, 8) < 0) r = -EIO;
    if (r == 0 && fsync(fd) < 0) r = -errno;
    if (close(fd) < 0 && r == 0) r = -errno;
    if (r < 0) { unlink(tmp); return r; }
    if (rename(tmp, path) < 0) { r = -errno; unlink(tmp); return r; }
    return fsync_parent_dir(path);
}

static int read_recovery_state(struct daemon_state *d, struct recovery_state *st) {
    memset(st, 0, sizeof(*st));
    char path[PATH_MAX], clip[PATH_MAX], pri[PATH_MAX];
    int pr = recovery_paths(d, path, sizeof(path), clip, sizeof(clip), pri, sizeof(pri));
    if (pr < 0) return pr;
    (void)clip; (void)pri;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    char magic[8]; uint32_t ver = 0, kind = 0, flags = 0; uint64_t started = 0;
    int r = read_exact(fd, magic, sizeof(magic));
    if (r < 0 || memcmp(magic, RECOVERY_MAGIC, sizeof(magic)) != 0) { close(fd); return -EINVAL; }
    if ((r = read_exact(fd, &ver, sizeof(ver))) < 0 ||
        (r = read_exact(fd, &kind, sizeof(kind))) < 0 ||
        (r = read_exact(fd, &flags, sizeof(flags))) < 0 ||
        (r = read_exact(fd, &started, sizeof(started))) < 0) { close(fd); return r; }
    unsigned char extra = 0;
    ssize_t tail;
    do { tail = read(fd, &extra, 1); } while (tail < 0 && errno == EINTR);
    close(fd);
    if (tail != 0) return tail < 0 ? -errno : -EBADMSG;
    ver = le32toh(ver); kind = le32toh(kind); flags = le32toh(flags); started = le64toh(started);
    if (ver != 1 || (kind != RECOVERY_GRAB && kind != RECOVERY_PASTE)) return -EINVAL;
    st->kind = (enum recovery_kind)kind; st->started_realtime_ms = started;
    st->clipboard_present = (flags & 1U) != 0; st->primary_present = (flags & 2U) != 0;
    st->primary_supported = (flags & 4U) != 0; st->copyq_was_monitoring = (flags & 8U) != 0;
    return 0;
}

static void recovery_clear(struct daemon_state *d) {
    char state[PATH_MAX], clip[PATH_MAX], pri[PATH_MAX], grab_result[PATH_MAX];
    if (recovery_paths(d, state, sizeof(state), clip, sizeof(clip), pri, sizeof(pri)) < 0) return;
    if (recovery_grab_result_path(d, grab_result, sizeof(grab_result)) < 0) return;
    (void)unlink(state);
    (void)unlink(clip);
    (void)unlink(pri);
    (void)unlink(grab_result);
    (void)fsync_parent_dir(state);
}

static int transaction_prepare(struct daemon_state *d, enum recovery_kind kind,
                               const struct item *clip, bool clip_present,
                               const struct item *pri, bool primary_present, bool primary_supported) {
    if (d->in_transaction) return -EBUSY;
    struct recovery_state st = {
        .kind = kind, .started_realtime_ms = realtime_ms(),
        .clipboard_present = clip_present, .primary_present = primary_present,
        .primary_supported = primary_supported, .copyq_was_monitoring = copyq_should_suspend(d),
    };
    char state_path[PATH_MAX], clip_path[PATH_MAX], pri_path[PATH_MAX];
    int r = recovery_paths(d, state_path, sizeof(state_path), clip_path, sizeof(clip_path), pri_path, sizeof(pri_path));
    if (r < 0) return r;
    recovery_clear(d);
    if (clip_present && clip && (r = write_item_path(d, clip_path, clip)) < 0) goto fail;
    if (primary_supported && primary_present && pri && (r = write_item_path(d, pri_path, pri)) < 0) goto fail;
    if ((r = write_recovery_state(d, &st)) < 0) goto fail;
    d->copyq_was_monitoring = st.copyq_was_monitoring;
    if (d->copyq_was_monitoring) copyq_set(false);
    d->in_transaction = true;
    return 0;
fail:
    recovery_clear(d);
    return r;
}

static void transaction_finish(struct daemon_state *d) {
    if (d->copyq_was_monitoring) copyq_set(true);
    d->copyq_was_monitoring = false; d->in_transaction = false; recovery_clear(d);
}

static void transaction_leave_recovery(struct daemon_state *d) {
    if (d->copyq_was_monitoring) copyq_set(true);
    d->copyq_was_monitoring = false; d->in_transaction = false;
    /* Keep recovery files and force systemd to restart us so startup recovery retries. */
    d->restart_requested = true; d->running = false;
}

static int restore_from_recovery_item(struct daemon_state *d, enum sel_kind kind,
                                      bool present, const char *path) {
    if (!present) {
        struct offer_state *current = kind == SEL_CLIPBOARD ? d->clipboard_offer : d->primary_offer;
        if (!current) return 0;
        uint64_t old = kind == SEL_CLIPBOARD ? d->clipboard_generation : d->primary_generation;
        clear_selection(d, kind);
        return wait_generation(d, kind, old, 700);
    }
    struct item it; int r = read_item_path(d, path, &it); if (r < 0) return r;
    r = restore_selection(d, kind, &it); item_free(&it); return r;
}

static int recover_if_needed(struct daemon_state *d) {
    struct recovery_state st = {0}; int r = read_recovery_state(d, &st);
    if (r == -ENOENT) return 0;
    if (r < 0) { logmsg("discarding invalid ClipReg recovery state"); recovery_clear(d); return 0; }
    char state_path[PATH_MAX], clip_path[PATH_MAX], pri_path[PATH_MAX], grab_result_path[PATH_MAX];
    r = recovery_paths(d, state_path, sizeof(state_path), clip_path, sizeof(clip_path), pri_path, sizeof(pri_path));
    if (r < 0) return r;
    r = recovery_grab_result_path(d, grab_result_path, sizeof(grab_result_path));
    if (r < 0) return r;
    uint64_t now = realtime_ms();
    uint64_t age = now >= st.started_realtime_ms ? now - st.started_realtime_ms : UINT64_MAX;
    bool restore_clip = false, restore_pri = false;

    if (st.kind == RECOVERY_GRAB) {
        /*
         * Never restore merely because a grab is recent. Prove that the
         * clipboard still contains a ClipReg-owned sentinel/staging source or
         * exactly the application Copy result recorded by the interrupted
         * transaction. If a newer external clipboard exists, preserve it.
         */
        if (age <= RECOVERY_MAX_AGE_MS) {
            if (d->clipboard_offer == NULL || selection_has_owner_marker(d, SEL_CLIPBOARD)) {
                restore_clip = true;
            } else {
                struct item expected, current;
                int er = read_item_path(d, grab_result_path, &expected);
                if (er == 0) {
                    bool present = false; uint64_t generation = 0;
                    int cr = stable_snapshot_current(d, SEL_CLIPBOARD, &current, &present, &generation);
                    (void)generation;
                    if (cr == 0 && present && item_content_equal(&expected, &current)) restore_clip = true;
                    if (cr == 0) item_free(&current);
                    item_free(&expected);
                }
            }
        }
    } else if (st.kind == RECOVERY_PASTE) {
        restore_clip = d->clipboard_offer == NULL || selection_has_owner_marker(d, SEL_CLIPBOARD);
        if (st.primary_supported && d->primary_supported)
            restore_pri = d->primary_offer == NULL || selection_has_owner_marker(d, SEL_PRIMARY);
    }

    if (restore_clip) {
        r = restore_from_recovery_item(d, SEL_CLIPBOARD, st.clipboard_present, clip_path);
        if (r < 0) { logmsg("clipboard crash recovery failed: %d", r); return r; }
    } else {
        logmsg("preserving newer clipboard instead of applying stale/unproven recovery (%llums old)",
               (unsigned long long)age);
    }
    if (restore_pri) {
        r = restore_from_recovery_item(d, SEL_PRIMARY, st.primary_present, pri_path);
        if (r < 0) { logmsg("primary-selection crash recovery failed: %d", r); return r; }
    }
    if (st.copyq_was_monitoring && command_exists("copyq")) copyq_set(true);
    recovery_clear(d);
    d->primary_affinity_valid = false;
    logmsg("recovered interrupted %s transaction", st.kind == RECOVERY_GRAB ? "grab" : "paste");
    return 0;
}

static int capture_primary_if_affine(struct daemon_state*d,struct item*out){
    if(!d->primary_affinity_valid||strcmp(d->primary_affinity_app,d->active_app)||d->primary_affinity_focus_generation!=d->active_app_generation||!d->primary_offer)return -ENOENT;
    return snapshot_current(d,SEL_PRIMARY,out);
}

static int restore_if_still_ours(struct daemon_state *d, enum sel_kind kind,
                                 const struct item *original, bool *preserved_newer);

static int command_save(struct daemon_state *d, const char *reg, enum sel_kind kind,
                        uint32_t source, char *reply, size_t rcap) {
    if (!valid_register_name(reg)) return -EINVAL;
    struct item raw, content; bool present = false; uint64_t generation = 0;
    int r = stable_snapshot_current(d, kind, &raw, &present, &generation);
    (void)generation;
    if (r < 0) return r;
    if (!present || raw.count == 0) { item_free(&raw); return -ENODATA; }
    if (is_secret_item(&raw)) { item_free(&raw); return -EPERM; }
    raw.source_kind = source; snprintf(raw.app_id, sizeof(raw.app_id), "%s", d->active_app);
    r = item_content_copy(&raw, &content); item_free(&raw); if (r < 0) return r;
    content.source_kind = source; snprintf(content.app_id, sizeof(content.app_id), "%s", d->active_app);
    r = save_register_file(d, reg, &content); item_free(&content);
    if (r == 0) snprintf(reply, rcap, "%s %s", kind == SEL_PRIMARY ? "saved primary ->" : "saved clipboard ->", reg);
    return r;
}

static int command_grab(struct daemon_state *d, const char *reg, uint64_t received,
                        char *reply, size_t rcap) {
    if (!valid_register_name(reg)) return -EINVAL;
    int r = drain_wayland(d); if (r < 0) return r;
    const struct app_profile *p = profile_for(d, d->active_app); if (!p) return -ENOENT;

    if (!strcmp(p->grab_strategy, "primary-only") || !strcmp(p->grab_strategy, "primary-first")) {
        struct item pri, content;
        int pr = capture_primary_if_affine(d, &pri);
        if (pr == 0 && pri.count) {
            if (is_secret_item(&pri)) { item_free(&pri); return -EPERM; }
            pri.source_kind = 2; snprintf(pri.app_id, sizeof(pri.app_id), "%s", d->active_app);
            pr = item_content_copy(&pri, &content); item_free(&pri);
            if (pr == 0) {
                content.source_kind = 2; snprintf(content.app_id, sizeof(content.app_id), "%s", d->active_app);
                pr = save_register_file(d, reg, &content); item_free(&content);
                if (pr == 0) { snprintf(reply, rcap, "grabbed selection -> %s (primary, %s)", reg, d->active_app); return 0; }
            }
        }
        if (!strcmp(p->grab_strategy, "primary-only")) return -ENODATA;
    }

    struct item original; bool original_present = false; uint64_t original_gen = 0;
    r = stable_snapshot_current(d, SEL_CLIPBOARD, &original, &original_present, &original_gen);
    if (r < 0) return r;
    r = transaction_prepare(d, RECOVERY_GRAB, &original, original_present, NULL, false, false);
    if (r < 0) { item_free(&original); return r; }

    hotkey_release_guard(d, received);
    r = drain_wayland(d);
    if (r < 0 || d->clipboard_generation != original_gen) {
        transaction_finish(d); item_free(&original); return r < 0 ? r : -EAGAIN;
    }

    /*
     * Publish an owner-marked sentinel before asking the application to Copy.
     * This makes success observable even when the selected bytes happen to be
     * identical to the user's pre-existing clipboard. CopyQ is suspended for
     * the transaction, so the sentinel never becomes history noise.
     */
    struct item sentinel; item_init(&sentinel);
    uint64_t sentinel_before = d->clipboard_generation;
    struct source_state *sentinel_source = publish_item(d, SEL_CLIPBOARD, &sentinel, true);
    if (!sentinel_source) { transaction_finish(d); item_free(&original); return -ENOMEM; }
    r = wait_marker_offer(d, SEL_CLIPBOARD, sentinel_before, 700);
    if (r < 0) {
        (void)restore_selection(d, SEL_CLIPBOARD, &original);
        transaction_finish(d); item_free(&original); return r;
    }

    uint64_t focus_generation = d->active_app_generation;
    uint64_t before = d->clipboard_generation;
    r = inject_chord(d, d->cfg.safe_copy_chord);
    if (r == 0) r = wait_external_generation(d, SEL_CLIPBOARD, before, d->cfg.safe_probe_timeout_ms);

    if (r < 0 && d->active_app_generation == focus_generation && strcmp(p->copy_chord, "-")) {
        before = d->clipboard_generation;
        r = inject_chord(d, p->copy_chord);
        if (r == 0) r = wait_external_generation(d, SEL_CLIPBOARD, before, d->cfg.copy_timeout_ms);
    }

    if (r < 0 || d->active_app_generation != focus_generation) {
        bool newer = false;
        int rr = restore_if_still_ours(d, SEL_CLIPBOARD, &original, &newer);
        item_free(&original);
        if (rr < 0) { transaction_leave_recovery(d); return rr; }
        transaction_finish(d);
        return r < 0 ? r : -EAGAIN;
    }

    uint64_t copied_gen = d->clipboard_generation;
    struct item copied, content;
    r = snapshot_current(d, SEL_CLIPBOARD, &copied);
    if (r < 0) {
        bool newer = false; (void)restore_if_still_ours(d, SEL_CLIPBOARD, &original, &newer);
        transaction_finish(d); item_free(&original); return r;
    }
    r = drain_wayland(d);
    if (r < 0 || d->clipboard_generation != copied_gen || d->active_app_generation != focus_generation) {
        /* A newer clipboard/focus supersedes our Copy result. Never overwrite it. */
        item_free(&copied); item_free(&original); transaction_finish(d); return r < 0 ? r : -EAGAIN;
    }
    if (copied.count == 0 || is_secret_item(&copied)) {
        bool secret = copied.count && is_secret_item(&copied); item_free(&copied);
        int rr = restore_selection(d, SEL_CLIPBOARD, &original); item_free(&original);
        if (rr < 0) { transaction_leave_recovery(d); return rr; }
        transaction_finish(d); return secret ? -EPERM : -ENODATA;
    }
    copied.source_kind = 1; snprintf(copied.app_id, sizeof(copied.app_id), "%s", d->active_app);
    r = item_content_copy(&copied, &content); item_free(&copied);
    if (r < 0) {
        int rr = restore_selection(d, SEL_CLIPBOARD, &original); item_free(&original);
        if (rr < 0) { transaction_leave_recovery(d); return rr; }
        transaction_finish(d); return r;
    }

    /*
     * Record the exact non-secret Copy result before restoration. Startup
     * recovery may restore the old clipboard only while the current clipboard
     * still matches this result (or our sentinel). This prevents a daemon
     * restart from clobbering a newer user clipboard.
     */
    char grab_result_path[PATH_MAX];
    r = recovery_grab_result_path(d, grab_result_path, sizeof(grab_result_path));
    if (r == 0) r = write_item_path(d, grab_result_path, &content);
    if (r < 0) {
        int rr = restore_selection(d, SEL_CLIPBOARD, &original); item_free(&original); item_free(&content);
        if (rr < 0) { transaction_leave_recovery(d); return rr; }
        transaction_finish(d); return r;
    }

    int rr = restore_selection(d, SEL_CLIPBOARD, &original); item_free(&original);
    if (rr < 0) { item_free(&content); transaction_leave_recovery(d); return rr; }
    transaction_finish(d);
    content.source_kind = 1; snprintf(content.app_id, sizeof(content.app_id), "%s", d->active_app);
    r = save_register_file(d, reg, &content); item_free(&content);
    if (r == 0) snprintf(reply, rcap, "grabbed selection -> %s (%s)", reg, d->active_app[0] ? d->active_app : "active app");
    return r;
}

static int restore_if_still_ours(struct daemon_state *d, enum sel_kind kind,
                                 const struct item *original, bool *preserved_newer) {
    if (!selection_has_owner_marker(d, kind)) {
        *preserved_newer = true;
        return 0;
    }
    return restore_selection(d, kind, original);
}

static int command_paste(struct daemon_state *d, const char *reg, uint64_t received,
                         char *reply, size_t rcap) {
    if (!valid_register_name(reg)) return -EINVAL;
    struct item regitem, orig_clip, orig_pri; bool clip_present = false, pri_present = false;
    uint64_t clip_gen = 0, pri_gen = 0;
    int r = load_register_file(d, reg, &regitem); if (r < 0) return r;
    r = stable_snapshot_current(d, SEL_CLIPBOARD, &orig_clip, &clip_present, &clip_gen);
    if (r < 0) { item_free(&regitem); return r; }
    item_init(&orig_pri);
    if (d->cfg.stage_primary_for_paste && d->primary_supported) {
        r = stable_snapshot_current(d, SEL_PRIMARY, &orig_pri, &pri_present, &pri_gen);
        if (r < 0) { item_free(&regitem); item_free(&orig_clip); return r; }
    }
    const struct app_profile *p = profile_for(d, d->active_app);
    if (!p) { item_free(&regitem); item_free(&orig_clip); item_free(&orig_pri); return -ENOENT; }
    r = transaction_prepare(d, RECOVERY_PASTE, &orig_clip, clip_present, &orig_pri, pri_present,
                            d->cfg.stage_primary_for_paste && d->primary_supported);
    if (r < 0) { item_free(&regitem); item_free(&orig_clip); item_free(&orig_pri); return r; }

    /* If something changed between stable snapshot and staging, fail closed. */
    r = drain_wayland(d);
    if (r < 0 || d->clipboard_generation != clip_gen ||
        (d->cfg.stage_primary_for_paste && d->primary_supported && d->primary_generation != pri_gen)) {
        transaction_finish(d); item_free(&regitem); item_free(&orig_clip); item_free(&orig_pri);
        return r < 0 ? r : -EAGAIN;
    }

    uint64_t gc = d->clipboard_generation, gp = d->primary_generation;
    struct source_state *sc = publish_item(d, SEL_CLIPBOARD, &regitem, true), *sp = NULL;
    if (!sc) { r = -ENOMEM; goto out_restore; }
    r = wait_marker_offer(d, SEL_CLIPBOARD, gc, 700); if (r < 0) goto out_restore;
    if (d->cfg.stage_primary_for_paste && d->primary_supported) {
        sp = publish_item(d, SEL_PRIMARY, &regitem, true);
        if (!sp) { r = -ENOMEM; goto out_restore; }
        r = wait_marker_offer(d, SEL_PRIMARY, gp, 700); if (r < 0) goto out_restore;
    }

    /* Let clipboard watchers make immediate reads, then establish the request baseline. */
    (void)wait_source_quiet(d, sc, d->cfg.request_quiet_ms, 600);
    if (sp) (void)wait_source_quiet(d, sp, d->cfg.request_quiet_ms, 600);
    hotkey_release_guard(d, received);
    uint64_t bc = sc->send_count, bp = sp ? sp->send_count : 0;
    uint64_t fc = sc->send_fail_count, fp = sp ? sp->send_fail_count : 0;

    /* Paste exactly once. A retry with a different accelerator can double-paste
     * when the target consumes a cached selection without a new data request. */
    const char *paste_chord = strcmp(p->paste_chord, "-") ? p->paste_chord : d->cfg.safe_paste_chord;
    if (!strcmp(p->paste_chord, "-") && (!d->cfg.stage_primary_for_paste || !d->primary_supported)) {
        r = -ENOTSUP;
    } else {
        r = inject_chord(d, paste_chord);
        if (r == 0) {
            uint64_t deadline = now_ms() + (uint64_t)d->cfg.paste_timeout_ms;
            while (now_ms() < deadline && sc->send_count == bc && (!sp || sp->send_count == bp)) {
                int dr = dispatch_once(d, (int)(deadline - now_ms())); if (dr < 0) { r = dr; break; }
            }
            if (sc->send_count == bc && (!sp || sp->send_count == bp)) r = -ETIMEDOUT;
        }
    }
    if (r == 0) {
        (void)wait_source_quiet(d, sc, d->cfg.request_quiet_ms, d->cfg.transfer_timeout_ms);
        if (sp) (void)wait_source_quiet(d, sp, d->cfg.request_quiet_ms, d->cfg.transfer_timeout_ms);
        if (sc->send_fail_count != fc || (sp && sp->send_fail_count != fp)) r = -EIO;
    }

out_restore:;
    bool newer_clip = false, newer_pri = false;
    int rc = restore_if_still_ours(d, SEL_CLIPBOARD, &orig_clip, &newer_clip);
    int rp = 0;
    if (d->cfg.stage_primary_for_paste && d->primary_supported)
        rp = restore_if_still_ours(d, SEL_PRIMARY, &orig_pri, &newer_pri);
    d->primary_affinity_valid = false;
    item_free(&regitem); item_free(&orig_clip); item_free(&orig_pri);
    if (rc < 0 || rp < 0) { transaction_leave_recovery(d); return rc < 0 ? rc : rp; }
    transaction_finish(d);
    if (r < 0) return r;
    if (newer_clip || newer_pri)
        snprintf(reply, rcap, "pasted %s; preserved newer %s selection", reg, newer_clip ? "clipboard" : "primary");
    else
        snprintf(reply, rcap, "pasted %s into %s", reg, d->active_app[0] ? d->active_app : "active app");
    return 0;
}

static int command_copy(struct daemon_state *d, const char *reg, char *reply, size_t rcap) {
    struct item it; int r = load_register_file(d, reg, &it); if (r < 0) return r;
    uint64_t g = d->clipboard_generation;
    struct source_state *s = publish_item(d, SEL_CLIPBOARD, &it, false); item_free(&it);
    if (!s) return -ENOMEM;
    r = wait_generation(d, SEL_CLIPBOARD, g, 700);
    if (r == 0) snprintf(reply, rcap, "copied %s -> clipboard", reg);
    return r;
}

static int command_clear(struct daemon_state *d, const char *reg, char *reply, size_t rcap) {
    if (!valid_register_name(reg)) return -EINVAL;
    char suffix[128], path[PATH_MAX];
    int n = snprintf(suffix, sizeof(suffix), "/registers/%s.clipreg", reg);
    if (n < 0 || (size_t)n >= sizeof(suffix)) return -ENAMETOOLONG;
    int r = path_suffix(path, sizeof(path), d->data_dir, suffix); if (r < 0) return r;
    if (unlink(path) < 0) return -errno;
    r = fsync_parent_dir(path);
    if (r < 0) return r;
    snprintf(reply, rcap, "cleared %s", reg);
    return 0;
}

static int ignore_sigpipe(void);

static bool item_equal(const struct item *a, const struct item *b) {
    if (a->created_ms != b->created_ms || a->source_kind != b->source_kind ||
        strcmp(a->app_id, b->app_id) || a->count != b->count) return false;
    for (size_t i = 0; i < a->count; i++) {
        const struct blob *x = &a->blobs[i], *y = &b->blobs[i];
        if (strcmp(x->mime, y->mime) || x->len != y->len) return false;
        if (x->len && memcmp(x->data, y->data, x->len)) return false;
    }
    return true;
}

static int selftest_main(void) {
    char tmp[] = "/tmp/clipreg-selftest-XXXXXX";
    if (!mkdtemp(tmp)) { perror("clipreg selftest mkdtemp"); return 1; }
    struct daemon_state d = {0};
    default_config(&d.cfg);
    snprintf(d.data_dir, sizeof(d.data_dir), "%s", tmp);

    struct item original, loaded, content;
    item_init(&original); item_init(&loaded); item_init(&content);
    original.created_ms = 123456789ULL;
    original.source_kind = 77U;
    snprintf(original.app_id, sizeof(original.app_id), "selftest.app");
    const char text[] = "hello\nClipReg";
    const uint8_t binary[] = {0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff};
    int r = item_add(&original, "text/plain;charset=utf-8", text, sizeof(text) - 1U);
    if (r == 0) r = item_add(&original, "application/octet-stream", binary, sizeof(binary));
    if (r == 0) r = save_register_file(&d, "roundtrip", &original);
    if (r == 0) r = load_register_file(&d, "roundtrip", &loaded);
    if (r == 0 && !item_equal(&original, &loaded)) r = -EBADMSG;

    /* The decoder must reject a valid object with trailing garbage rather than
     * silently accepting a partially-corrupted/concatenated register file. */
    if (r == 0) {
        char corrupt_path[PATH_MAX];
        snprintf(corrupt_path, sizeof(corrupt_path), "%s/registers/roundtrip.clipreg", tmp);
        int cfd = open(corrupt_path, O_WRONLY | O_APPEND | O_CLOEXEC);
        if (cfd < 0) r = -errno;
        else {
            unsigned char extra = 0xA5;
            int wr = write_all(cfd, &extra, 1U);
            close(cfd);
            if (wr < 0) r = wr;
            else {
                struct item corrupt; item_init(&corrupt);
                int cr = load_register_file(&d, "roundtrip", &corrupt);
                item_free(&corrupt);
                if (cr != -EBADMSG) r = -EBADMSG;
            }
        }
    }

    /* Metadata owned by ClipReg/CopyQ must never leak into persistent content. */
    if (r == 0) r = item_add(&original, OWNER_MIME, "1", 1U);
    if (r == 0) r = item_content_copy(&original, &content);
    if (r == 0 && (item_find(&content, OWNER_MIME) || content.count != 2U)) r = -EBADMSG;

    /* Secret markers must be recognized before persistent storage. */
    struct item secret; item_init(&secret);
    if (r == 0) r = item_add(&secret, KDE_SECRET, "secret", 6U);
    if (r == 0 && !is_secret_item(&secret)) r = -EBADMSG;
    item_free(&secret);

    if (r == 0 && (!valid_register_name("F12") || !valid_register_name("address") ||
                   valid_register_name("bad/name") || valid_register_name(""))) r = -EBADMSG;

    /* A clipboard consumer may close the Wayland transfer pipe before we
     * finish writing. That must produce EPIPE, never terminate the daemon with
     * SIGPIPE. This regression test mirrors source_send()'s write path. */
    if (r == 0) {
        r = ignore_sigpipe();
        if (r == 0) {
            int pp[2];
            if (pipe2(pp, O_CLOEXEC) < 0) r = -errno;
            else {
                close(pp[0]);
                const unsigned char byte = 0x42;
                int wr = write_all_timeout(pp[1], &byte, 1U, 100);
                close(pp[1]);
                if (wr != -EPIPE) r = -EBADMSG;
            }
        }
    }

    item_free(&content); item_free(&loaded); item_free(&original);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/registers/roundtrip.clipreg", tmp); (void)unlink(path);
    snprintf(path, sizeof(path), "%s/registers", tmp); (void)rmdir(path);
    (void)rmdir(tmp);

    if (r < 0) {
        fprintf(stderr, "ClipReg %s self-test: FAILED (%s)\n", CLIPREG_VERSION, strerror(-r));
        return 1;
    }
    printf("ClipReg %s self-test: OK\n", CLIPREG_VERSION);
    return 0;
}

static const struct blob *preferred_text(const struct item*it){const struct blob*b=item_find(it,"text/plain;charset=utf-8");if(b)return b;b=item_find(it,"text/plain");if(b)return b;b=item_find(it,"UTF8_STRING");return b;}

static int command_show(struct daemon_state*d,const char*reg,char*reply,size_t rcap){struct item it;int r=load_register_file(d,reg,&it);if(r<0)return r;size_t off=0;off+=(size_t)snprintf(reply+off,rcap-off,"register=%s\nsource=%u\napp=%s\ncreated_ms=%llu\nmimes=%zu\n",reg,it.source_kind,it.app_id,(unsigned long long)it.created_ms,it.count);for(size_t i=0;i<it.count&&off<rcap; i++)off+=(size_t)snprintf(reply+off,rcap-off,"  %s (%zu bytes)\n",it.blobs[i].mime,it.blobs[i].len);const struct blob*b=preferred_text(&it);if(b&&off<rcap-16){size_t n=b->len;if(n>rcap-off-16)n=rcap-off-16;off+=(size_t)snprintf(reply+off,rcap-off,"text:\n");memcpy(reply+off,b->data,n);off+=n;reply[off]=0;}item_free(&it);return 0;}

static int command_list(struct daemon_state*d,char*reply,size_t rcap){char dir[PATH_MAX];int path_r=path_suffix(dir,sizeof(dir),d->data_dir,"/registers");if(path_r<0)return path_r;DIR*dp=opendir(dir);if(!dp){if(errno==ENOENT){snprintf(reply,rcap,"(no registers)");return 0;}return -errno;}size_t off=0;struct dirent*de;while((de=readdir(dp))){size_t l=strlen(de->d_name);if(l<=8||strcmp(de->d_name+l-8,".clipreg"))continue;char name[256];size_t n=l-8;if(n>=sizeof(name))continue;memcpy(name,de->d_name,n);name[n]=0;struct item it;if(load_register_file(d,name,&it)<0)continue;const struct blob*b=preferred_text(&it);char prev[80]="[rich/non-text]";if(b){size_t pn=b->len<70?b->len:70;memcpy(prev,b->data,pn);prev[pn]=0;for(size_t i=0;i<pn;i++)if(prev[i]=='\n'||prev[i]=='\r'||prev[i]=='\t')prev[i]=' ';}off+=(size_t)snprintf(reply+off,off<rcap?rcap-off:0,"%-12s %s\n",name,prev);item_free(&it);if(off>=rcap-1)break;}closedir(dp);if(off==0)snprintf(reply,rcap,"(no registers)");return 0;}

static int command_app(struct daemon_state*d,char*reply,size_t rcap){const struct app_profile*p=profile_for(d,d->active_app);snprintf(reply,rcap,"app=%s\ncopy_fallback=%s\npaste_fallback=%s\ngrab_strategy=%s\nprimary_affinity=%s",d->active_app[0]?d->active_app:"(unknown)",p?p->copy_chord:"",p?p->paste_chord:"",p?p->grab_strategy:"",d->primary_affinity_valid?"valid":"invalid");return 0;}

static int command_doctor(struct daemon_state*d,char*reply,size_t rcap){(void)snprintf(reply,rcap,"ClipReg %s\nWayland: %s\next-data-control-v1: %s\nCOSMIC toplevel-info: %s\nuinput: %s\nactive app: %s\nCopyQ suppression: %s\n",CLIPREG_VERSION,d->display?"OK":"FAIL",d->dc_manager?"OK":"FAIL",d->tl_info?"OK":"MISSING (generic profile only)",d->uinput_fd>=0?"OK":"FAIL",d->active_app[0]?d->active_app:"unknown",d->cfg.suppress_copyq?"enabled":"disabled");return (d->display&&d->dc_manager&&d->uinput_fd>=0)?0:-ENOTSUP;}

static const char *err_name(int e){e=-e;switch(e){case ENODATA:return "no selectable/clipboard content";case EPERM:return "content is marked secret and was not stored";case ETIMEDOUT:return "target application did not complete the clipboard operation";case EINVAL:return "invalid argument/configuration";case ENOENT:return "register or resource not found";case ENOTSUP:return "required compositor/input capability unavailable";case EAGAIN:return "clipboard changed concurrently; operation aborted";case EFBIG:return "clipboard content exceeds configured size limit";default:return strerror(e);}}

static int handle_command(struct daemon_state*d,const char*line,char*reply,size_t rcap){char buf[512];snprintf(buf,sizeof(buf),"%s",line);char*save=NULL;char*cmd=strtok_r(buf," \t\r\n",&save);char*arg=strtok_r(NULL," \t\r\n",&save);if(!cmd)return -EINVAL;uint64_t received=now_ms();if(!strcmp(cmd,"save")){if(!arg)return -EINVAL;return command_save(d,arg,SEL_CLIPBOARD,3,reply,rcap);}if(!strcmp(cmd,"primary")){if(!arg)return -EINVAL;return command_save(d,arg,SEL_PRIMARY,4,reply,rcap);}if(!strcmp(cmd,"grab")){if(!arg)return -EINVAL;return command_grab(d,arg,received,reply,rcap);}if(!strcmp(cmd,"paste")){if(!arg)return -EINVAL;return command_paste(d,arg,received,reply,rcap);}if(!strcmp(cmd,"copy")){if(!arg)return -EINVAL;return command_copy(d,arg,reply,rcap);}if(!strcmp(cmd,"clear")){if(!arg)return -EINVAL;return command_clear(d,arg,reply,rcap);}if(!strcmp(cmd,"show")){if(!arg)return -EINVAL;return command_show(d,arg,reply,rcap);}if(!strcmp(cmd,"list"))return command_list(d,reply,rcap);if(!strcmp(cmd,"app"))return command_app(d,reply,rcap);if(!strcmp(cmd,"doctor"))return command_doctor(d,reply,rcap);if(!strcmp(cmd,"reload")){load_config_file(d);load_profiles(d);snprintf(reply,rcap,"configuration reloaded");return 0;}return -EINVAL;}

static void prune_sources(struct daemon_state *d) {
    if (d->in_transaction) return;
    struct source_state **pp = &d->sources;
    while (*pp) {
        struct source_state *s = *pp;
        if (s->cancelled && !s->proxy) {
            *pp = s->next; item_free(&s->item); free(s); continue;
        }
        pp = &s->next;
    }
}

static void prune_tops(struct daemon_state *d) {
    struct top_state **pp = &d->tops;
    while (*pp) {
        struct top_state *t = *pp;
        if (!t->proxy) { *pp = t->next; free(t); continue; }
        pp = &t->next;
    }
}

static int setup_paths(struct daemon_state *d) {
    const char *rt = getenv("XDG_RUNTIME_DIR"), *home = getenv("HOME");
    const char *data = getenv("XDG_DATA_HOME"), *cfg = getenv("XDG_CONFIG_HOME");
    if (!rt || !home) return -EINVAL;
    int r = path_suffix(d->runtime_dir, sizeof(d->runtime_dir), rt, "/clipreg"); if (r < 0) return r;
    r = path_suffix(d->data_dir, sizeof(d->data_dir), data && *data ? data : home,
                    data && *data ? "/clipreg" : "/.local/share/clipreg"); if (r < 0) return r;
    r = path_suffix(d->config_dir, sizeof(d->config_dir), cfg && *cfg ? cfg : home,
                    cfg && *cfg ? "/clipreg" : "/.config/clipreg"); if (r < 0) return r;
    r = path_suffix(d->socket_path, sizeof(d->socket_path), d->runtime_dir, "/clipreg.sock"); if (r < 0) return r;
    if ((r = mkdir_p(d->runtime_dir, 0700)) < 0) return r;
    if ((r = mkdir_p(d->data_dir, 0700)) < 0) return r;
    if ((r = mkdir_p(d->config_dir, 0700)) < 0) return r;
    return 0;
}

static int setup_socket(struct daemon_state*d){unlink(d->socket_path);int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return -errno;struct sockaddr_un a={0};a.sun_family=AF_UNIX;if(strlen(d->socket_path)>=sizeof(a.sun_path)){close(fd);return -ENAMETOOLONG;}strcpy(a.sun_path,d->socket_path);if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0){int e=-errno;close(fd);return e;}chmod(d->socket_path,0600);if(listen(fd,16)<0){int e=-errno;close(fd);return e;}d->listen_fd=fd;return 0;}

static void sig_handler(int sig){(void)sig;if(G)G->running=false;}

/* Wayland clipboard consumers receive MIME payloads over pipes. A consumer is
 * allowed to close its read end early (for example after probing a format).
 * Without ignoring SIGPIPE, a perfectly ordinary short read can terminate the
 * entire daemon before write(2) has a chance to report EPIPE. Treat it as an
 * I/O failure for that transfer instead; source_send() records the failed
 * transfer and the transaction can recover normally. */
static int ignore_sigpipe(void) {
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    if (sigemptyset(&sa.sa_mask) < 0) return -errno;
    if (sigaction(SIGPIPE, &sa, NULL) < 0) return -errno;
    return 0;
}

static int daemon_main(void) {
    struct daemon_state d = {0};
    d.uinput_fd = -1; d.listen_fd = -1; d.running = true;
    if (setup_paths(&d) < 0) return 1;
    G = &d;
    int sr = ignore_sigpipe();
    if (sr < 0) { logmsg("could not ignore SIGPIPE: %s", err_name(sr)); G = NULL; return 1; }
    load_config_file(&d); load_profiles(&d);
    int r = setup_wayland(&d);
    if (r < 0) { logmsg("Wayland setup failed: %s", err_name(r)); G = NULL; return 1; }
    r = recover_if_needed(&d);
    if (r < 0) { logmsg("recovery failed; will retry after restart: %s", err_name(r)); G = NULL; return 1; }
    d.uinput_fd = setup_uinput();
    if (d.uinput_fd < 0) { logmsg("uinput setup failed: %s", err_name(d.uinput_fd)); G = NULL; return 1; }
    r = setup_socket(&d);
    if (r < 0) { logmsg("socket setup failed: %s", err_name(r)); G = NULL; return 1; }
    signal(SIGTERM, sig_handler); signal(SIGINT, sig_handler); signal(SIGHUP, SIG_IGN);
    logmsg("ready (%s)", CLIPREG_VERSION);

    while (d.running) {
        struct pollfd pf[2] = {
            {.fd = wl_display_get_fd(d.display), .events = POLLIN},
            {.fd = d.listen_fd, .events = POLLIN},
        };
        (void)wl_display_dispatch_pending(d.display); (void)wl_display_flush(d.display);
        int pr = poll(pf, 2, 250);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pf[0].revents & POLLIN) { if (wl_display_dispatch(d.display) < 0) break; }
        if (pf[1].revents & POLLIN) {
            int c = accept4(d.listen_fd, NULL, NULL, SOCK_CLOEXEC);
            if (c >= 0) {
                char req[512], reply[MAX_REPLY]; ssize_t n = recv(c, req, sizeof(req) - 1, 0);
                if (n > 0) {
                    req[n] = 0; reply[0] = 0;
                    int cr = handle_command(&d, req, reply, sizeof(reply)); char out[MAX_REPLY + 64];
                    if (cr < 0) snprintf(out, sizeof(out), "ERR %d %s", -cr, err_name(cr));
                    else snprintf(out, sizeof(out), "OK %s", reply);
                    (void)send(c, out, strlen(out), 0);
                }
                close(c);
            }
        }
        prune_sources(&d); prune_tops(&d);
    }

    if (d.copyq_was_monitoring) copyq_set(true);
    unlink(d.socket_path);
    if (d.uinput_fd >= 0) { (void)ioctl(d.uinput_fd, UI_DEV_DESTROY); close(d.uinput_fd); }
    prune_sources(&d); prune_tops(&d);
    if (d.display) wl_display_disconnect(d.display);
    free_profiles(d.profiles);
    G = NULL;
    return d.restart_requested ? 1 : 0;
}

static int client_socket_path(char *out, size_t cap) {
    const char *rt = getenv("XDG_RUNTIME_DIR"); if (!rt) return -EINVAL;
    if (snprintf(out, cap, "%s/clipreg/clipreg.sock", rt) >= (int)cap) return -ENAMETOOLONG;
    return 0;
}

static int connect_daemon_socket(const char *path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0); if (fd < 0) return -errno;
    struct sockaddr_un a = {0}; a.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(a.sun_path)) { close(fd); return -ENAMETOOLONG; }
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { int e = -errno; close(fd); return e; }
    return fd;
}

static void try_start_daemon_service(void) {
    if (!command_exists("systemctl")) return;
    char out[256]; char *argv[] = {"systemctl", "--user", "start", "clipreg.service", NULL};
    (void)exec_capture(argv, out, sizeof(out));
}

static void notify_user(const char *title, const char *body, bool error) {
    if (!command_exists("notify-send")) return;
    pid_t p = fork();
    if (p == 0) {
        execlp("notify-send", "notify-send", "-u", error ? "critical" : "low", "-a", "ClipReg", title, body, (char *)NULL);
        _exit(127);
    }
}

static int autostart_main(int argc, char **argv) {
    if (!command_exists("systemctl")) {
        fprintf(stderr, "clipreg: systemctl not found\n");
        return 1;
    }
    if (argc != 3 || (!strcmp(argv[2], "help") || !strcmp(argv[2], "--help"))) {
        fprintf(stderr, "Usage: clipreg autostart on|off|status\n");
        return argc == 3 ? 0 : 2;
    }
    char out[1024];
    if (!strcmp(argv[2], "on")) {
        char *args[] = {"systemctl", "--user", "enable", "--now", "clipreg.service", NULL};
        int r = exec_capture(args, out, sizeof(out));
        if (r != 0) { fprintf(stderr, "clipreg: could not enable autostart: %s", out); return 1; }
        puts("ClipReg autostart: on (service enabled and started)");
        return 0;
    }
    if (!strcmp(argv[2], "off")) {
        char *args[] = {"systemctl", "--user", "disable", "--now", "clipreg.service", NULL};
        int r = exec_capture(args, out, sizeof(out));
        if (r != 0) { fprintf(stderr, "clipreg: could not disable autostart: %s", out); return 1; }
        puts("ClipReg autostart: off (service disabled and stopped; commands can still start it on demand)");
        return 0;
    }
    if (!strcmp(argv[2], "status")) {
        char enabled[256] = "unknown", active[256] = "unknown";
        char *e[] = {"systemctl", "--user", "is-enabled", "clipreg.service", NULL};
        char *a[] = {"systemctl", "--user", "is-active", "clipreg.service", NULL};
        int er = exec_capture(e, enabled, sizeof(enabled));
        int ar = exec_capture(a, active, sizeof(active));
        enabled[strcspn(enabled, "\r\n")] = 0;
        active[strcspn(active, "\r\n")] = 0;
        printf("autostart=%s\nservice=%s\n", er == 0 ? "on" : "off", active[0] ? active : (ar == 0 ? "active" : "inactive"));
        return 0;
    }
    fprintf(stderr, "clipreg: unknown autostart mode '%s' (expected on|off|status)\n", argv[2]);
    return 2;
}

static int client_main(int argc, char **argv) {
    bool notify = false; int ai = 1;
    if (ai < argc && !strcmp(argv[ai], "--notify")) { notify = true; ai++; }
    if (ai >= argc) {
        fprintf(stderr, "Usage: clipreg [--notify] grab|save|primary|paste|copy|show|clear|list|app|doctor|reload [REGISTER]\n       clipreg autostart on|off|status\n");
        return 2;
    }
    char req[512];
    if (ai + 1 < argc) snprintf(req, sizeof(req), "%s %s", argv[ai], argv[ai + 1]);
    else snprintf(req, sizeof(req), "%s", argv[ai]);
    char sp[PATH_MAX]; if (client_socket_path(sp, sizeof(sp)) < 0) return 1;
    int fd = connect_daemon_socket(sp);
    if (fd < 0 && (fd == -ENOENT || fd == -ECONNREFUSED)) {
        try_start_daemon_service();
        uint64_t deadline = now_ms() + 1500;
        do {
            fd = connect_daemon_socket(sp);
            if (fd >= 0) break;
            sleep_ms(25);
        } while (now_ms() < deadline);
    }
    if (fd < 0) {
        fprintf(stderr, "clipreg: daemon unavailable (%s). Check: systemctl --user status clipreg.service\n", strerror(-fd));
        return 1;
    }
    if (send(fd, req, strlen(req), 0) < 0) { perror("clipreg send"); close(fd); return 1; }
    char reply[MAX_REPLY + 64]; ssize_t n = recv(fd, reply, sizeof(reply) - 1, 0); close(fd);
    if (n <= 0) { fprintf(stderr, "clipreg: no response from daemon\n"); return 1; }
    reply[n] = 0;
    if (!strncmp(reply, "OK ", 3)) {
        const char *body = reply + 3; if (*body) printf("%s\n", body);
        if (notify && strcmp(argv[ai], "show") && strcmp(argv[ai], "list") && strcmp(argv[ai], "doctor") && strcmp(argv[ai], "app"))
            notify_user("ClipReg", body, false);
        return 0;
    }
    const char *body = !strncmp(reply, "ERR ", 4) ? reply + 4 : reply;
    fprintf(stderr, "clipreg: %s\n", body); if (notify) notify_user("ClipReg failed", body, true); return 1;
}

int main(int argc,char**argv){if(argc>=2&&!strcmp(argv[1],"daemon"))return daemon_main();if(argc>=2&&!strcmp(argv[1],"--selftest"))return selftest_main();if(argc>=2&&(!strcmp(argv[1],"--version")||!strcmp(argv[1],"version"))){puts(CLIPREG_VERSION);return 0;}if(argc>=2&&!strcmp(argv[1],"autostart"))return autostart_main(argc,argv);return client_main(argc,argv);}
