#include "plugin.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define MAX_LOADED_PLUGINS 16

static const RtsPlugin *loaded_plugins[MAX_LOADED_PLUGINS];
static int loaded_count = 0;

int rts_plugin_count(void) {
    return loaded_count;
}

const RtsPlugin *rts_plugin_at(int index) {
    if (index < 0 || index >= loaded_count) return NULL;
    return loaded_plugins[index];
}

const RtsPlugin *rts_find_plugin(const char *id) {
    for (int i = 0; i < loaded_count; ++i) {
        if (loaded_plugins[i] && strcmp(loaded_plugins[i]->id, id) == 0)
            return loaded_plugins[i];
    }
    return NULL;
}

bool rts_plugin_load(const char *so_path) {
    if (loaded_count >= MAX_LOADED_PLUGINS) {
        fprintf(stderr, "plugin registry full; cannot load %s\n", so_path);
        return false;
    }
    void *handle = dlopen(so_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "dlopen %s: %s\n", so_path, dlerror());
        return false;
    }
    rts_plugin_entry_fn fn = (rts_plugin_entry_fn)dlsym(handle, "open_rts_plugin_entry");
    if (!fn) {
        fprintf(stderr, "dlsym open_rts_plugin_entry in %s: %s\n", so_path, dlerror());
        dlclose(handle);
        return false;
    }
    const RtsPlugin *plugin = fn();
    if (!plugin || !plugin->id) {
        fprintf(stderr, "%s: plugin entry returned NULL or missing id\n", so_path);
        dlclose(handle);
        return false;
    }
    if (rts_find_plugin(plugin->id)) {
        fprintf(stderr, "plugin '%s' already loaded; skipping %s\n", plugin->id, so_path);
        dlclose(handle);
        return true;
    }
    loaded_plugins[loaded_count++] = plugin;
    return true;
}
