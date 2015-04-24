/*
 * Copyright (C) 2015 Endless Mobile
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Alexander Larsson <alexl@redhat.com>
 */

#include <stdint.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "wakefield-compositor.h"
#include "wakefield-private.h"
#include "xdg-shell-server-protocol.h"

#define wl_resource_for_each_reverse(resource, list)				\
	for (resource = 0, resource = wl_resource_from_link((list)->prev);	\
	     wl_resource_get_link(resource) != (list);				\
	     resource = wl_resource_from_link(wl_resource_get_link(resource)->prev))

struct WakefieldPointer
{
  struct wl_list resource_list;
  struct wl_resource *cursor_surface;

  guint32 button_count;

  struct wl_resource *current_surface;

  struct wl_client *grab_client;
  guint32 grab_button;
  GdkDevice *grab_device;
  struct wl_resource *grab_xdg_popup;
  guint32 grab_time;
  guint32 grab_serial;
};

struct WakefieldKeyboard
{
  struct wl_list resource_list;
};

struct WakefieldOutput
{
  struct wl_list resource_list;
};

struct WakefieldSeat
{
  struct WakefieldPointer pointer;
  struct WakefieldKeyboard keyboard;
};

struct WakefieldRegion
{
  struct wl_resource *resource;
  cairo_region_t *region;
};

struct _WakefieldCompositorPrivate
{
  GdkWindow *event_window;
  GSource *wayland_source;
  struct wl_display *wl_display;

  struct wl_list surfaces;
  struct wl_list xdg_surfaces;
  struct wl_list xdg_popups;
  struct wl_list shell_resources;
  struct WakefieldSeat seat;
  struct WakefieldOutput output;
  struct WakefieldDataDevice *data_device;
};
typedef struct _WakefieldCompositorPrivate WakefieldCompositorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (WakefieldCompositor, wakefield_compositor, GTK_TYPE_WIDGET);

static void
wakefield_compositor_realize (GtkWidget *widget)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  GtkAllocation allocation;
  GdkWindow *window;
  GdkWindowAttr attributes;
  gint attributes_mask;
  struct wl_resource *xdg_surface_resource;

  gtk_widget_set_realized (widget, TRUE);

  window = gtk_widget_get_parent_window (widget);
  gtk_widget_set_window (widget, window);
  g_object_ref (window);

  gtk_widget_get_allocation (widget, &allocation);

  attributes.x = allocation.x;
  attributes.y = allocation.y;
  attributes.width = allocation.width;
  attributes.height = allocation.height;
  attributes.wclass = GDK_INPUT_ONLY;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  attributes.window_type = GDK_WINDOW_CHILD;

  attributes.event_mask = (gtk_widget_get_events (widget) |
                           GDK_EXPOSURE_MASK);

  priv->event_window = gdk_window_new (window,
                                       &attributes, attributes_mask);
  gtk_widget_register_window (widget, priv->event_window);

  wl_resource_for_each (xdg_surface_resource, &priv->xdg_surfaces)
    {
      wakefield_xdg_surface_realize (xdg_surface_resource, priv->event_window);
    }
}

static void
wakefield_compositor_unrealize (GtkWidget *widget)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *xdg_surface_resource;

  wl_resource_for_each (xdg_surface_resource, &priv->xdg_surfaces)
    {
      wakefield_xdg_surface_unrealize (xdg_surface_resource);
    }

  if (priv->event_window != NULL)
    {
      gtk_widget_unregister_window (widget, priv->event_window);
      gdk_window_destroy (priv->event_window);
      priv->event_window = NULL;
    }

  GTK_WIDGET_CLASS (wakefield_compositor_parent_class)->unrealize (widget);
}


static void
wakefield_compositor_map (GtkWidget *widget)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

  GTK_WIDGET_CLASS (wakefield_compositor_parent_class)->map (widget);

  gdk_window_show (priv->event_window);
}

static void
wakefield_compositor_unmap (GtkWidget *widget)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

  gdk_window_hide (priv->event_window);

  GTK_WIDGET_CLASS (wakefield_compositor_parent_class)->unmap (widget);
}

static void
refresh_output (WakefieldCompositor *compositor,
                struct wl_resource *output)
{
  GtkAllocation allocation;

  gtk_widget_get_allocation (GTK_WIDGET (compositor), &allocation);

