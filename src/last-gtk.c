#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <mcheck.h>
#include <stdio.h>

#include <gtk/gtk.h>

#include "headers/last.h"
#include "headers/last-gtk.h"
#include "headers/bind.h"
#include "components/last-component.h"
#include "headers/auto-splitter.h"
#include "headers/settings.h"
#include "headers/memcheck.h"

#define LAST_APP_TYPE (last_app_get_type ())
#define LAST_APP(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST                 \
     ((obj), LAST_APP_TYPE, LASTApp))

typedef struct _LASTApp       LASTApp;
typedef struct _LASTAppClass  LASTAppClass;

#define LAST_APP_WINDOW_TYPE (last_app_window_get_type ())
#define LAST_APP_WINDOW(obj)                             \
    (G_TYPE_CHECK_INSTANCE_CAST                         \
     ((obj), LAST_APP_WINDOW_TYPE, LASTAppWindow))

typedef struct _LASTAppWindow         LASTAppWindow;
typedef struct _LASTAppWindowClass    LASTAppWindowClass;

#define WINDOW_PAD (8)

typedef struct
{
    guint key;
    GdkModifierType mods;
} Keybind;

struct _LASTAppWindow
{
    GtkApplicationWindow parent;
    char data_path[256];
    int decorated;
    last_game *game;
    last_timer *timer;
    GdkDisplay *display;
    GtkWidget *box;
    GList *components;
    GtkWidget *footer;
    GtkWidget *menu;
    GtkCssProvider *style;
    gboolean hide_cursor;
    gboolean global_hotkeys;
    Keybind keybind_start_split;
    Keybind keybind_stop_reset;
    Keybind keybind_cancel;
    Keybind keybind_unsplit;
    Keybind keybind_skip_split;
    Keybind keybind_toggle_decorations;
};

struct _LASTAppWindowClass
{
    GtkApplicationWindowClass parent_class;
};

G_DEFINE_TYPE(LASTAppWindow, last_app_window, GTK_TYPE_APPLICATION_WINDOW);

static Keybind parse_keybind(const gchar *accelerator)
{
    Keybind kb;
    gtk_accelerator_parse(accelerator, &kb.key, &kb.mods);
    return kb;
}

static int keybind_match(Keybind kb, GdkEventKey key)
{
    return key.keyval == kb.key &&
        kb.mods == (key.state & gtk_accelerator_get_default_mod_mask());
}

static void last_app_window_destroy(GtkWidget *widget, gpointer data)
{
    LASTAppWindow *win = (LASTAppWindow*)widget;
    if (win->timer)
    {
        last_timer_release(win->timer);
    }
    if (win->game)
    {
        last_game_release(win->game);
    }
}

static gpointer save_game_thread(gpointer data)
{
    last_game *game = data;
    last_game_save(game);
    return NULL;
}

static void save_game(last_game *game)
{
    g_thread_new("save_game", save_game_thread, game);
}

static void last_app_window_clear_game(LASTAppWindow *win)
{
    GdkScreen *screen;
    GList *l;

    gtk_widget_hide(win->box);

    for (l = win->components; l != NULL; l = l->next)
    {
        LASTComponent *component = l->data;
        if (component->ops->clear_game)
        {
            component->ops->clear_game(component);
        }
    }

    // remove game's style
    if (win->style)
    {
        screen = gdk_display_get_default_screen(win->display);
        gtk_style_context_remove_provider_for_screen(
                screen, GTK_STYLE_PROVIDER(win->style));
        g_object_unref(win->style);
        win->style = NULL;
    }
}

// Forward declarations
static void timer_start(LASTAppWindow *win);
static void timer_stop(LASTAppWindow *win);
static void timer_split(LASTAppWindow *win);
static void timer_reset(LASTAppWindow *win);

