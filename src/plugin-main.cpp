#include "social-comments-dock.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-social-comments", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Facebook and YouTube live comments dock + clean browser overlay for OBS Studio (v0.4)";
}

static SocialCommentsDock *g_dock = nullptr;

bool obs_module_load(void)
{
    g_dock = new SocialCommentsDock();
    if (!obs_frontend_add_dock_by_id("social-comments-dock", "Social Comments", g_dock)) {
        delete g_dock;
        g_dock = nullptr;
        blog(LOG_ERROR, "[Social Comments] Could not add dock");
        return false;
    }

    blog(LOG_INFO, "[Social Comments] Loaded");
    return true;
}

void obs_module_unload(void)
{
    if (g_dock) {
        // OBS owns the QDockWidget wrapper. Removing the dock destroys the
        // wrapper and its child widget, so do not delete g_dock again.
        obs_frontend_remove_dock("social-comments-dock");
        g_dock = nullptr;
    }
    blog(LOG_INFO, "[Social Comments] Unloaded");
}