  wl_output_send_scale (output, gtk_widget_get_scale_factor (GTK_WIDGET (compositor)));
  wl_output_send_mode (output,
                       WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                       allocation.width,
                       allocation.height,
                       60);
  wl_output_send_done (output);
}

static void
send_xdg_configure_request (WakefieldCompositor *compositor,
                            struct wl_resource *xdg_surface)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  GtkAllocation allocation;
  struct wl_array states;
  uint32_t serial = wl_display_next_serial (priv->wl_display);
  uint32_t *s;

  gtk_widget_get_allocation (GTK_WIDGET (compositor), &allocation);

  wl_array_init(&states);
  s = wl_array_add(&states, sizeof *s);
  *s = XDG_SURFACE_STATE_FULLSCREEN;
  if ((gtk_widget_get_state_flags (GTK_WIDGET (compositor)) & GTK_STATE_FLAG_BACKDROP) == 0)
    {
      s = wl_array_add(&states, sizeof *s);
      *s = XDG_SURFACE_STATE_ACTIVATED;
    }
  xdg_surface_send_configure (xdg_surface, allocation.width, allocation.height,
                              &states, serial);
  wl_array_release(&states);
}

static void
wakefield_compositor_size_allocate (GtkWidget *widget,
                                    GtkAllocation *allocation)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *xdg_surface_resource, *output;

  gtk_widget_set_allocation (widget, allocation);

  if (gtk_widget_get_realized (widget))
    gdk_window_move_resize (priv->event_window,
                            allocation->x,
                            allocation->y,
                            allocation->width,
                            allocation->height);

  wl_resource_for_each (output, &priv->output.resource_list)
    {
      refresh_output (compositor, output);
    }

  wl_resource_for_each (xdg_surface_resource, &priv->xdg_surfaces)
    {
      send_xdg_configure_request (compositor, xdg_surface_resource);
    }
}

static void
wakefield_compositor_state_flags_changed (GtkWidget        *widget,
                                          GtkStateFlags     old_state)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *xdg_surface_resource;

  GTK_WIDGET_CLASS (wakefield_compositor_parent_class)->state_flags_changed (widget, old_state);

  wl_resource_for_each (xdg_surface_resource, &priv->xdg_surfaces)
    {
      send_xdg_configure_request (compositor, xdg_surface_resource);
    }
}

static gboolean
wakefield_compositor_draw (GtkWidget *widget,
                           cairo_t   *cr)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *xdg_surface_resource;

  wl_resource_for_each (xdg_surface_resource, &priv->xdg_surfaces)
    {
      struct wl_resource *surface_resource = wakefield_xdg_surface_get_surface (xdg_surface_resource);
      if (surface_resource)
        wakefield_surface_draw (surface_resource, cr);
    }

  return TRUE;
}

static struct wl_resource *
wakefield_compositor_get_pointer_for_client (WakefieldCompositor *compositor,
                                             struct wl_client *client)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

  return wl_resource_find_for_client (&priv->seat.pointer.resource_list, client);
}

static uint32_t
convert_gdk_button_to_libinput (int gdk_button)
{
 switch (gdk_button)
   {
   case 3:
     return 273;
   case 2:
     return 274;
   default:
     return gdk_button + 271;
   }
}

static void
wakefield_compositor_clear_grab (WakefieldCompositor *compositor)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct WakefieldPointer *pointer = &priv->seat.pointer;

  if (pointer->grab_xdg_popup)
    gdk_device_ungrab (pointer->grab_device, GDK_CURRENT_TIME);

  pointer->grab_button = 0;
  pointer->grab_serial = 0;
  pointer->grab_device = NULL;
  pointer->grab_client = NULL;
  pointer->grab_xdg_popup = NULL;
}

static gboolean
should_send_pointer_event (WakefieldCompositor *compositor)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct WakefieldPointer *pointer = &priv->seat.pointer;

  /* We never deliver events outside a client-owned surface except when
     there is an implicit grab (not popup grab, those are owner_events). */
  return
    pointer->current_surface != NULL ||
    (pointer->grab_button != 0 && pointer->grab_xdg_popup == NULL);
}

