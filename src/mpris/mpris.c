#include "mpris.h"
#include <gio/gio.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>

static GDBusConnection *connection = NULL;
static MPRISCallbacks _callbacks;
static PlaybackStatus current_status = MPRIS_STOPPED;
static LoopStatus current_loop = MPRIS_LOOP_NONE;
static bool current_shuffle = false;

static char *current_title = NULL;
static char *current_artist = NULL;
static char *current_album = NULL;
static char *current_art_url = NULL;
MPRISCallbacks global_callbacks = {0};

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.mpris.MediaPlayer2'>"
    "    <property name='Identity' type='s' access='read'/>"
    "    <property name='CanQuit' type='b' access='read'/>"
    "    <property name='CanRaise' type='b' access='read'/>"
    "    <property name='HasTrackList' type='b' access='read'/>"
    "  </interface>"
    "  <interface name='org.mpris.MediaPlayer2.Player'>"
    "    <method name='Next'/>"
    "    <method name='Previous'/>"
    "    <method name='Pause'/>"
    "    <method name='PlayPause'/>"
    "    <method name='Stop'/>"
    "    <method name='Play'/>"
    "    <method name='Seek'>"
    "      <arg direction='in' name='Offset' type='x'/>"
    "    </method>"
    "    <method name='SetPosition'>"
    "      <arg direction='in' name='TrackId' type='o'/>"
    "      <arg direction='in' name='Position' type='x'/>"
    "    </method>"
    "    <property name='PlaybackStatus' type='s' access='read'/>"
    "    <property name='LoopStatus' type='s' access='readwrite'/>"
    "    <property name='Shuffle' type='b' access='readwrite'/>"
    "    <property name='Metadata' type='a{sv}' access='read'/>"
    "    <property name='Position' type='x' access='read'/>"
    "    <property name='CanGoNext' type='b' access='read'/>"
    "    <property name='CanGoPrevious' type='b' access='read'/>"
    "    <property name='CanPlay' type='b' access='read'/>"
    "    <property name='CanPause' type='b' access='read'/>"
    "    <property name='CanSeek' type='b' access='read'/>"
    "    <property name='CanControl' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static GVariant* build_metadata_variant() {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "mpris:trackid", g_variant_new_object_path("/org/mpris/MediaPlayer2/Track/0"));

    if (current_title) g_variant_builder_add(&builder, "{sv}", "xesam:title", g_variant_new_string(current_title));
    if (current_album) g_variant_builder_add(&builder, "{sv}", "xesam:album", g_variant_new_string(current_album));
    if (current_artist) {
        const gchar *artists[] = { current_artist, NULL };
        g_variant_builder_add(&builder, "{sv}", "xesam:artist", g_variant_new_strv(artists, 1));
    }
    if (current_art_url) g_variant_builder_add(&builder, "{sv}", "mpris:artUrl", g_variant_new_string(current_art_url));

    // Duration in microseconds
    if (_callbacks.get_duration) {
        gint64 duration = (gint64)(_callbacks.get_duration() * 1000000);
        g_variant_builder_add(&builder, "{sv}", "mpris:length", g_variant_new_int64(duration));
    }

    return g_variant_builder_end(&builder);
}

static void handle_method_call(GDBusConnection *conn, const gchar *sender, const gchar *object_path,
                               const gchar *interface_name, const gchar *method_name,
                               GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data) {
    (void)conn; (void)sender; (void)object_path; (void)interface_name; (void)user_data;
    if (g_strcmp0(method_name, "Next") == 0) _callbacks.on_next();
    else if (g_strcmp0(method_name, "Previous") == 0) _callbacks.on_previous();
    else if (g_strcmp0(method_name, "Pause") == 0) _callbacks.on_pause();
    else if (g_strcmp0(method_name, "Play") == 0) _callbacks.on_play();
    else if (g_strcmp0(method_name, "PlayPause") == 0) _callbacks.on_play_pause();
    else if (g_strcmp0(method_name, "Stop") == 0) _callbacks.on_stop();
    else if (g_strcmp0(method_name, "Seek") == 0) {
        gint64 offset;
        g_variant_get(parameters, "(x)", &offset);
        if (_callbacks.on_seek) _callbacks.on_seek(offset);
    }
    else if (g_strcmp0(method_name, "SetPosition") == 0) {
        gchar *track_id = NULL;   // NOT const
        gint64 position;

        g_variant_get(parameters, "(ox)", &track_id, &position);

        if (_callbacks.on_set_position)
            _callbacks.on_set_position(track_id, position);

        g_free(track_id);
    }

    g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant* handle_get_property(GDBusConnection *conn, const gchar *sender, const gchar *object_path,
                                    const gchar *interface_name, const gchar *property_name,
                                    GError **error, gpointer user_data) {
    (void) conn; (void) sender; (void) object_path; (void) interface_name; (void) error; (void) user_data;
    if (g_strcmp0(property_name, "PlaybackStatus") == 0) {
        const char *st = (current_status == MPRIS_PLAYING) ? "Playing" : (current_status == MPRIS_PAUSED ? "Paused" : "Stopped");
        return g_variant_new_string(st);
    }
    if (g_strcmp0(property_name, "LoopStatus") == 0) {
        const char *ls = (current_loop == MPRIS_LOOP_TRACK) ? "Track" : (current_loop == MPRIS_LOOP_PLAYLIST ? "Playlist" : "None");
        return g_variant_new_string(ls);
    }
    if (g_strcmp0(property_name, "Shuffle") == 0) return g_variant_new_boolean(current_shuffle);
    if (g_strcmp0(property_name, "Position") == 0) {
        gint64 pos = (_callbacks.get_position) ? (gint64)(_callbacks.get_position() * 1000000) : 0;
        return g_variant_new_int64(pos);
    }
    if (g_strcmp0(property_name, "Metadata") == 0) return build_metadata_variant();
    if (g_strcmp0(property_name, "Identity") == 0) return g_variant_new_string("CMusicPlayer");
    if (g_str_has_prefix(property_name, "Can")) return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "HasTrackList") == 0) return g_variant_new_boolean(FALSE);

    return NULL;
}