static gboolean last_app_window_step(gpointer data)
{
    LASTAppWindow *win = data;
    long long now = last_time_now();
    static int set_cursor;
    if (win->hide_cursor && !set_cursor)
    {
        GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(win));
        if (gdk_window)
        {
            GdkCursor *cursor = gdk_cursor_new_for_display(win->display, GDK_BLANK_CURSOR);
            gdk_window_set_cursor(gdk_window, cursor);
            set_cursor = 1;
        }
    }
    if (win->timer)
    {
        last_timer_step(win->timer, now);
        
        if(atomic_load(&auto_splitter_enabled))
        {
            if (atomic_load(&call_start) && !win->timer->loading)
            {
                timer_start(win);
                atomic_store(&call_start, 0);
            }
            if (atomic_load(&call_split))
            {
                timer_split(win);
                atomic_store(&call_split, 0);
            }
            if (atomic_load(&toggle_loading))
            {
                win->timer->loading = !win->timer->loading;
                if (win->timer->running && win->timer->loading)
                {
                    timer_stop(win);
                }
                else if (win->timer->started && !win->timer->running && !win->timer->loading)
                {
                    timer_start(win);
                }
                atomic_store(&toggle_loading, 0);
            }
            if (atomic_load(&call_reset))
            {
                timer_reset(win);
                atomic_store(&call_reset, 0);
            }
        }
    }

    return TRUE;
}

static int last_app_window_find_theme(LASTAppWindow *win,
                                      const char *theme_name,
                                      const char *theme_variant,
                                      char *str)
{
    char theme_path[256];
    struct stat st = {0};
    if (!theme_name || !strlen(theme_name))
    {
        str[0] = '\0';
        return 0;
    }
    strcpy(theme_path, "/");
    strcat(theme_path, theme_name);
    strcat(theme_path, "/");
    strcat(theme_path, theme_name);
    if (theme_variant && strlen(theme_variant))
    {
        strcat(theme_path, "-");
        strcat(theme_path, theme_variant);
    }
    strcat(theme_path, ".css");

    strcpy(str, win->data_path);
    strcat(str, "/themes");
    strcat(str, theme_path);
    if (stat(str, &st) == -1)
    {
        strcpy(str, "/usr/share/last/themes");
        strcat(str, theme_path);
        if (stat(str, &st) == -1)
        {
            str[0] = '\0';
            return 0;
        }
    }
    return 1;
}

static void last_app_window_show_game(LASTAppWindow *win)
{
    GdkScreen *screen;
    char str[256];
    GList *l;

    // set dimensions
    if (win->game->width > 0 && win->game->height > 0)
    {
        gtk_widget_set_size_request(GTK_WIDGET(win),
                                    win->game->width,
                                    win->game->height);
    }

    // set game theme
    if (last_app_window_find_theme(win,
                                   win->game->theme,
                                   win->game->theme_variant,
                                   str))
    {
        win->style = gtk_css_provider_new();
        screen = gdk_display_get_default_screen(win->display);
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(win->style),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        gtk_css_provider_load_from_path(
            GTK_CSS_PROVIDER(win->style),
            str, NULL);
    }

    for (l = win->components; l != NULL; l = l->next)
    {
        LASTComponent *component = l->data;
        if (component->ops->show_game)
        {
            component->ops->show_game(component, win->game, win->timer);
        }
    }

    gtk_widget_show(win->box);
}

static void resize_window(LASTAppWindow *win,
                          int window_width,
                          int window_height)
{
    GList *l;
    for (l = win->components; l != NULL; l = l->next)
    {
        LASTComponent *component = l->data;
        if (component->ops->resize)
        {
            component->ops->resize(component,
                    window_width - 2 * WINDOW_PAD,
                    window_height);
        }
    }
}

static gboolean last_app_window_resize(GtkWidget *widget,
                                       GdkEvent *event,
                                       gpointer data)
{
    LASTAppWindow *win = (LASTAppWindow*)widget;
    resize_window(win, event->configure.width, event->configure.height);
    return FALSE;
}

static void timer_start_split(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        if (!win->timer->running)
        {
            if (last_timer_start(win->timer))
            {
                save_game(win->game);
            }
        }
        else
        {
            last_timer_split(win->timer);
        }
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->start_split)
            {
                component->ops->start_split(component, win->timer);
            }
        }
    }
}

static void timer_start(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        if (!win->timer->running)
        {
            if (last_timer_start(win->timer))
            {
                save_game(win->game);
            }
            for (l = win->components; l != NULL; l = l->next)
            {
                LASTComponent *component = l->data;
                if (component->ops->start_split)
                {    
                    component->ops->start_split(component, win->timer);
                }
            }
        }
    }
}