void
wakefield_compositor_send_button (WakefieldCompositor *compositor,
                                  struct wl_resource *surface,
                                  GdkEventButton *event)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct WakefieldPointer *pointer = &priv->seat.pointer;
  struct wl_resource *pointer_resource, *xdg_popup_resource;
  uint32_t serial = wl_display_next_serial (priv->wl_display);
  guint32 button;

  button = convert_gdk_button_to_libinput (event->button);

  if (event->type == GDK_BUTTON_PRESS)
    {
      if (pointer->button_count == 0)
        {
          pointer->grab_button = button;
          pointer->grab_device = gdk_event_get_device ((GdkEvent *)event);
          pointer->grab_client = wl_resource_get_client (surface);
          pointer->grab_time = event->time;
      }
      pointer->button_count++;
    }
  else
    {
      pointer->button_count--;

      if (pointer->button_count == 0)
        {
          if (pointer->grab_xdg_popup == NULL)
            wakefield_compositor_clear_grab (compositor);
          else if (pointer->grab_xdg_popup != NULL && priv->seat.pointer.current_surface == NULL)
            {
              wl_resource_for_each (xdg_popup_resource, &priv->xdg_popups)
                wakefield_xdg_popup_close (xdg_popup_resource);
            }
        }
    }

  if (surface != NULL && should_send_pointer_event (compositor))
    {
      pointer_resource = wakefield_compositor_get_pointer_for_client (compositor, wl_resource_get_client (surface));
      if (pointer_resource)
        wl_pointer_send_button (pointer_resource, serial,
                                event->time,
                                button,
                                (event->type == GDK_BUTTON_PRESS ? 1 : 0));
    }

  if (pointer->button_count == 1 && event->type == GDK_BUTTON_PRESS)
    pointer->grab_serial = wl_display_get_serial (priv->wl_display);
}

void
wakefield_compositor_send_scroll (WakefieldCompositor *compositor,
                                  struct wl_resource *surface,
                                  GdkEventScroll *event)
{
  struct wl_resource *pointer_resource;

  if (surface == NULL)
    return;

  pointer_resource = wakefield_compositor_get_pointer_for_client (compositor,
                                                                  wl_resource_get_client (surface));
  if (pointer_resource && should_send_pointer_event (compositor))
    {
      switch (event->direction)
        {
        case GDK_SCROLL_SMOOTH:
          if (event->delta_x != 0)
            {
              wl_pointer_send_axis (pointer_resource,
                                    event->time,
                                    WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                    wl_fixed_from_double (event->delta_x * 10.0));
            }
          if (event->delta_y != 0)
            {
              wl_pointer_send_axis (pointer_resource,
                                    event->time,
                                    WL_POINTER_AXIS_VERTICAL_SCROLL,
                                    wl_fixed_from_double (event->delta_y * 10.0));
            }
          break;
        case GDK_SCROLL_UP:
          wl_pointer_send_axis (pointer_resource,
                                event->time,
                                WL_POINTER_AXIS_VERTICAL_SCROLL,
                                wl_fixed_from_int (-10.0));
          break;
        case GDK_SCROLL_DOWN:
          wl_pointer_send_axis (pointer_resource,
                                event->time,
                                WL_POINTER_AXIS_VERTICAL_SCROLL,
                                wl_fixed_from_int (10.0));
          break;
        case GDK_SCROLL_LEFT:
          wl_pointer_send_axis (pointer_resource,
                                event->time,
                                WL_POINTER_AXIS_VERTICAL_SCROLL,
                                wl_fixed_from_int (-10.0));
          break;
        case GDK_SCROLL_RIGHT:
          wl_pointer_send_axis (pointer_resource,
                                event->time,
                                WL_POINTER_AXIS_VERTICAL_SCROLL,
                                wl_fixed_from_int (10.0));
          break;
        }
    }
}

void
wakefield_compositor_send_motion (WakefieldCompositor *compositor,
                                  struct wl_resource *surface,
                                  GdkEventMotion *event)
{
  struct wl_resource *pointer_resource;

  if (surface == NULL)
    return;

  pointer_resource = wakefield_compositor_get_pointer_for_client (compositor,
                                                                  wl_resource_get_client (surface));
  if (pointer_resource && should_send_pointer_event (compositor))
    {
      wl_pointer_send_motion (pointer_resource,
                              event->time,
                              wl_fixed_from_double (event->x),
                              wl_fixed_from_double (event->y));
    }
}

