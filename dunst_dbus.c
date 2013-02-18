/* copyright 2013 Sascha Kruse and contributors (see LICENSE for licensing information) */

#include <glib.h>
#include <gio/gio.h>
#include "dunst.h"
#include "dunst_dbus.h"

GDBusConnection *dbus_conn;

static GDBusNodeInfo *introspection_data = NULL;

static const char *introspection_xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<node name=\"/org/freedesktop/Notifications\">"
    "    <interface name=\"org.freedesktop.Notifications\">"
    "        "
    "        <method name=\"GetCapabilities\">"
    "            <arg direction=\"out\" name=\"capabilities\" type=\"as\"/>"
    "        </method>"
    "        <method name=\"Notify\">"
    "            <arg direction=\"in\" name=\"app_name\" type=\"s\"/>"
    "            <arg direction=\"in\" name=\"replaces_id\" type=\"u\"/>"
    "            <arg direction=\"in\" name=\"app_icon\" type=\"s\"/>"
    "            <arg direction=\"in\" name=\"summary\" type=\"s\"/>"
    "            <arg direction=\"in\" name=\"body\" type=\"s\"/>"
    "            <arg direction=\"in\" name=\"actions\" type=\"as\"/>"
    "            <arg direction=\"in\" name=\"hints\" type=\"a{sv}\"/>"
    "            <arg direction=\"in\" name=\"expire_timeout\" type=\"i\"/>"
    "            <arg direction=\"out\" name=\"id\" type=\"u\"/>"
    "        </method>"
    "        "
    "        <method name=\"CloseNotification\">"
    "            <arg direction=\"in\" name=\"id\" type=\"u\"/>"
    "        </method>"
    "        <method name=\"GetServerInformation\">"
    "            <arg direction=\"out\" name=\"name\" type=\"s\"/>"
    "            <arg direction=\"out\" name=\"vendor\" type=\"s\"/>"
    "            <arg direction=\"out\" name=\"version\" type=\"s\"/>"
    "            <arg direction=\"out\" name=\"spec_version\" type=\"s\"/>"
    "        </method>"
    "        <signal name=\"NotificationClosed\">"
    "            <arg name=\"id\" type=\"u\"/>"
    "            <arg name=\"reason\" type=\"u\"/>"
    "        </signal>"
    "        <signal name=\"ActionInvoked\">"
    "            <arg name=\"id\" type=\"u\"/>"
    "            <arg name=\"action_key\" type=\"s\"/>"
    "        </signal>"
    "    </interface>"
    "    "
    "    <interface name=\"org.xfce.Notifyd\">"
    "        <method name=\"Quit\"/>" "    </interface>" "</node>";


static void onGetCapabilities(GDBusConnection *connection,
                const gchar *sender,
                const GVariant *parameters,
                GDBusMethodInvocation *invocation);
static void onNotify(GDBusConnection *connection,
                const gchar *sender,
                GVariant *parameters,
                GDBusMethodInvocation *invocation);
static void onCloseNotification(GDBusConnection *connection,
                const gchar *sender,
                GVariant *parameters,
                GDBusMethodInvocation *invocation);
static void onGetServerInformation(GDBusConnection *connection,
                const gchar *sender,
                const GVariant *parameters,
                GDBusMethodInvocation *invocation);

void handle_method_call(GDBusConnection *connection,
                const gchar *sender,
                const gchar *object_path,
                const gchar *interface_name,
                const gchar *method_name,
                GVariant *parameters,
                GDBusMethodInvocation *invocation,
                gpointer user_data)
{
        if (g_strcmp0(method_name, "GetCapabilities") == 0) {
                onGetCapabilities(connection, sender, parameters, invocation);
        }
        else if (g_strcmp0(method_name, "Notify") == 0) {
                onNotify(connection, sender, parameters, invocation);
        }
        else if (g_strcmp0(method_name, "CloseNotification") == 0) {
                onCloseNotification(connection, sender, parameters, invocation);
        }
        else if (g_strcmp0(method_name, "GetServerInformation") == 0) {
                onGetServerInformation(connection, sender, parameters, invocation);
        } else {
                g_object_unref(invocation);
                printf("WARNING: sender: %s; unknown method_name: %s\n", sender, method_name);
        }

}

static void onGetCapabilities(GDBusConnection *connection,
                const gchar *sender,
                const GVariant *parameters,
                GDBusMethodInvocation *invocation)
{
        GVariantBuilder *builder;
        GVariant *value;

        builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
        g_variant_builder_add (builder, "s", "actions");
        g_variant_builder_add (builder, "s", "body");
        value = g_variant_new ("as", builder);
        g_variant_builder_unref (builder);
        g_dbus_method_invocation_return_value(invocation, value);

        g_dbus_connection_flush(connection, NULL, NULL, NULL);
        g_variant_unref(value);
}