static gboolean handle_set_property(GDBusConnection *conn, const gchar *sender, const gchar *object_path,
                                    const gchar *interface_name, const gchar *property_name,
                                    GVariant *value, GError **error, gpointer user_data) {
    (void) conn; (void) sender; (void) object_path; (void) interface_name; (void) error; (void) user_data;
    if (g_strcmp0(property_name, "LoopStatus") == 0) {
        const gchar *val = g_variant_get_string(value, NULL);
        LoopStatus status = MPRIS_LOOP_NONE;
        if (g_strcmp0(val, "Track") == 0) status = MPRIS_LOOP_TRACK;
        else if (g_strcmp0(val, "Playlist") == 0) status = MPRIS_LOOP_PLAYLIST;
        if (_callbacks.on_set_loop) _callbacks.on_set_loop(status);
        current_loop = status;
    } else if (g_strcmp0(property_name, "Shuffle") == 0) {
        current_shuffle = g_variant_get_boolean(value);
        if (_callbacks.on_set_shuffle) _callbacks.on_set_shuffle(current_shuffle);
    }
    return TRUE;
}

static const GDBusInterfaceVTable vtable = {
    .method_call = handle_method_call,
    .get_property = handle_get_property,
    .set_property = handle_set_property
};

// ... existing mpris_set_status, on_bus_acquired, etc ...
void mpris_set_status(PlaybackStatus status) {
   current_status = status;
   if (!connection) return;
   GVariantBuilder props;
   g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
   const char *st = (status == MPRIS_PLAYING) ? "Playing" : (status == MPRIS_PAUSED ? "Paused" : "Stopped");
   g_variant_builder_add(&props, "{sv}", "PlaybackStatus", g_variant_new_string(st));
   g_dbus_connection_emit_signal(connection, NULL, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged", g_variant_new("(sa{sv}as)", "org.mpris.MediaPlayer2.Player", &props, NULL), NULL);
}

void mpris_update_metadata(const char* title, const char* artist, const char* album, const char* art_url) {
    g_free(current_title); g_free(current_artist); g_free(current_album); g_free(current_art_url);
    current_title = g_strdup(title);
    current_artist = g_strdup(artist);
    current_album = g_strdup(album);
    current_art_url = g_strdup(art_url);

    if (!connection) return;
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "Metadata", build_metadata_variant());
    g_dbus_connection_emit_signal(connection, NULL, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged", g_variant_new("(sa{sv}as)", "org.mpris.MediaPlayer2.Player", &props, NULL), NULL);
}

static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data) {
   (void)user_data; (void) name;
   connection = conn;
   GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(introspection_xml, NULL);

   // Register both the base interface and the Player interface
   g_dbus_connection_register_object(conn, "/org/mpris/MediaPlayer2", info->interfaces[0], &vtable, NULL, NULL, NULL);
   g_dbus_connection_register_object(conn, "/org/mpris/MediaPlayer2", info->interfaces[1], &vtable, NULL, NULL, NULL);
   g_dbus_node_info_unref(info);
}


void mpris_init(MPRISCallbacks callbacks) {
   _callbacks = callbacks;
   g_bus_own_name(G_BUS_TYPE_SESSION, "org.mpris.MediaPlayer2.CMusicPlayer", G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, NULL, NULL, NULL, NULL);
}



void* mpris_thread_func(void* arg) {
   (void)arg;
   GMainContext *context = g_main_context_new();
   g_main_context_push_thread_default(context);
   mpris_init(global_callbacks);
   GMainLoop *loop = g_main_loop_new(context, FALSE);
   printf("INFO: MPRIS: Background thread started successfully.\n");
   g_main_loop_run(loop);

   // Cleanup if the loop ever exits

   g_main_loop_unref(loop);
   g_main_context_pop_thread_default(context);
   g_main_context_unref(context);
   return NULL;
}


void mpris_start_thread(MPRISCallbacks cbs) {
   global_callbacks = cbs; // Store cbs globally so the thread can see them
   pthread_t thread_id;
   if (pthread_create(&thread_id, NULL, mpris_thread_func, NULL) != 0) {
       printf("ERROR: MPRIS: Failed to create background thread.\n");
       return;
   }
   pthread_detach(thread_id);
}