void
wakefield_compositor_send_enter (WakefieldCompositor *compositor,
                                 struct wl_resource  *surface,
                                 GdkEventCrossing *event)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *pointer_resource;
  uint32_t serial = wl_display_next_serial (priv->wl_display);

  if (surface == NULL)
    return;

  g_assert (priv->seat.pointer.current_surface == NULL);
  priv->seat.pointer.current_surface = surface;

  pointer_resource = wakefield_compositor_get_pointer_for_client (compositor,
                                                                  wl_resource_get_client (surface));
  if (pointer_resource && should_send_pointer_event (compositor))
    {
      wl_pointer_send_enter (pointer_resource, serial,
                             surface,
                             wl_fixed_from_double (event->x),
                             wl_fixed_from_double (event->y));
    }
}

void
wakefield_compositor_send_leave (WakefieldCompositor *compositor,
                                 struct wl_resource  *surface,
                                 GdkEventCrossing *event)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *pointer_resource;
  uint32_t serial = wl_display_next_serial (priv->wl_display);

  if (surface == NULL)
    return;

  g_assert (priv->seat.pointer.current_surface == surface);
  priv->seat.pointer.current_surface = NULL;

  pointer_resource = wakefield_compositor_get_pointer_for_client (compositor,
                                                                  wl_resource_get_client (surface));
  if (pointer_resource && should_send_pointer_event (compositor))
    {
      wl_pointer_send_leave (pointer_resource, serial,
                             surface);
    }
}

static struct wl_resource *
wakefield_compositor_get_xdg_surface_for_window (WakefieldCompositor *compositor,
                                                 GdkWindow *window)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *xdg_surface_resource;

  wl_resource_for_each (xdg_surface_resource, &priv->xdg_surfaces)
    {
      if (window == wakefield_xdg_surface_get_window (xdg_surface_resource))
        return wakefield_xdg_surface_get_surface (xdg_surface_resource);
    }

  return NULL;
}


static gboolean
wakefield_compositor_button_press_event (GtkWidget      *widget,
                                         GdkEventButton *event)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  struct wl_resource *surface;

  surface = wakefield_compositor_get_xdg_surface_for_window (compositor, event->window);

  if (surface)
    wakefield_compositor_send_button (compositor, surface, event);

  return TRUE;
}

static gboolean
wakefield_compositor_button_release_event (GtkWidget      *widget,
                                           GdkEventButton *event)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  struct wl_resource *surface;

  surface = wakefield_compositor_get_xdg_surface_for_window (compositor, event->window);

  if (surface)
    wakefield_compositor_send_button (compositor, surface, event);

  return TRUE;
}

static gboolean
wakefield_compositor_scroll_event (GtkWidget      *widget,
                                   GdkEventScroll *event)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  struct wl_resource *surface;

  surface = wakefield_compositor_get_xdg_surface_for_window (compositor, event->window);

  if (surface)
    wakefield_compositor_send_scroll (compositor, surface, event);

  return TRUE;
}

static gboolean
wakefield_compositor_motion_notify_event (GtkWidget      *widget,
                                          GdkEventMotion *event)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  struct wl_resource *surface;

  surface = wakefield_compositor_get_xdg_surface_for_window (compositor, event->window);

  if (surface)
    wakefield_compositor_send_motion (compositor, surface, event);

  return FALSE;
}

static gboolean
wakefield_compositor_enter_notify_event (GtkWidget        *widget,
                                         GdkEventCrossing *event)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  struct wl_resource *surface;

  if (event->mode == GDK_CROSSING_GRAB || event->mode == GDK_CROSSING_UNGRAB)
    return FALSE;

  surface = wakefield_compositor_get_xdg_surface_for_window (compositor, event->window);

  if (surface)
    wakefield_compositor_send_enter (compositor,
                                     surface,
                                     event);

  return FALSE;
}

static gboolean
wakefield_compositor_leave_notify_event (GtkWidget        *widget,
                                         GdkEventCrossing *event)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  struct wl_resource *surface;

  if (event->mode == GDK_CROSSING_GRAB || event->mode == GDK_CROSSING_UNGRAB)
    return FALSE;

  surface = wakefield_compositor_get_xdg_surface_for_window (compositor, event->window);

  if (surface)
    wakefield_compositor_send_leave (compositor, surface, event);

  return FALSE;
}