static void timer_split(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        if (win->timer->running)
        {
            last_timer_split(win->timer);
            for (l = win->components; l != NULL; l = l->next)
            {
                LASTComponent *component = l->data;
                if (component->ops->start_split)
                {
                    component->ops->start_split(component, win->timer);
                }
            }
        }
    }
}

static void timer_stop(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        if (win->timer->running)
        {
            last_timer_stop(win->timer);
        }
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->stop_reset)
            {
                component->ops->stop_reset(component, win->timer);
            }
        }
    }
}

static void timer_stop_reset(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        if (win->timer->running)
        {
            last_timer_stop(win->timer);
        }
        else
        {
            if (last_timer_reset(win->timer))
            {
                last_app_window_clear_game(win);
                last_app_window_show_game(win);
                save_game(win->game);
            }
        }
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->stop_reset)
            {
                component->ops->stop_reset(component, win->timer);
            }
        }
    }
}

static void timer_reset(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        if (win->timer->running)
        {
            last_timer_stop(win->timer);
            for (l = win->components; l != NULL; l = l->next)
            {
                LASTComponent *component = l->data;
                if (component->ops->stop_reset)
                {
                    component->ops->stop_reset(component, win->timer);
                }
            }
        }
        if (last_timer_reset(win->timer))
        {
            last_app_window_clear_game(win);
            last_app_window_show_game(win);
            save_game(win->game);
        }
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->stop_reset)
            {
                component->ops->stop_reset(component, win->timer);
            }
        }
    }
}

static void timer_cancel_run(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        if (last_timer_cancel(win->timer))
        {
            last_app_window_clear_game(win);
            last_app_window_show_game(win);
            save_game(win->game);
        }
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->cancel_run)
            {
                component->ops->cancel_run(component, win->timer);
            }
        }
    }
}


static void timer_skip(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        last_timer_skip(win->timer);
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->skip)
            {
                component->ops->skip(component, win->timer);
            }
        }
    }
}

static void timer_unsplit(LASTAppWindow *win)
{
    if (win->timer)
    {
        GList *l;
        last_timer_unsplit(win->timer);
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->unsplit)
            {
                component->ops->unsplit(component, win->timer);
            }
        }
    }
}


static void toggle_decorations(LASTAppWindow *win)
{
    gtk_window_set_decorated(GTK_WINDOW(win), !win->decorated);
    win->decorated = !win->decorated;
}

static void keybind_start_split(GtkWidget *widget, LASTAppWindow *win)
{
    timer_start_split(win);
}

static void keybind_stop_reset(const char *str, LASTAppWindow *win)
{
    timer_stop_reset(win);
}

static void keybind_cancel(const char *str, LASTAppWindow *win)
{
    timer_cancel_run(win);
}

static void keybind_skip(const char *str, LASTAppWindow *win)
{
    timer_skip(win);
}

static void keybind_unsplit(const char *str, LASTAppWindow *win)
{
    timer_unsplit(win);
}

static void keybind_toggle_decorations(const char *str, LASTAppWindow *win)
{
    toggle_decorations(win);
}

static gboolean last_app_window_keypress(GtkWidget *widget,
                                        GdkEvent *event,
                                        gpointer data)
{
    LASTAppWindow *win = (LASTAppWindow*)data;
    if (keybind_match(win->keybind_start_split, event->key))
    {    
        timer_start_split(win);
    }
    else if (keybind_match(win->keybind_stop_reset, event->key))
    {
        timer_stop_reset(win);
    }
    else if (keybind_match(win->keybind_cancel, event->key))
    {
        timer_cancel_run(win);
    }
    else if (keybind_match(win->keybind_unsplit, event->key))
    {
        timer_unsplit(win);
    }
    else if (keybind_match(win->keybind_skip_split, event->key))
    {
        timer_skip(win);
    }
    else if (keybind_match(win->keybind_toggle_decorations, event->key))
    {
        toggle_decorations(win);
    }
    return TRUE;
}

static gboolean last_app_window_draw(gpointer data)
{
    LASTAppWindow *win = data;
    if (win->timer)
    {
        GList *l;
        for (l = win->components; l != NULL; l = l->next)
        {
            LASTComponent *component = l->data;
            if (component->ops->draw)
            {
                component->ops->draw(component, win->game, win->timer);
            }
        }
    }
    else
    {
        GdkRectangle rect;
        gtk_widget_get_allocation(GTK_WIDGET(win), &rect);
        gdk_window_invalidate_rect(gtk_widget_get_window(GTK_WIDGET(win)),
                                   &rect, FALSE);
    }
    return TRUE;
}

