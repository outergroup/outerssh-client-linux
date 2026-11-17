#include "config.h"

#include <OuterSSH/OuterSSH.h>
#include <OuterSSH/SSHProxyAddress.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

enum { TAG_PROXY_STARTED = 1, TAG_HOMEPAGE, TAG_FIND_OUTER_SHELL, TAG_DESTROY };

static const char find_outer_shell[] =
    "if [ \"$(id -u)\" = 0 ]; then d=/run; else d=\"${XDG_RUNTIME_DIR:-/run/user/$(id -u)}\"; fi; "
    "p=\"$d/org.outershell.OuterShell\"; [ -S \"$p\" ] && printf '%s' \"$p\"";
static const char outer_shell_suffix[] = "/org.outershell.OuterShell";

typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GtkLabel *status;
    WebKitNetworkSession *network;
    WebKitWebView *view;
    OuterSSHEventQueue *events;
    guint events_watch;
    OuterSSHSessionPool *pool;
    OuterSSHHTTPProxyServer *proxy;
    guint16 proxy_port;
    char *proxy_username;
    char *proxy_password;
    char *pending_url;
    unsigned live_objects;
} App;

static void show_status(App *app, const char *message)
{
    gtk_label_set_text(app->status, message ? message : "");
    gtk_widget_set_visible(GTK_WIDGET(app->status), message != NULL);
}

static void show_error(App *app, const char *prefix, OuterSSHError error)
{
    char *message = g_strdup_printf("%s: %.*s", prefix, (int)error.message_length, error.message);
    show_status(app, message);
    g_free(message);
}

static void load(App *app, const char *url)
{
    show_status(app, NULL);
    if (!app->proxy_port) {
        g_free(app->pending_url);
        app->pending_url = g_strdup(url);
        return;
    }
    webkit_web_view_load_uri(app->view, url);
}

static void go_home(App *app)
{
    show_status(app, "Connecting…");
    if (!outerssh_session_pool_get_homepage(app->pool, TAG_HOMEPAGE))
        show_status(app, "Could not request the homepage.");
}

/* Turns a server homepage into a URL the web view can load through OuterSSH's
 * proxy. This starter supports http+unix sockets and http://localhost. */
static char *proxy_url_for_homepage(const char *url)
{
    static const char unix_scheme[] = "http+unix://";
    char host[OUTERSSH_PROXY_SOCKET_HOST_CAPACITY];
    size_t host_length;
    if (!g_ascii_strncasecmp(url, unix_scheme, sizeof(unix_scheme) - 1)) {
        const char *authority = url + sizeof(unix_scheme) - 1;
        size_t authority_length = strcspn(authority, "/?#");
        char *encoded = g_strndup(authority, authority_length);
        char *path = g_uri_unescape_string(encoded, NULL);
        g_free(encoded);
        gboolean ok = path && path[0] == '/' &&
            outerssh_proxy_encode_unix_socket_host(path, strlen(path), host, sizeof(host), &host_length);
        g_free(path);
        if (!ok) return NULL;
        const char *rest = authority + authority_length;
        return g_strconcat("http://", host, rest[0] == '/' ? "" : "/", rest, NULL);
    }
    GUri *uri = g_uri_parse(url, G_URI_FLAGS_ENCODED, NULL);
    if (!uri) return NULL;
    const char *uri_host = g_uri_get_host(uri);
    char *result = NULL;
    if (!g_strcmp0(g_uri_get_scheme(uri), "http") && !g_uri_get_userinfo(uri) && uri_host &&
        outerssh_proxy_encode_tcp_host(uri_host, strlen(uri_host), host, sizeof(host), &host_length)) {
        const char *path = g_uri_get_path(uri);
        result = g_uri_join(G_URI_FLAGS_ENCODED, "http", NULL, host, g_uri_get_port(uri),
            path[0] ? path : "/", g_uri_get_query(uri), g_uri_get_fragment(uri));
    }
    g_uri_unref(uri);
    return result;
}

static void homepage_received(App *app, const OuterSSHEvent *event)
{
    if (event->homepage_received.error.domain != OUTERSSH_ERROR_NONE) {
        show_error(app, "Could not connect", event->homepage_received.error);
        return;
    }
    if (!event->homepage_received.found) {
        if (!outerssh_session_pool_execute_command(app->pool, find_outer_shell, sizeof(find_outer_shell) - 1,
                NULL, 0, TAG_FIND_OUTER_SHELL))
            show_status(app, "Could not look for Outer Shell.");
        return;
    }
    char *url = g_strndup(event->homepage_received.url, event->homepage_received.url_length);
    char *proxy_url = proxy_url_for_homepage(url);
    if (proxy_url) {
        load(app, proxy_url);
    } else {
        char *message = g_strdup_printf("This starter doesn't support the homepage %s.", url);
        show_status(app, message);
        g_free(message);
    }
    g_free(proxy_url);
    g_free(url);
}

