/* vim: set expandtab ts=4 sw=4: */
/*
 *
 *  Copyright © 2006 Juha Kautto <juha@xfce.org>
 *
 *  it under the terms of the GNU Library General Public License as published 
 *  by the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Authors:
 *      Juha Kautto <juha@xfce.org>
 *      Based on XFce panel plugin clock and date-time plugin
 */

#include <config.h>
#include <sys/stat.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkevents.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include "orageclock.h"

/* -------------------------------------------------------------------- *
 *                               Clock                                  *
 * -------------------------------------------------------------------- */


static gboolean oc_date_tooltip(Clock *clock)
{
    char date_s[255];
    char *utf8date = NULL;

    /* TRANSLATORS: Use format characters from strftime(3)
     * to get the proper string for your locale.
     * I used these:
     * %A  : full weekday name
     * %d  : day of the month
     * %B  : full month name
     * %Y  : four digit year
     * %V  : ISO week number
     */
    strftime(date_s, 255, _("%A %d %B %Y/%V"), &clock->now);

    /* Conversion to utf8 */
    if (!g_utf8_validate(date_s, -1, NULL)) {
        utf8date = g_locale_to_utf8(date_s, -1, NULL, NULL, NULL);
    }

    if (utf8date) {
        gtk_tooltips_set_tip(clock->tips, GTK_WIDGET(clock->plugin), 
                utf8date, NULL);
        g_free(utf8date);
    }
    else {
        gtk_tooltips_set_tip(clock->tips, GTK_WIDGET(clock->plugin), 
                date_s, NULL);
    }

    return(TRUE);
}

static gboolean oc_get_time(Clock *clock)
{
    time_t  t;
    char    tmp[OC_MAX_LINE_LENGTH];
    int     i;
    static gint mday = -1;

    time(&t);
    localtime_r(&t, &clock->now);

    for (i = 0; i < OC_MAX_LINES; i++) {
        if (clock->line[i].show) {
            strftime(tmp, OC_MAX_LINE_LENGTH-1, clock->line[i].data->str
                    , &clock->now);
            /* gtk_label_set_text call takes almost
             * 100 % of the time wasted in this procedure 
             * */
            if (strcmp(tmp,  clock->line[i].prev)) {
                gtk_label_set_text(GTK_LABEL(clock->line[i].label), tmp);
                strcpy(clock->line[i].prev, tmp);
            }
        }
    }

    if (mday != clock->now.tm_mday) {
        oc_date_tooltip(clock);
        mday = clock->now.tm_mday;
    }

    return(TRUE);
}

static void oc_update_size(Clock *clock, int size)
{
    /* keep in sync with systray */
    if (size > 26) {
        gtk_container_set_border_width(GTK_CONTAINER(clock->frame), 2);
        size -= 3;
    }
    else {
        gtk_container_set_border_width(GTK_CONTAINER(clock->frame), 0);
        size -= 1;
    }
}

static gboolean popup_program(GtkWidget *widget, gchar *program, Clock *clock)
{
    GdkAtom atom;
    Window xwindow;
    GError *error = NULL;
    GdkEventClient gev;
    gchar *check, *popup; /* atom names to use */

    if (strcmp(program, "orage") == 0) {
        check = "_XFCE_CALENDAR_RUNNING";
        popup = "_XFCE_CALENDAR_TOGGLE_HERE";
    }
    else if (strcmp(program, "globaltime") == 0) {
        check = "_XFCE_GLOBALTIME_RUNNING";
        popup = "_XFCE_GLOBALTIME_TOGGLE_HERE";
    }
    else {
        g_warning("%s: unknown program to start %s", OC_NAME, program);
        return(FALSE);
    }

    /* send message to program to check if it is running */
    atom = gdk_atom_intern(check, FALSE);
    if ((xwindow = XGetSelectionOwner(GDK_DISPLAY(),
            gdk_x11_atom_to_xatom(atom))) != None) { /* yes, then toggle */
        gev.type = GDK_CLIENT_EVENT;
        gev.window = widget->window;
        gev.send_event = TRUE;
        gev.message_type = gdk_atom_intern(popup, FALSE);
        gev.data_format = 8;

        if (!gdk_event_send_client_message((GdkEvent *) &gev,
                (GdkNativeWindow)xwindow)) 
             g_message("%s: send message to %s failed", OC_NAME, program);
        gdk_flush();

        return(TRUE);
    }
    else { /* not running, let's try to start it. Need to reset TZ! */
        if (clock->TZ_orig != NULL)  /* we had TZ when we started */
            g_setenv("TZ", clock->TZ_orig, 1);
        else  /* TZ was not set so take it out */
            g_unsetenv("TZ");
        tzset();

        if (!xfce_exec(program, FALSE, FALSE, &error)) 
            g_message("%s: start of %s failed", OC_NAME, program);

        if ((clock->timezone->str != NULL) && (clock->timezone->len > 0)) {
        /* user has set timezone, so let's set TZ */
            g_setenv("TZ", clock->timezone->str, 1);
            tzset();
        }

        return(TRUE);
    }

    return(FALSE);
}