static void last_app_window_init(LASTAppWindow *win)
{
    GtkCssProvider *provider;
    GdkScreen *screen;
    struct passwd *pw;
    char str[256];
    const char *theme;
    const char *theme_variant;
    int i;

    win->display = gdk_display_get_default();
    win->style = NULL;

    // make data path
    pw = getpwuid(getuid());
    strcpy(win->data_path, pw->pw_dir);
    strcat(win->data_path, "/.last");

    // load settings
    GSettings *settings = g_settings_new("wildmouse.last");
    win->hide_cursor = g_settings_get_boolean(settings, "hide-cursor");
    win->global_hotkeys = g_settings_get_boolean(settings, "global-hotkeys");
    win->keybind_start_split = parse_keybind(
	g_settings_get_string(settings, "keybind-start-split"));
    win->keybind_stop_reset = parse_keybind(
	g_settings_get_string(settings, "keybind-stop-reset"));
    win->keybind_cancel = parse_keybind(
	g_settings_get_string(settings, "keybind-cancel"));
    win->keybind_unsplit = parse_keybind(
	g_settings_get_string(settings, "keybind-unsplit"));
    win->keybind_skip_split = parse_keybind(
	g_settings_get_string(settings, "keybind-skip-split"));
    win->keybind_toggle_decorations = parse_keybind(
	g_settings_get_string(settings, "keybind-toggle-decorations"));
    win->decorated = g_settings_get_boolean(settings, "start-decorated");
    gtk_window_set_decorated(GTK_WINDOW(win), win->decorated);

    // Load CSS defaults
    provider = gtk_css_provider_new();
    screen = gdk_display_get_default_screen(win->display);
    gtk_style_context_add_provider_for_screen(
        screen,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_css_provider_load_from_data(
        GTK_CSS_PROVIDER(provider),
        (char *)last_gtk_css, last_gtk_css_len, NULL);
    g_object_unref(provider);

    // Load theme
    theme = g_settings_get_string(settings, "theme");
    theme_variant = g_settings_get_string(settings, "theme-variant");
    if (last_app_window_find_theme(win, theme, theme_variant, str))
    {
        provider = gtk_css_provider_new();
        screen = gdk_display_get_default_screen(win->display);
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        gtk_css_provider_load_from_path(
            GTK_CSS_PROVIDER(provider),
            str, NULL);
        g_object_unref(provider);
    }

    // Load window junk
    add_class(GTK_WIDGET(win), "window");
    win->game = 0;
    win->timer = 0;

    g_signal_connect(win, "destroy",
                     G_CALLBACK(last_app_window_destroy), NULL);
    g_signal_connect(win, "configure-event",
                     G_CALLBACK(last_app_window_resize), win);

    if (win->global_hotkeys)
    {
        keybinder_init();
        keybinder_bind(
            g_settings_get_string(settings, "keybind-start-split"),
            (KeybinderHandler)keybind_start_split,
            win);
        keybinder_bind(
            g_settings_get_string(settings, "keybind-stop-reset"),
            (KeybinderHandler)keybind_stop_reset,
            win);
        keybinder_bind(
            g_settings_get_string(settings, "keybind-cancel"),
            (KeybinderHandler)keybind_cancel,
            win);
        keybinder_bind(
            g_settings_get_string(settings, "keybind-unsplit"),
            (KeybinderHandler)keybind_unsplit,
            win);
        keybinder_bind(
            g_settings_get_string(settings, "keybind-skip-split"),
            (KeybinderHandler)keybind_skip,
            win);
        keybinder_bind(
            g_settings_get_string(settings, "keybind-toggle-decorations"),
            (KeybinderHandler)keybind_toggle_decorations,
            win);
    }
    else
    {
        g_signal_connect(win, "key_press_event",
                         G_CALLBACK(last_app_window_keypress), win);
    }

    win->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(win->box, WINDOW_PAD);
    gtk_widget_set_margin_bottom(win->box, WINDOW_PAD);
    gtk_widget_set_vexpand(win->box, TRUE);
    gtk_container_add(GTK_CONTAINER(win), win->box);

    // Create all available components (TODO: change this in the future)
    win->components = NULL;
    for (i = 0; last_components[i].name != NULL; i++)
    {
        LASTComponent *component = last_components[i].new();
        if (component)
        {
            GtkWidget *widget = component->ops->widget(component);
            if (widget)
            {
                gtk_widget_set_margin_start(widget, WINDOW_PAD);
                gtk_widget_set_margin_end(widget, WINDOW_PAD);
                gtk_container_add(GTK_CONTAINER(win->box),
                        component->ops->widget(component));
            }
            win->components = g_list_append(win->components, component);
        }
    }

    win->footer = gtk_grid_new();
    add_class(win->footer, "footer");
    gtk_widget_set_margin_start(win->footer, WINDOW_PAD);
    gtk_widget_set_margin_end(win->footer, WINDOW_PAD);
    gtk_container_add(GTK_CONTAINER(win->box), win->footer);
    gtk_widget_show(win->footer);

    g_timeout_add(1, last_app_window_step, win);
    g_timeout_add((int)(1000 / 30.), last_app_window_draw, win); 
}

static void last_app_window_class_init(LASTAppWindowClass *class)
{
}

static LASTAppWindow *last_app_window_new(LASTApp *app)
{
    LASTAppWindow *win;
    win = g_object_new(LAST_APP_WINDOW_TYPE, "application", app, NULL);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DIALOG);
    return win;
}