static void outer_shell_found(App *app, const OuterSSHEvent *event)
{
    if (event->command_completed.error.domain != OUTERSSH_ERROR_NONE) {
        show_error(app, "Could not look for Outer Shell", event->command_completed.error);
        return;
    }
    const char *path = (const char *)event->command_completed.standard_output;
    size_t length = event->command_completed.standard_output_length;
    char host[OUTERSSH_PROXY_SOCKET_HOST_CAPACITY];
    size_t host_length;
    if (event->command_completed.exit_status != 0 || length <= sizeof(outer_shell_suffix) - 1 ||
        path[0] != '/' || memcmp(path + length - (sizeof(outer_shell_suffix) - 1), outer_shell_suffix,
            sizeof(outer_shell_suffix) - 1) != 0 ||
        !outerssh_proxy_encode_unix_socket_host(path, length, host, sizeof(host), &host_length)) {
        show_status(app, "Outer Shell isn't running on this server.");
        return;
    }
    char *url = g_strconcat("http://", host, "/", NULL);
    load(app, url);
    g_free(url);
}

static void proxy_started(App *app, const OuterSSHEvent *event)
{
    if (event->proxy_started.error.domain != OUTERSSH_ERROR_NONE) {
        show_error(app, "Could not start the proxy", event->proxy_started.error);
        return;
    }
    app->proxy_port = event->proxy_started.listen_port;
    app->proxy_username = g_strndup(event->proxy_started.username, event->proxy_started.username_length);
    app->proxy_password = g_strndup(event->proxy_started.password, event->proxy_started.password_length);
    char *address = g_strdup_printf("http://127.0.0.1:%u", app->proxy_port);
    WebKitNetworkProxySettings *settings = webkit_network_proxy_settings_new(address, NULL);
    webkit_network_session_set_proxy_settings(app->network, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, settings);
    webkit_network_proxy_settings_free(settings);
    g_free(address);
    if (app->pending_url) {
        char *url = app->pending_url;
        app->pending_url = NULL;
        load(app, url);
        g_free(url);
    }
}

/* Answers OuterSSH's request for its server-side helper with a file built by
 * OuterSSH/Scripts/build_outerssh_linux_helpers.sh. */
static void provide_helper(App *app, const OuterSSHEvent *event)
{
    char *path = g_strdup_printf("%s/outerssh-helper-%.*s.gz", HELPER_DIRECTORY,
        (int)event->helper_binary_needed.platform_length, event->helper_binary_needed.platform);
    int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (file >= 0 && fstat(file, &status) == 0) {
        outerssh_session_pool_provide_helper_binary(app->pool, event->helper_binary_needed.question,
            file, 0, (uint64_t)status.st_size);
    } else {
        if (file >= 0) close(file);
        g_printerr("Missing %s; this server's sockets need it.\n", path);
        outerssh_session_pool_decline_helper_binary(app->pool, event->helper_binary_needed.question,
            (OuterSSHError){0});
    }
    g_free(path);
}

static void destroyed(App *app)
{
    if (--app->live_objects) return;
    outerssh_event_queue_destroy(app->events);
    gtk_window_destroy(app->window);
}

static void handle_event(App *app, const OuterSSHEvent *event)
{
    switch (event->type) {
    case OUTERSSH_EVENT_PROXY_STARTED:
        proxy_started(app, event);
        break;
    case OUTERSSH_EVENT_HOMEPAGE_RECEIVED:
        homepage_received(app, event);
        break;
    case OUTERSSH_EVENT_COMMAND_COMPLETED:
        if (event->tag == TAG_FIND_OUTER_SHELL) outer_shell_found(app, event);
        break;
    case OUTERSSH_EVENT_HOST_KEY_VALIDATION:
        g_print("Trusting host key %.*s. To pin it, set TRUSTED_HOST_KEY in config.h.\n",
            (int)event->host_key_validation.fingerprint_length, event->host_key_validation.fingerprint);
        outerssh_session_pool_answer_host_key(app->pool, event->host_key_validation.question, true);
        break;
    case OUTERSSH_EVENT_SUDO_PASSWORD_NEEDED:
        outerssh_session_pool_decline_sudo_password(app->pool, event->sudo_password_needed.question);
        break;
    case OUTERSSH_EVENT_HELPER_BINARY_NEEDED:
        provide_helper(app, event);
        break;
    case OUTERSSH_EVENT_CONNECTION_FAILED:
        show_error(app, "Connection failed", event->connection_failed.error);
        break;
    case OUTERSSH_EVENT_DESTROYED:
        destroyed(app);
        break;
    default:
        break;
    }
}