static gboolean on_button_press_event_cb(GtkWidget *widget
        , GdkEventButton *event, Clock *clock)
{
    if (event->button == 1)
        return(popup_program(widget, "orage", clock));
    else if (event->button == 2)
        return(popup_program(widget, "globaltime", clock));

    return(FALSE);
}


/* -------------------------------------------------------------------- *
 *                     Panel Plugin Interface                           *
 * -------------------------------------------------------------------- */


/* Interface Implementation */

static gboolean oc_set_size(XfcePanelPlugin *plugin, int size, Clock *clock)
{
    oc_update_size(clock, size);

    return(TRUE);
}

static void oc_free_data(XfcePanelPlugin *plugin, Clock *clock)
{
    GtkWidget *dlg = g_object_get_data(G_OBJECT(plugin), "dialog");

    if (dlg)
        gtk_widget_destroy(dlg);
    
    g_source_remove(clock->timeout_id);
    g_object_unref(clock->tips);
    g_object_unref(clock->line[0].label);
    g_object_unref(clock->line[1].label);
    g_object_unref(clock->line[2].label);
    g_free(clock->TZ_orig);
    g_free(clock);
}

static void oc_read_rc_file(XfcePanelPlugin *plugin, Clock *clock)
{
    gchar  *file;
    XfceRc *rc;
    const gchar  *ret;
    gchar tmp[100];
    gint i;

    if (!(file = xfce_panel_plugin_lookup_rc_file(plugin))) {
        g_warning("%s: unable to read rc file", OC_NAME);
        return;
    }
    if (!(rc = xfce_rc_simple_open(file, TRUE))) {
        g_warning("%s: unable to read-open rc file (%s)", OC_NAME, file);
        return;
    }
    g_free(file);

    clock->show_frame = xfce_rc_read_bool_entry(rc, "show_frame", TRUE);

    clock->fg_set = xfce_rc_read_bool_entry(rc, "fg_set", TRUE);
    if (clock->fg_set) {
        ret = xfce_rc_read_entry(rc, "fg", NULL);
        sscanf(ret, "%uR %uG %uB"
                , &clock->fg.red, &clock->fg.green, &clock->fg.blue);
        clock->fg.pixel = 0;
    }

    clock->bg_set = xfce_rc_read_bool_entry(rc, "bg_set", TRUE);
    if (clock->bg_set) {
        ret = xfce_rc_read_entry(rc, "bg", NULL);
        sscanf(ret, "%uR %uG %uB"
                , &clock->bg.red, &clock->bg.green, &clock->bg.blue);
        clock->bg.pixel = 0;
    }

    ret = xfce_rc_read_entry(rc, "timezone", NULL);
    g_string_assign(clock->timezone, ret); 
    
    for (i = 0; i < OC_MAX_LINES; i++) {
        sprintf(tmp, "show%d", i);
        clock->line[i].show = xfce_rc_read_bool_entry(rc, tmp, TRUE);
        if (clock->line[i].show) {
            sprintf(tmp, "data%d", i);
            ret = xfce_rc_read_entry(rc, tmp, NULL);
            g_string_assign(clock->line[i].data, ret);

            sprintf(tmp, "font%d", i);
            ret = xfce_rc_read_entry(rc, tmp, NULL);
            g_string_assign(clock->line[i].font, ret);
        }
    }

    xfce_rc_close(rc);
}