static void
resource_release (struct wl_client *client,
                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
pointer_set_cursor (struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t serial,
                    struct wl_resource *surface_resource,
                    int32_t x, int32_t y)
{
}

static const struct wl_pointer_interface pointer_implementation = {
  pointer_set_cursor,
  resource_release,
};

static void
seat_get_pointer (struct wl_client    *client,
                  struct wl_resource  *seat_resource,
                  uint32_t             id)
{
  struct WakefieldSeat *seat = wl_resource_get_user_data (seat_resource);
  struct WakefieldPointer *pointer = &seat->pointer;
  struct wl_resource *cr;

  cr = wl_resource_create (client, &wl_pointer_interface, wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (cr, &pointer_implementation, pointer, unbind_resource);
  wl_list_insert (&pointer->resource_list, wl_resource_get_link (cr));
}

static void
wakefield_pointer_init (struct WakefieldPointer *pointer)
{
  wl_list_init (&pointer->resource_list);
  pointer->cursor_surface = NULL;
}

static const struct wl_keyboard_interface keyboard_implementation = {
  resource_release,
};

static void
seat_get_keyboard (struct wl_client    *client,
                   struct wl_resource  *seat_resource,
                   uint32_t             id)
{
  struct WakefieldSeat *seat = wl_resource_get_user_data (seat_resource);
  struct WakefieldKeyboard *keyboard = &seat->keyboard;
  struct wl_resource *cr;

  cr = wl_resource_create (client, &wl_keyboard_interface, wl_resource_get_version (seat_resource), id);
  wl_resource_set_implementation (cr, &keyboard_implementation, keyboard, unbind_resource);
  wl_list_insert (&keyboard->resource_list, wl_resource_get_link (cr));
}

static void
wakefield_keyboard_init (struct WakefieldKeyboard *keyboard)
{
  wl_list_init (&keyboard->resource_list);
}

#define SEAT_VERSION 4

static const struct wl_seat_interface seat_interface = {
  seat_get_pointer,
  seat_get_keyboard, /* get_keyboard */
  NULL, /* get_touch */
};

static void
wl_seat_destructor (struct wl_resource *resource)
{
}

static void
bind_seat (struct wl_client *client,
           void *data,
           uint32_t version,
           uint32_t id)
{
  struct WaylandSeat *seat = data;
  struct wl_resource *cr;

  cr = wl_resource_create (client, &wl_seat_interface, version, id);
  wl_resource_set_implementation (cr, &seat_interface, seat, wl_seat_destructor);
  wl_seat_send_capabilities (cr, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
  wl_seat_send_name (cr, "seat0");
}

static void
wakefield_seat_init (struct WakefieldSeat *seat,
                     struct wl_display    *wl_display)
{
  wakefield_pointer_init (&seat->pointer);
  wakefield_keyboard_init (&seat->keyboard);

  wl_global_create (wl_display, &wl_seat_interface, SEAT_VERSION, seat, bind_seat);
}

static void
bind_output (struct wl_client *client,
             void *data,
             uint32_t version,
             uint32_t id)
{
  WakefieldCompositor *compositor = data;
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct WakefieldOutput *output = &priv->output;
  struct wl_resource *cr;

  cr = wl_resource_create (client, &wl_output_interface, version, id);
  wl_resource_set_implementation (cr, NULL, output, unbind_resource);
  wl_list_insert (&output->resource_list, wl_resource_get_link (cr));

  wl_output_send_geometry (cr,
                           0, 0,
                           0, 0, /* mm */
                           0, /* subpixel */
                           "Wakefield", "Gtk",
                           WL_OUTPUT_TRANSFORM_NORMAL);

  refresh_output (compositor, cr);
}

#define WL_OUTPUT_VERSION 2

static void
wakefield_output_init (WakefieldCompositor *compositor)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  wl_list_init (&priv->output.resource_list);
  wl_global_create (priv->wl_display, &wl_output_interface,
                    WL_OUTPUT_VERSION, compositor, bind_output);
}

static GSource * wayland_event_source_new (struct wl_display *display);

cairo_region_t *
wakefield_region_get_region (struct wl_resource *region_resource)
{
  struct WakefieldRegion *region = wl_resource_get_user_data (region_resource);
  return cairo_region_copy (region->region);
}

static void
wl_region_add (struct wl_client *client,
               struct wl_resource *resource,
               gint32 x,
               gint32 y,
               gint32 width,
               gint32 height)
{
  struct WakefieldRegion *region = wl_resource_get_user_data (resource);
  cairo_rectangle_int_t rectangle = { x, y, width, height };
  cairo_region_union_rectangle (region->region, &rectangle);
}

static void
wl_region_subtract (struct wl_client *client,
                    struct wl_resource *resource,
                    gint32 x,
                    gint32 y,
                    gint32 width,
                    gint32 height)
{
  struct WakefieldRegion *region = wl_resource_get_user_data (resource);
  cairo_rectangle_int_t rectangle = { x, y, width, height };
  cairo_region_subtract_rectangle (region->region, &rectangle);
}

static const struct wl_region_interface region_interface = {
  resource_release,
  wl_region_add,
  wl_region_subtract
};

static void
wl_region_destructor (struct wl_resource *resource)
{
  struct WakefieldRegion *region = wl_resource_get_user_data (resource);

  cairo_region_destroy (region->region);
  g_slice_free (struct WakefieldRegion, region);
}

static void
wl_compositor_create_region (struct wl_client *client,
                             struct wl_resource *compositor_resource,
                             uint32_t id)
{
  struct WakefieldRegion *region = g_slice_new0 (struct WakefieldRegion);

  region->resource = wl_resource_create (client, &wl_region_interface, wl_resource_get_version (compositor_resource), id);
  wl_resource_set_implementation (region->resource, &region_interface, region, wl_region_destructor);

  region->region = cairo_region_create ();
}

void
wakefield_compositor_surface_unmapped (WakefieldCompositor *compositor,
                                       struct wl_resource  *surface)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *xdg_surface = wakefield_surface_get_xdg_surface (surface);
  struct wl_resource *xdg_popup = wakefield_surface_get_xdg_popup (surface);

  if (priv->seat.pointer.current_surface == surface)
    priv->seat.pointer.current_surface = NULL;

  if (xdg_surface)
    gtk_widget_queue_draw (GTK_WIDGET (compositor));

  if (xdg_popup != NULL &&
      priv->seat.pointer.grab_xdg_popup == xdg_popup)
    {
      wakefield_compositor_clear_grab (compositor);
    }
}

void
wakefield_compositor_surface_mapped (WakefieldCompositor *compositor,
                                     struct wl_resource  *surface)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *xdg_surface = wakefield_surface_get_xdg_surface  (surface);

  if (xdg_surface && gtk_widget_get_realized (GTK_WIDGET (compositor)))
    wakefield_xdg_surface_realize (xdg_surface, priv->event_window);
}