static gboolean events_ready(int fd, GIOCondition condition, void *context)
{
    (void)fd; (void)condition;
    App *app = context;
    OuterSSHEvent events[16];
    size_t count;
    while ((count = outerssh_event_queue_poll(app->events, events, G_N_ELEMENTS(events))) != 0) {
        for (size_t i = 0; i < count && app->live_objects; i++) handle_event(app, &events[i]);
        if (!app->live_objects) return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean authenticate_proxy(WebKitWebView *view, WebKitAuthenticationRequest *request, void *context)
{
    (void)view;
    App *app = context;
    if (!webkit_authentication_request_is_for_proxy(request) || !app->proxy_port) return FALSE;
    if (webkit_authentication_request_is_retry(request) ||
        webkit_authentication_request_get_port(request) != app->proxy_port) {
        webkit_authentication_request_cancel(request);
        return TRUE;
    }
    WebKitCredential *credential = webkit_credential_new(app->proxy_username, app->proxy_password,
        WEBKIT_CREDENTIAL_PERSISTENCE_NONE);
    webkit_authentication_request_set_can_save_credentials(request, FALSE);
    webkit_authentication_request_authenticate(request, credential);
    webkit_credential_free(credential);
    return TRUE;
}

static void port_entered(GtkEntry *entry, void *context)
{
    App *app = context;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    guint64 port;
    if (!*text) {
        go_home(app);
    } else if (g_ascii_string_to_unsigned(text, 10, 1, 65535, &port, NULL)) {
        char *url = g_strdup_printf("http://localhost" OUTERSSH_PROXY_TCP_HOST_SUFFIX ":%u/", (unsigned)port);
        load(app, url);
        g_free(url);
    } else {
        show_status(app, "Enter a port number from 1 to 65535.");
    }
}

static void home_clicked(GtkButton *button, void *context)
{
    (void)button;
    go_home(context);
}

static gboolean close_requested(GtkWindow *window, void *context)
{
    (void)window;
    App *app = context;
    if (!app->pool) return FALSE;
    gtk_widget_set_sensitive(GTK_WIDGET(app->window), FALSE);
    outerssh_http_proxy_server_destroy(app->proxy, TAG_DESTROY);
    outerssh_session_pool_destroy(app->pool, TAG_DESTROY);
    app->pool = NULL;
    return TRUE;
}

static gboolean connect_to_server(App *app)
{
    OuterSSHSessionAuthentication authentication = {.kind = OUTERSSH_SESSION_AUTHENTICATION_PASSWORD};
    char *key = NULL;
    gsize key_length = 0;
    const char *passphrase = PRIVATE_KEY_PASSPHRASE;
    const char *password = SERVER_PASSWORD;
    const char *path = PRIVATE_KEY_PATH;
    if (path) {
        GError *failure = NULL;
        if (!g_file_get_contents(path, &key, &key_length, &failure)) {
            show_status(app, failure->message);
            g_error_free(failure);
            return FALSE;
        }
        authentication = (OuterSSHSessionAuthentication){
            .kind = OUTERSSH_SESSION_AUTHENTICATION_PRIVATE_KEY,
            .private_key = key, .private_key_length = key_length,
            .passphrase = passphrase, .passphrase_length = passphrase ? strlen(passphrase) : 0,
        };
    } else if (password) {
        authentication.password = password;
        authentication.password_length = strlen(password);
    }
    OuterSSHSessionPoolConfiguration configuration = {
        .hostname = SERVER_HOST, .hostname_length = strlen(SERVER_HOST),
        .username = SERVER_USER, .username_length = strlen(SERVER_USER),
        .port = SERVER_PORT,
        .authentication = authentication,
        .trusted_host_key_fingerprint = TRUSTED_HOST_KEY[0] ? TRUSTED_HOST_KEY : NULL,
        .trusted_host_key_fingerprint_length = strlen(TRUSTED_HOST_KEY),
    };
    OuterSSHError error = {0};
    app->pool = outerssh_session_pool_create(app->events, &configuration, &error);
    if (key) {
        memset(key, 0, key_length);
        g_free(key);
    }
    if (!app->pool) {
        show_error(app, "Could not create the session", error);
        return FALSE;
    }
    OuterSSHHTTPProxyServerConfiguration proxy_configuration = {.rewrite_loopback_addresses = true};
    app->proxy = outerssh_http_proxy_server_create(app->pool, &proxy_configuration, &error);
    if (!app->proxy) {
        show_error(app, "Could not create the proxy", error);
        outerssh_session_pool_destroy(app->pool, TAG_DESTROY);
        app->pool = NULL;
        app->live_objects = 1;
        return FALSE;
    }
    app->live_objects = 2;
    outerssh_http_proxy_server_start(app->proxy, 0, TAG_PROXY_STARTED);
    go_home(app);
    return TRUE;
}

/* Pages served from a Unix socket see their address as a long encoded hostname.
 * OuterSSH's script shows the socket path in its place wherever the page
 * displays that hostname. */
static WebKitUserContentManager *create_content_manager(void)
{
    WebKitUserContentManager *manager = webkit_user_content_manager_new();
    char *source = NULL;
    GError *failure = NULL;
    if (!g_file_get_contents(SOCKET_HOST_DISPLAY_SCRIPT, &source, NULL, &failure)) {
        g_printerr("Could not load %s: %s\n", SOCKET_HOST_DISPLAY_SCRIPT, failure->message);
        g_error_free(failure);
        return manager;
    }
    const char *socket_origins[] = {
        "http://*.unix.outerssh.localhost/*", "https://*.unix.outerssh.localhost/*",
        "http://*.pipe.outerssh.localhost/*", "https://*.pipe.outerssh.localhost/*", NULL
    };
    WebKitUserScript *script = webkit_user_script_new(source, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, socket_origins, NULL);
    webkit_user_content_manager_add_script(manager, script);
    webkit_user_script_unref(script);
    g_free(source);
    return manager;
}

static void activate(GtkApplication *application, void *context)
{
    App *app = context;
    app->window = GTK_WINDOW(gtk_application_window_new(application));
    gtk_window_set_title(app->window, "OuterSSH Client");
    gtk_window_set_default_size(app->window, 1100, 760);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Port on the server, or empty for Home");
    gtk_widget_set_hexpand(entry, TRUE);
    GtkWidget *home = gtk_button_new_with_label("Home");
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(bar, 6);
    gtk_widget_set_margin_end(bar, 6);
    gtk_widget_set_margin_top(bar, 6);
    gtk_widget_set_margin_bottom(bar, 6);
    gtk_box_append(GTK_BOX(bar), home);
    gtk_box_append(GTK_BOX(bar), entry);

    app->status = GTK_LABEL(gtk_label_new(NULL));
    gtk_label_set_wrap(app->status, TRUE);
    gtk_widget_set_margin_bottom(GTK_WIDGET(app->status), 6);

    app->network = webkit_network_session_new_ephemeral();
    WebKitUserContentManager *scripts = create_content_manager();
    app->view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", app->network,
        "user-content-manager", scripts, NULL));
    g_object_unref(scripts);
    gtk_widget_set_vexpand(GTK_WIDGET(app->view), TRUE);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(content), bar);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(app->status));
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(app->view));
    gtk_window_set_child(app->window, content);

    g_signal_connect(entry, "activate", G_CALLBACK(port_entered), app);
    g_signal_connect(home, "clicked", G_CALLBACK(home_clicked), app);
    g_signal_connect(app->view, "authenticate", G_CALLBACK(authenticate_proxy), app);
    g_signal_connect(app->window, "close-request", G_CALLBACK(close_requested), app);

    app->events = outerssh_event_queue_create();
    if (!app->events || outerssh_event_queue_wait_handle(app->events) == OUTERSSH_EVENT_WAIT_HANDLE_INVALID) {
        show_status(app, "Could not create the OuterSSH event queue.");
    } else {
        app->events_watch = g_unix_fd_add(outerssh_event_queue_wait_handle(app->events), G_IO_IN, events_ready, app);
        if (!connect_to_server(app) && !app->live_objects) {
            g_source_remove(app->events_watch);
            outerssh_event_queue_destroy(app->events);
            app->events = NULL;
        }
    }
    gtk_window_present(app->window);
}

int main(int argc, char **argv)
{
    App app = {0};
    app.application = gtk_application_new("org.outerssh.ClientStarter", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app.application, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(app.application), argc, argv);
    g_object_unref(app.application);
    g_clear_object(&app.network);
    g_free(app.proxy_username);
    g_free(app.proxy_password);
    g_free(app.pending_url);
    return status;
}