void oc_write_rc_file(XfcePanelPlugin *plugin, Clock *clock)
{
    gchar  *file;
    XfceRc *rc;
    gchar   tmp[100];
    gint i;

    if (!(file = xfce_panel_plugin_save_location(plugin, TRUE))) {
        g_warning("%s: unable to write rc file", OC_NAME);
        return;
    }
    if (!(rc = xfce_rc_simple_open(file, FALSE))) {
        g_warning("%s: unable to read-open rc file (%s)", OC_NAME, file);
        return;
    }
    g_free(file);

    xfce_rc_write_bool_entry(rc, "show_frame", clock->show_frame);

    xfce_rc_write_bool_entry(rc, "fg_set", clock->fg_set);
    if (clock->fg_set) {
        sprintf(tmp, "%uR %uG %uB"
                , clock->fg.red, clock->fg.green, clock->fg.blue);
        xfce_rc_write_entry(rc, "fg", tmp);
    }
    else {
        xfce_rc_delete_entry(rc, "fg", TRUE);
    }

    xfce_rc_write_bool_entry(rc, "bg_set", clock->bg_set);
    if (clock->bg_set) {
        sprintf(tmp, "%uR %uG %uB"
                , clock->bg.red, clock->bg.green, clock->bg.blue);
        xfce_rc_write_entry(rc, "bg", tmp);
    }
    else {
        xfce_rc_delete_entry(rc, "bg", TRUE);
    }

    xfce_rc_write_entry(rc, "timezone",  clock->timezone->str);

    for (i = 0; i < OC_MAX_LINES; i++) {
        sprintf(tmp, "show%d", i);
        xfce_rc_write_bool_entry(rc, tmp, clock->line[i].show);
        if (clock->line[i].show) {
            sprintf(tmp, "data%d", i);
            xfce_rc_write_entry(rc, tmp,  clock->line[i].data->str);
            sprintf(tmp, "font%d", i);
            xfce_rc_write_entry(rc, tmp,  clock->line[i].font->str);
        }
        else {
            sprintf(tmp, "data%d", i);
            xfce_rc_delete_entry(rc, tmp,  FALSE);
            sprintf(tmp, "font%d", i);
            xfce_rc_delete_entry(rc, tmp,  FALSE);
        }
    }

    xfce_rc_close(rc);
}

/* Create widgets and connect to signals */

Clock *orage_oc_new(XfcePanelPlugin *plugin)
{
    Clock *clock = g_new0(Clock, 1);
    gchar *data_init[] = {"%X", "%A", "%x"};
    gboolean show_init[] = {TRUE, FALSE, FALSE};
    gint  i;

    clock->plugin = plugin;

    clock->ebox = gtk_event_box_new();
    gtk_widget_show(clock->ebox);

    clock->frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(clock->ebox), clock->frame);
    gtk_widget_show(clock->frame);

    clock->vbox = gtk_vbox_new(TRUE, 0);
    gtk_widget_show(clock->vbox);
    gtk_container_add(GTK_CONTAINER(clock->frame), clock->vbox);

    clock->show_frame = TRUE;
    clock->fg_set = FALSE;
    clock->bg_set = FALSE;

    clock->timezone = g_string_new(""); /* = not set */
    clock->TZ_orig = g_strdup(g_getenv("TZ"));

    for (i = 0; i < OC_MAX_LINES; i++) {
        clock->line[i].label = gtk_label_new("");
        g_object_ref(clock->line[i].label); /* it is not always in the vbox */
        gtk_widget_show(clock->line[i].label);
        clock->line[i].show = show_init[i];
        clock->line[i].data = g_string_new(data_init[i]);
        clock->line[i].font = g_string_new("");
    }

    clock->tips = gtk_tooltips_new();
    g_object_ref(clock->tips);
    gtk_object_sink(GTK_OBJECT(clock->tips));
    oc_get_time(clock);
    clock->timeout_id = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE
            , 200, (GSourceFunc) oc_get_time, clock, NULL);
        
    return(clock);
}

void oc_show_frame_set(Clock *clock)
{
    gtk_frame_set_shadow_type(GTK_FRAME(clock->frame)
            , clock->show_frame ? GTK_SHADOW_IN : GTK_SHADOW_NONE);
}