static void
wl_compositor_create_surface (struct wl_client *client,
                              struct wl_resource *compositor_resource,
                              uint32_t id)
{
  WakefieldCompositor *compositor = wl_resource_get_user_data (compositor_resource);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *surface;

  surface = wakefield_surface_new (compositor, client, compositor_resource, id);
  wl_list_insert (&priv->surfaces, wl_resource_get_link (surface));
}


const static struct wl_compositor_interface compositor_interface = {
  wl_compositor_create_surface,
  wl_compositor_create_region
};


static void
xdg_shell_destroy (struct wl_client *client,
                   struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_use_unstable_version (struct wl_client *client,
                          struct wl_resource *resource,
                          int32_t version)
{
  if (version > 5)
    {
      wl_resource_post_error (resource, 1,
                              "xdg-shell:: version not implemented yet.");
      return;
    }
}

static void
xdg_get_xdg_surface (struct wl_client *client,
                     struct wl_resource *shell_resource,
                     uint32_t id,
                     struct wl_resource *surface_resource)
{
  struct wl_resource *xdg_surface, *output_resource;
  WakefieldCompositor *compositor = wl_resource_get_user_data (shell_resource);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  WakefieldSurfaceRole role;

  if (surface_resource == NULL)
    {
      wl_resource_post_error (shell_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "xdg_shell::get_xdg_popup requires a surface");
      return;
    }

  role = wakefield_surface_get_role (surface_resource);
  if (role)
    {
      wl_resource_post_error (shell_resource, XDG_SHELL_ERROR_ROLE,
                              "This wl_surface already has a role");
      return;
    }

  xdg_surface = wakefield_xdg_surface_new (client, shell_resource, id, surface_resource);
  wl_list_insert (priv->xdg_surfaces.prev, wl_resource_get_link (xdg_surface));

  output_resource = wl_resource_find_for_client (&priv->output.resource_list, client);
  wl_surface_send_enter (surface_resource, output_resource);

  send_xdg_configure_request (compositor, xdg_surface);
}

static void
xdg_get_xdg_popup (struct wl_client *client,
                   struct wl_resource *shell_resource,
                   uint32_t id,
                   struct wl_resource *surface_resource,
                   struct wl_resource *parent_resource,
                   struct wl_resource *seat_resource,
                   uint32_t serial,
                   int32_t x, int32_t y)
{
  WakefieldCompositor *compositor = wl_resource_get_user_data (shell_resource);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct WakefieldPointer *pointer = &priv->seat.pointer;
  struct wl_resource *xdg_popup, *output_resource;
  WakefieldSurfaceRole role, parent_role;

  if (surface_resource == NULL)
    {
      wl_resource_post_error (shell_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "xdg_shell::get_xdg_popup requires a surface");
      return;
    }

  role = wakefield_surface_get_role (surface_resource);
  if (role)
    {
      wl_resource_post_error (shell_resource, XDG_SHELL_ERROR_ROLE,
                              "This wl_surface already has a role");
      return;
    }

  if (parent_resource == NULL)
    {
      wl_resource_post_error (shell_resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "xdg_shell::get_xdg_popup requires a parent shell surface");
      return;
    }

  parent_role = wakefield_surface_get_role (parent_resource);

  if (parent_role != WAKEFIELD_SURFACE_ROLE_XDG_SURFACE &&
      parent_role != WAKEFIELD_SURFACE_ROLE_XDG_POPUP)
    {
      wl_resource_post_error (shell_resource,
                              XDG_POPUP_ERROR_INVALID_PARENT,
                              "xdg_popup parent was invalid");
      return;
    }

  if (!wl_list_empty (&priv->xdg_popups))
    {
      struct wl_resource *top_xdg_popup = wl_resource_from_link (priv->xdg_popups.next);
      if (parent_resource != wakefield_xdg_surface_get_surface (top_xdg_popup))
        {
          wl_resource_post_error (shell_resource,
                                  XDG_POPUP_ERROR_NOT_THE_TOPMOST_POPUP,
                                  "xdg_popup was not created on the "
                                  "topmost popup");
          return;
        }
    }

  xdg_popup = wakefield_xdg_popup_new (compositor, client, shell_resource, id, surface_resource, parent_resource, serial, x, y);
  wl_list_insert (&priv->xdg_popups, wl_resource_get_link (xdg_popup));

  output_resource = wl_resource_find_for_client (&priv->output.resource_list, client);
  wl_surface_send_enter (surface_resource, output_resource);

  if (pointer->grab_button != 0 &&
      wakefield_xdg_popup_get_serial (xdg_popup) == pointer->grab_serial)
    {
      pointer->grab_xdg_popup = xdg_popup;
      gdk_device_grab (pointer->grab_device,
                       wakefield_surface_get_window (parent_resource),
                       GDK_OWNERSHIP_NONE,
                       TRUE,
                       GDK_POINTER_MOTION_MASK |
                       GDK_BUTTON_PRESS_MASK |
                       GDK_BUTTON_RELEASE_MASK |
                       GDK_SCROLL_MASK |
                       GDK_ENTER_NOTIFY_MASK |
                       GDK_LEAVE_NOTIFY_MASK,
                       NULL,
                       pointer->grab_time);
    }
  else
    {
      wakefield_xdg_popup_close (xdg_popup);
    }
}

static void
xdg_pong (struct wl_client *client,
          struct wl_resource *resource,
          uint32_t serial)
{
  wl_resource_post_error (resource, 1,
                          "xdg-shell:: pong not implemented yet.");
}

static const struct xdg_shell_interface xdg_implementation = {
  xdg_shell_destroy,
  xdg_use_unstable_version,
  xdg_get_xdg_surface,
  xdg_get_xdg_popup,
  xdg_pong
};

#define XDG_SHELL_VERSION 1

static void
bind_xdg_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
  WakefieldCompositor *compositor = data;
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *cr;

  cr = wl_resource_create (client,  &xdg_shell_interface, XDG_SHELL_VERSION, id);
  wl_resource_set_implementation (cr, &xdg_implementation, compositor, unbind_resource);
  wl_list_insert (&priv->shell_resources, wl_resource_get_link (cr));
}