static void last_app_window_open(LASTAppWindow *win, const char *file)
{
    if (win->timer)
    {
        last_app_window_clear_game(win);
        last_timer_release(win->timer);
        win->timer = 0;
    }
    if (win->game)
    {
        last_game_release(win->game);
        win->game = 0;
    }
    if (last_game_create(&win->game, file))
    {
        win->game = 0;
    }
    else if (last_timer_create(&win->timer, win->game))
    {
        win->timer = 0;
    }
    else
    {
        last_app_window_show_game(win);
    }
}

struct _LASTApp
{
    GtkApplication parent;
};

struct _LASTAppClass
{
    GtkApplicationClass parent_class;
};

G_DEFINE_TYPE(LASTApp, last_app, GTK_TYPE_APPLICATION);

static void open_activated(GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       app)
{
    char splits_path[256];
    GList *windows;
    LASTAppWindow *win;
    GtkWidget *dialog;
    struct stat st = {0};
    gint res;
    if (app == 1)
    {
        app = parameter;
    }

    windows = gtk_application_get_windows(GTK_APPLICATION(app));
    if (windows)
    {
        win = LAST_APP_WINDOW(windows->data);
    }
    else
    {
        win = last_app_window_new(LAST_APP(app));
    }
    dialog = gtk_file_chooser_dialog_new (
        "Open File", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    strcpy(splits_path, win->data_path);
    strcat(splits_path, "/splits");
    if (stat(splits_path, &st) == -1)
    {
        mkdir(splits_path, 0700);
    }
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                        splits_path);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT)
    {
        char *filename;
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        filename = gtk_file_chooser_get_filename(chooser);
        last_app_window_open(win, filename);
        last_update_setting("split_file", json_string(filename));
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void open_auto_splitter(GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       app)
{
    char auto_splitters_path[256];
    GList *windows;
    LASTAppWindow *win;
    GtkWidget *dialog;
    struct stat st = {0};
    gint res;
    if (app == 1)
    {
        app = parameter;
    }

    windows = gtk_application_get_windows(GTK_APPLICATION(app));
    if (windows)
    {
        win = LAST_APP_WINDOW(windows->data);
    }
    else
    {
        win = last_app_window_new(LAST_APP(app));
    }
    dialog = gtk_file_chooser_dialog_new (
        "Open File", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    strcpy(auto_splitters_path, win->data_path);
    strcat(auto_splitters_path, "/auto-splitters");
    if (stat(auto_splitters_path, &st) == -1)
    {
        mkdir(auto_splitters_path, 0700);
    }
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                        auto_splitters_path);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT)
    {
        char *filename;
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        filename = gtk_file_chooser_get_filename(chooser);
        strcpy(auto_splitter_file, filename);
        last_update_setting("auto_splitter_file", json_string(filename));
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void save_activated(GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       app)
{
    GList *windows;
    LASTAppWindow *win;
    if (app == 1)
    {
        app = parameter;
    }

    windows = gtk_application_get_windows(GTK_APPLICATION(app));
    if (windows)
    {
        win = LAST_APP_WINDOW(windows->data);
    }
    else
    {
        win = last_app_window_new(LAST_APP(app));
    }
    if (win->game && win->timer)
    {
        int width, height;
        gtk_window_get_size(GTK_WINDOW(win), &width, &height);
        win->game->width = width;
        win->game->height = height;
        last_game_update_splits(win->game, win->timer);
        save_game(win->game);
    }
}

static void reload_activated(GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       app)
{
    GList *windows;
    LASTAppWindow *win;
    char *path;
    if (app == 1)
    {
        app = parameter;
    }

    windows = gtk_application_get_windows(GTK_APPLICATION(app));
    if (windows)
    {
        win = LAST_APP_WINDOW(windows->data);
    }
    else
    {
        win = last_app_window_new(LAST_APP(app));
    }
    if (win->game)
    {
        path = strdup(win->game->path);
        last_app_window_open(win, path);
        tracked_free(path);
    }
}

static void close_activated(GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       app)
{
    GList *windows;
    LASTAppWindow *win;
    if (app == 1)
    {
        app = parameter;
    }
    
    windows = gtk_application_get_windows(GTK_APPLICATION(app));
    if (windows)
    {
        win = LAST_APP_WINDOW(windows->data);
    }
    else
    {
        win = last_app_window_new(LAST_APP(app));
    }
    if (win->game && win->timer)
    {
        last_app_window_clear_game(win);
    }
    if (win->timer)
    {
        last_timer_release(win->timer);
        win->timer = 0;
    }
    if (win->game)
    {
        last_game_release(win->game);
        win->game = 0;
    }
    gtk_widget_set_size_request(GTK_WIDGET(win), -1, -1);
}

static void quit_activated(GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       app)
{
    muntrace();

    int num_leaks = alloc_count - free_count;

    if(num_leaks < 0)
        num_leaks = 0;

    fprintf(logFile, "Number of memory leaks: %d\n", num_leaks);
    fclose(logFile);

    exit(0);
}

static void toggle_auto_splitter(GtkCheckMenuItem *menu_item, gpointer user_data)
{
    gboolean active = gtk_check_menu_item_get_active(menu_item);
    if (active)
    {
        atomic_store(&auto_splitter_enabled, 1);
        last_update_setting("auto_splitter_enabled", json_true());
    }
    else
    {
        atomic_store(&auto_splitter_enabled, 0);
        last_update_setting("auto_splitter_enabled", json_false());
    }
}

static gboolean button_right_click(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (event->button == GDK_BUTTON_SECONDARY)
    {
        LASTAppWindow *win = (LASTAppWindow*)widget;
        win->menu = GTK_WIDGET(data);
        gtk_widget_show_all(win->menu);
        gtk_menu_popup_at_pointer(GTK_MENU(win->menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

static void create_context_menu(LASTAppWindow *win, GApplication *app)
{
    win->menu = gtk_menu_new();
    GtkWidget *menu_open_splits = gtk_menu_item_new_with_label("Open Splits");
    GtkWidget *menu_save_splits = gtk_menu_item_new_with_label("Save Splits");
    GtkWidget *menu_open_auto_splitter = gtk_menu_item_new_with_label("Open Auto Splitter");
    GtkWidget *menu_enable_auto_splitter = gtk_check_menu_item_new_with_label("Enable Auto Splitter");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_enable_auto_splitter), atomic_load(&auto_splitter_enabled));
    GtkWidget *menu_reload = gtk_menu_item_new_with_label("Reload");
    GtkWidget *menu_close = gtk_menu_item_new_with_label("Close");
    GtkWidget *menu_quit = gtk_menu_item_new_with_label("Quit");

    // Attach the callback functions to the menu items
    g_signal_connect(menu_open_splits, "activate", G_CALLBACK(open_activated), app);
    g_signal_connect(menu_save_splits, "activate", G_CALLBACK(save_activated), app);
    g_signal_connect(menu_open_auto_splitter, "activate", G_CALLBACK(open_auto_splitter), app);
    g_signal_connect(menu_enable_auto_splitter, "toggled", G_CALLBACK(toggle_auto_splitter), NULL);
    g_signal_connect(menu_reload, "activate", G_CALLBACK(reload_activated), app);
    g_signal_connect(menu_close, "activate", G_CALLBACK(close_activated), app);
    g_signal_connect(menu_quit, "activate", G_CALLBACK(quit_activated), app);

    // Add the menu items to the menu
    gtk_menu_shell_append(GTK_MENU_SHELL(win->menu), menu_open_splits);
    gtk_menu_shell_append(GTK_MENU_SHELL(win->menu), menu_save_splits);
    gtk_menu_shell_append(GTK_MENU_SHELL(win->menu), menu_open_auto_splitter);
    gtk_menu_shell_append(GTK_MENU_SHELL(win->menu), menu_enable_auto_splitter);
    gtk_menu_shell_append(GTK_MENU_SHELL(win->menu), menu_reload);
    gtk_menu_shell_append(GTK_MENU_SHELL(win->menu), menu_close);
    gtk_menu_shell_append(GTK_MENU_SHELL(win->menu), menu_quit);

    // Attach the menu to the window
    gtk_widget_add_events(win, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(win, "button_press_event", G_CALLBACK(button_right_click), win->menu);
}

static void last_app_activate(GApplication *app)
{
    LASTAppWindow *win;
    win = last_app_window_new(LAST_APP(app));
    gtk_window_present(GTK_WINDOW(win));
    if (get_setting_value("LAST", "split_file") != NULL)
    {
        last_app_window_open(win, json_string_value(get_setting_value("LAST", "split_file")));
    }
    if (get_setting_value("LAST", "auto_splitter_file") != NULL)
    {
        strcpy(auto_splitter_file, json_string_value(get_setting_value("LAST", "auto_splitter_file")));
    }
    if (get_setting_value("LAST", "auto_splitter_enabled") != NULL)
    {
        if (json_is_true(get_setting_value("LAST", "auto_splitter_enabled")))
        {
            atomic_store(&auto_splitter_enabled, 1);
        }
        else
        {
            atomic_store(&auto_splitter_enabled, 0);
        }
    }
    create_context_menu(win, app);
}

static void last_app_init(LASTApp *app)
{
}

static void last_app_open(GApplication  *app,
                          GFile        **files,
                          gint           n_files,
                          const gchar   *hint)
{
    GList *windows;
    LASTAppWindow *win;
    int i;
    windows = gtk_application_get_windows(GTK_APPLICATION(app));
    if (windows)
    {
        win = LAST_APP_WINDOW(windows->data);
    }
    else
    {
        win = last_app_window_new(LAST_APP(app));
    }
    for (i = 0; i < n_files; i++)
    {
        last_app_window_open(win, g_file_get_path(files[i]));
    }
    gtk_window_present(GTK_WINDOW(win));
}

LASTApp *last_app_new(void)
{
    g_set_application_name("last");
    return g_object_new(LAST_APP_TYPE,
                        "application-id", "wildmouse.last",
                        "flags", G_APPLICATION_HANDLES_OPEN,
                        NULL);
}

static void last_app_class_init(LASTAppClass *class)
{
    G_APPLICATION_CLASS(class)->activate = last_app_activate;
    G_APPLICATION_CLASS(class)->open = last_app_open;
}

struct ThreadArgs {
    int argc;
    char** argv;
};

void* run_application(void* args) {
    struct ThreadArgs* threadArgs = (struct ThreadArgs*)args;
    int argc = threadArgs->argc;
    char** argv = threadArgs->argv;
    g_application_run(G_APPLICATION(last_app_new()), argc, argv);
    return NULL;
}

void lastLog()
{
    char* username = getlogin();
    sprintf(logFilepath, "/home/%s/.last/last.log", username);
    logFile = fopen(logFilepath, "w");
}

int main(int argc, char *argv[]) {
    mtrace();
    lastLog();
    check_directories();

    pthread_t t1, t2;
    struct ThreadArgs threadArgs;
    threadArgs.argc = argc;
    threadArgs.argv = argv;
    pthread_create(&t1, NULL, &run_application, (void*)&threadArgs);
    pthread_create(&t2, NULL, &last_auto_splitter, NULL);

    pthread_join(t1, t2);

    return 0;
}