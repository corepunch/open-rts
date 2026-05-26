#include "plugin.h"

#include <string.h>

const RtsPlugin *open_rts_dark_reign_plugin(void);
const RtsPlugin *open_rts_dark_colony_plugin(void);

typedef const RtsPlugin *(*PluginFactory)(void);

static const PluginFactory PLUGIN_FACTORIES[] = {
    open_rts_dark_reign_plugin,
    open_rts_dark_colony_plugin,
};

static const RtsPlugin *plugin_by_index(int index) {
    if (index < 0 || index >= rts_plugin_count()) return NULL;
    return PLUGIN_FACTORIES[index]();
}

int rts_plugin_count(void) {
    return (int)(sizeof(PLUGIN_FACTORIES) / sizeof(PLUGIN_FACTORIES[0]));
}

const RtsPlugin *rts_plugin_at(int index) {
    if (index < 0 || index >= rts_plugin_count()) return NULL;
    return plugin_by_index(index);
}

const RtsPlugin *rts_find_plugin(const char *id) {
    for (int i = 0; i < rts_plugin_count(); ++i) {
        const RtsPlugin *plugin = rts_plugin_at(i);
        if (plugin && strcmp(plugin->id, id) == 0) return plugin;
    }
    return NULL;
}