static void onNotify(GDBusConnection *connection,
                const gchar *sender,
                GVariant *parameters,
                GDBusMethodInvocation *invocation)
{

        gchar *appname = NULL;
        guint replaces_id = 0;
        gchar *icon = NULL;
        gchar *summary = NULL;
        gchar *body = NULL;
        Actions *actions = malloc(sizeof(actions));
        gint timeout = -1;

        /* hints */
        gint urgency = 0;
        gint progress = 0;
        gchar *fgcolor = NULL;
        gchar *bgcolor = NULL;

        actions->actions = NULL;
        actions->count = 0;

        {
                GVariantIter *iter = g_variant_iter_new(parameters);
                GVariant *content;
                GVariant *dict_value;
                int idx = 0;
                while ((content = g_variant_iter_next_value(iter))) {

                        switch (idx) {
                                case 0:
                                        appname = g_variant_dup_string(content, NULL);
                                        break;
                                case 1:
                                        replaces_id = g_variant_get_uint32(content);
                                        break;
                                case 2:
                                        icon = g_variant_dup_string(content, NULL);
                                        break;
                                case 3:
                                        summary = g_variant_dup_string(content, NULL);
                                        break;
                                case 4:
                                        body = g_variant_dup_string(content, NULL);
                                        break;
                                case 5:
                                        actions->actions = g_variant_dup_strv(content, &(actions->count));
                                        break;
                                case 6:
                                        dict_value = g_variant_lookup_value(content, "urgency", G_VARIANT_TYPE_BYTE);
                                        urgency = g_variant_get_byte(dict_value);
                                        dict_value = g_variant_lookup_value(content, "fgcolor", G_VARIANT_TYPE_STRING);
                                        fgcolor = g_variant_dup_string(dict_value, NULL);
                                        dict_value = g_variant_lookup_value(content, "bgcolor", G_VARIANT_TYPE_STRING);
                                        bgcolor = g_variant_dup_string(dict_value, NULL);
                                        break;
                                case 7:
                                        timeout = g_variant_get_int32(content);
                                        break;
                        }


                        idx++;
                }

                g_variant_iter_free(iter);
        }

        printf("appname: %s\n", appname);
        printf("replaces_id: %i\n", replaces_id);
        printf("icon: %s\n", icon);
        printf("summary: %s\n", summary);
        printf("body: %s\n", body);
        printf("timeout: %i\n", timeout);
        printf("nactions: %d\n", actions->count);

        fflush(stdout);

        if (timeout > 0) {
                /* do some rounding */
                timeout = (timeout + 500) / 1000;
                if (timeout < 1) {
                        timeout = 1;
                }
        }

        notification *n = malloc(sizeof (notification));
        n->appname = appname;
        n->summary = summary;
        n->body = body;
        n->icon = icon;
        n->timeout = timeout;
        n->progress = (progress < 0 || progress > 100) ? 0 : progress + 1;
        n->urgency = urgency;
        n->dbus_client = strdup(sender);
        n->actions = actions;

        for (int i = 0; i < ColLast; i++) {
                n->color_strings[i] = NULL;
        }
        n->color_strings[ColFG] = fgcolor;
        n->color_strings[ColBG] = bgcolor;

        int id = init_notification(n, replaces_id);

        GVariant *reply = g_variant_new ("(u)", id);
        g_dbus_method_invocation_return_value(invocation, reply);
        g_dbus_connection_flush(connection, NULL, NULL, NULL);

        g_variant_unref(reply);
        run(NULL);
}

static void onCloseNotification(GDBusConnection *connection,
                const gchar *sender,
                GVariant *parameters,
                GDBusMethodInvocation *invocation)
{
        guint32 id;
        g_variant_get(parameters, "(u)", &id);
        close_notification_by_id(id, 3);
}

static void onGetServerInformation(GDBusConnection *connection,
                const gchar *sender,
                const GVariant *parameters,
                GDBusMethodInvocation *invocation)
{
        GVariant *value;

        value = g_variant_new ("(ssss)", "dunst", "knopwob", VERSION, "2013");
        g_dbus_method_invocation_return_value(invocation, value);

        g_dbus_connection_flush(connection, NULL, NULL, NULL);
}

void notificationClosed(notification * n, int reason)
{
        GVariant *body = g_variant_new ("(uu)", n->id, reason);
        GError *err = NULL;

        g_dbus_connection_emit_signal(
                        dbus_conn,
                        n->dbus_client,
                        "/org/freedesktop/Notifications",
                        "org.freedesktop.Notifications",
                        "NotificationClosed",
                        body,
                        &err);

        if (err) {
                printf("notificationClosed ERROR\n");
        }

}

static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call
};

static void on_bus_acquired(GDBusConnection *connection,
                const gchar *name,
                gpointer user_data)
{
        guint registration_id;

        registration_id = g_dbus_connection_register_object( connection,
                        "/org/freedesktop/Notifications",
                        introspection_data->interfaces[0],
                        &interface_vtable,
                        NULL,
                        NULL,
                        NULL);

        if (! registration_id > 0) {
                fprintf(stderr, "Unable to register\n");
                exit(1);
        }
}

static void on_name_acquired(GDBusConnection *connection,
                const gchar *name,
                gpointer user_data)
{
        dbus_conn = connection;
}

static void on_name_lost(GDBusConnection *connection,
                const gchar *name,
                gpointer user_data)
{
        fprintf(stderr, "Name Lost\n");
        exit(1);
}

int initdbus(void)
{
        guint owner_id;

        g_type_init();

        introspection_data = g_dbus_node_info_new_for_xml(introspection_xml,
                        NULL);

        owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                        "org.freedesktop.Notifications",
                        G_BUS_NAME_OWNER_FLAGS_NONE,
                        on_bus_acquired,
                        on_name_acquired,
                        on_name_lost,
                        NULL,
                        NULL);

        return owner_id;
}