void oc_fg_set(Clock *clock)
{
    GdkColor *fg = NULL;
    gint      i;

    if (clock->fg_set)
        fg = &clock->fg;

    for (i = 0; i < OC_MAX_LINES; i++)
        gtk_widget_modify_fg(clock->line[i].label, GTK_STATE_NORMAL, fg);
}

void oc_bg_set(Clock *clock)
{
    GdkColor *bg = NULL;

    if (clock->bg_set)
        bg = &clock->bg;

    gtk_widget_modify_bg(clock->ebox, GTK_STATE_NORMAL, bg);
}

void oc_timezone_set(Clock *clock)
{

    if ((clock->timezone->str != NULL) && (clock->timezone->len > 0)) {
        /* user has set timezone, so let's set TZ */
        g_setenv("TZ", clock->timezone->str, 1);
    }
    else if (clock->TZ_orig != NULL) { /* we had TZ when we started */
        g_setenv("TZ", clock->TZ_orig, 1);
    }
    else { /* TZ was not set so take it out */
        g_unsetenv("TZ");
    }
    tzset();
}

void oc_show_line_set(Clock *clock, gint lno)
{
    GtkWidget *line_label = clock->line[lno].label;

    if (clock->line[lno].show) {
        gtk_box_pack_start(GTK_BOX(clock->vbox), line_label, FALSE, FALSE, 0);
        switch (lno) {
            case 0: /* always on top */
                gtk_box_reorder_child(GTK_BOX(clock->vbox), line_label, 0);
                break;
            case 1: /* if line 0 is missing, we are first */
                if (clock->line[0].show)  /* we are second line */
                    gtk_box_reorder_child(GTK_BOX(clock->vbox), line_label, 1);
                else /* we are top line */
                    gtk_box_reorder_child(GTK_BOX(clock->vbox), line_label, 0);
                break;
            case 2: /* always last */
                break;
        }
    }
    else {
        gtk_container_remove(GTK_CONTAINER(clock->vbox), line_label);
    }

    oc_update_size(clock,
            xfce_panel_plugin_get_size(XFCE_PANEL_PLUGIN(clock->plugin)));
}

void oc_line_font_set(Clock *clock, gint lno)
{
    PangoFontDescription *font;

    if (clock->line[lno].font->str) {
        font = pango_font_description_from_string(clock->line[lno].font->str);
        gtk_widget_modify_font(clock->line[lno].label, font);
        pango_font_description_free(font);
    }
    else
        gtk_widget_modify_font(clock->line[lno].label, NULL);
}

static void oc_construct(XfcePanelPlugin *plugin)
{
    Clock *clock;
    gint i;

    xfce_textdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

    clock = orage_oc_new(plugin);

    gtk_container_add(GTK_CONTAINER(plugin), clock->ebox);

    oc_read_rc_file(plugin, clock);

    oc_show_frame_set(clock);
    oc_fg_set(clock);
    oc_bg_set(clock);
    oc_timezone_set(clock);
    for (i = 0; i < OC_MAX_LINES; i++) {
        if (clock->line[i].show)  /* need to add */
            oc_show_line_set(clock, i);
        oc_line_font_set(clock, i);
    }

    oc_update_size(clock, 
            xfce_panel_plugin_get_size(XFCE_PANEL_PLUGIN(plugin)));

    xfce_panel_plugin_add_action_widget(plugin, clock->ebox);
    
    xfce_panel_plugin_menu_show_configure(plugin);

    g_signal_connect(plugin, "configure-plugin", 
            G_CALLBACK(oc_properties_dialog), clock);

    g_signal_connect(plugin, "size-changed", 
            G_CALLBACK(oc_set_size), clock);
    
    g_signal_connect(plugin, "free-data", 
            G_CALLBACK(oc_free_data), clock);
    
    g_signal_connect(plugin, "save", 
            G_CALLBACK(oc_write_rc_file), clock);

/* callback for calendar and globaltime popup */
    g_signal_connect(clock->ebox, "button-press-event",
            G_CALLBACK(on_button_press_event_cb), clock);

}

/* Register with the panel */

XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL(oc_construct);

