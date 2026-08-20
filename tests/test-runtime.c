/* SPDX-License-Identifier: AGPL-3.0-only */
/* Runtime lifecycle regressions that need direct access to ClipReg internals. */
#define main clipreg_program_main
#include "../src/clipreg.c"
#undef main

static int fail(const char *message) {
    fprintf(stderr, "runtime lifecycle regression: %s\n", message);
    return 1;
}

static int test_grab_copy_policy(void) {
    struct daemon_state d = {0};
    default_config(&d.cfg);

    struct app_profile p = {0};
    snprintf(p.copy_chord, sizeof(p.copy_chord), "CTRL+SHIFT+C");

    snprintf(p.grab_strategy, sizeof(p.grab_strategy), "primary-first");
    struct grab_copy_plan plan = grab_copy_plan_for_profile(&d, &p);
    if (!plan.first_chord || strcmp(plan.first_chord, "CTRL+SHIFT+C") ||
        plan.fallback_chord != NULL || plan.first_timeout_ms != d.cfg.copy_timeout_ms)
        return fail("primary-first routed through the generic Copy probe");

    snprintf(p.grab_strategy, sizeof(p.grab_strategy), "copy");
    plan = grab_copy_plan_for_profile(&d, &p);
    if (!plan.first_chord || strcmp(plan.first_chord, d.cfg.safe_copy_chord) ||
        !plan.fallback_chord || strcmp(plan.fallback_chord, "CTRL+SHIFT+C") ||
        plan.first_timeout_ms != d.cfg.safe_probe_timeout_ms)
        return fail("ordinary copy strategy lost safe-probe then fallback ordering");

    snprintf(p.grab_strategy, sizeof(p.grab_strategy), "primary-only");
    plan = grab_copy_plan_for_profile(&d, &p);
    if (plan.first_chord || plan.fallback_chord)
        return fail("primary-only unexpectedly planned injected Copy input");

    return 0;
}

int main(void) {
    if (test_grab_copy_policy() != 0) return 1;

    struct daemon_state d = {0};
    default_config(&d.cfg);
    d.display = (struct wl_display *)(uintptr_t)1;
    d.clipboard_generation = 42;

    struct source_state owned = {0};
    owned.daemon = &d;
    owned.kind = SEL_CLIPBOARD;
    item_init(&owned.item);
    const char payload[] = "self-owned clipboard";
    if (item_add(&owned.item, "text/plain;charset=utf-8", payload, sizeof(payload) - 1U) < 0)
        return fail("could not construct owned item");
    d.active_clipboard_source = &owned;

    /* Model the offer that a real compositor reports back for our current
     * source. The stable snapshot must clone `owned.item`; trying to receive
     * this offer would require source_send() on the same blocked event loop. */
    struct offer_state offer = {0};
    char *mimes[] = {"text/plain;charset=utf-8"};
    offer.proxy = (struct ext_data_control_offer_v1 *)(uintptr_t)1;
    offer.mimes = mimes;
    offer.mime_count = 1;
    offer.assigned = true;
    d.clipboard_offer = &offer;

    struct item snap;
    bool present = false;
    uint64_t generation = 0;
    /* The offline Wayland stub reports flush failure but leaves errno alone.
     * Model the normal non-blocking EAGAIN path so drain_wayland() can run. */
    errno = EAGAIN;
    int r = stable_snapshot_current(&d, SEL_CLIPBOARD, &snap, &present, &generation);
    if (r < 0) {
        item_free(&owned.item);
        return fail("stable snapshot rejected a self-owned selection");
    }
    if (!present || generation != d.clipboard_generation || !item_equal(&owned.item, &snap)) {
        item_free(&snap);
        item_free(&owned.item);
        return fail("stable snapshot did not clone the self-owned selection exactly");
    }

    item_free(&snap);
    item_free(&owned.item);
    puts("runtime lifecycle regressions: PASS");
    return 0;
}