static void
bind_compositor (struct wl_client *client,
                 void *data,
                 uint32_t version,
                 uint32_t id)
{
  WakefieldCompositor *compositor = data;
  struct wl_resource *cr;

  cr = wl_resource_create (client, &wl_compositor_interface, version, id);
  wl_resource_set_implementation (cr, &compositor_interface, compositor, NULL);
}

#define WL_COMPOSITOR_VERSION 3

struct wl_display *
wakefield_compositor_get_display (WakefieldCompositor *compositor)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

  return priv->wl_display;
}

static void
wakefield_compositor_init (WakefieldCompositor *compositor)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  int fds[2];

  gtk_widget_set_has_window (GTK_WIDGET (compositor), FALSE);

  priv->wl_display = wl_display_create ();
  wl_display_init_shm (priv->wl_display);

  wl_global_create (priv->wl_display, &wl_compositor_interface,
                    WL_COMPOSITOR_VERSION, compositor, bind_compositor);

  wl_global_create (priv->wl_display, &xdg_shell_interface,
                    XDG_SHELL_VERSION, compositor, bind_xdg_shell);
  wl_list_init (&priv->shell_resources);

  priv->data_device = wakefield_data_device_new (compositor);

  wakefield_seat_init (&priv->seat, priv->wl_display);
  wakefield_output_init (compositor);

  wl_list_init (&priv->surfaces);
  wl_list_init (&priv->xdg_surfaces);
  wl_list_init (&priv->xdg_popups);

  socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);

  /* XXX: For testing */
  wl_display_add_socket_auto (priv->wl_display);

  /* Attach the wl_event_loop to ours */
  priv->wayland_source = wayland_event_source_new (priv->wl_display);
  g_source_attach (priv->wayland_source, NULL);
}

static void
wakefield_compositor_finalize (GObject *object)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (object);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

  g_source_destroy (priv->wayland_source);
  wl_display_destroy (priv->wl_display);

  G_OBJECT_CLASS (wakefield_compositor_parent_class)->finalize (object);
}


static void
wakefield_compositor_class_init (WakefieldCompositorClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = wakefield_compositor_finalize;

  widget_class->realize = wakefield_compositor_realize;
  widget_class->unrealize = wakefield_compositor_unrealize;
  widget_class->map = wakefield_compositor_map;
  widget_class->unmap = wakefield_compositor_unmap;
  widget_class->size_allocate = wakefield_compositor_size_allocate;
  widget_class->draw = wakefield_compositor_draw;
  widget_class->enter_notify_event = wakefield_compositor_enter_notify_event;
  widget_class->leave_notify_event = wakefield_compositor_leave_notify_event;
  widget_class->scroll_event = wakefield_compositor_scroll_event;
  widget_class->button_press_event = wakefield_compositor_button_press_event;
  widget_class->button_release_event = wakefield_compositor_button_release_event;
  widget_class->motion_notify_event = wakefield_compositor_motion_notify_event;
  widget_class->state_flags_changed = wakefield_compositor_state_flags_changed;
}

/* Wayland GSource */

typedef struct
{
  GSource source;
  struct wl_display *display;
} WaylandEventSource;

static gboolean
wayland_event_source_prepare (GSource *base, int *timeout)
{
  WaylandEventSource *source = (WaylandEventSource *)base;

  *timeout = -1;

  wl_display_flush_clients (source->display);

  return FALSE;
}

static gboolean
wayland_event_source_dispatch (GSource *base,
                               GSourceFunc callback,
                               void *data)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  struct wl_event_loop *loop = wl_display_get_event_loop (source->display);

  wl_event_loop_dispatch (loop, 0);

  return TRUE;
}

static GSourceFuncs wayland_event_source_funcs =
{
  wayland_event_source_prepare,
  NULL,
  wayland_event_source_dispatch,
  NULL
};

static GSource *
wayland_event_source_new (struct wl_display *display)
{
  WaylandEventSource *source;
  struct wl_event_loop *loop = wl_display_get_event_loop (display);

  source = (WaylandEventSource *) g_source_new (&wayland_event_source_funcs,
                                                sizeof (WaylandEventSource));
  source->display = display;
  g_source_add_unix_fd (&source->source,
                        wl_event_loop_get_fd (loop),
                        G_IO_IN | G_IO_ERR);

  return &source->source;
}
