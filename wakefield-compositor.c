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

struct WakefieldPointer
{
  struct wl_list resource_list;
  struct wl_resource *cursor_surface;
};

struct WakefieldShell
{
  struct wl_list resource_list;
};

struct WakefieldSeat
{
  struct WakefieldPointer pointer;
  /* struct WakefieldKeyboard keyboard; */
};

struct WakefieldRegion
{
  struct wl_resource *resource;
  cairo_region_t *region;
};

struct _WakefieldCompositorPrivate
{
  GdkWindow *event_window;
  struct wl_display *wl_display;

  struct wl_list surfaces;
  struct WakefieldSeat seat;
  struct WakefieldShell shell;
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
                           GDK_POINTER_MOTION_MASK |
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_SCROLL_MASK |
                           GDK_FOCUS_CHANGE_MASK |
                           GDK_KEY_PRESS_MASK |
                           GDK_KEY_RELEASE_MASK |
                           GDK_ENTER_NOTIFY_MASK |
                           GDK_LEAVE_NOTIFY_MASK |
                           GDK_EXPOSURE_MASK);

  priv->event_window = gdk_window_new (window,
                                       &attributes, attributes_mask);
  gtk_widget_register_window (widget, priv->event_window);
}

static void
wakefield_compositor_unrealize (GtkWidget *widget)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

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
wakefield_compositor_size_allocate (GtkWidget *widget,
                                    GtkAllocation *allocation)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

  gtk_widget_set_allocation (widget, allocation);

  if (gtk_widget_get_realized (widget))
    gdk_window_move_resize (priv->event_window,
                            allocation->x,
                            allocation->y,
                            allocation->width,
                            allocation->height);
}

static gboolean
wakefield_compositor_draw (GtkWidget *widget,
                           cairo_t   *cr)
{
#if 0
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);

  if (priv->surface)
    draw_surface (cr, priv->surface);

#endif

  return TRUE;
}

#if 0

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
broadcast_button (GtkWidget      *widget,
                  GdkEventButton *event)
{
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *resource;
  uint32_t serial = wl_display_next_serial (priv->wl_display);
  uint32_t button;

  button = convert_gdk_button_to_libinput (event->button);

  wl_resource_for_each (resource, &priv->seat.pointer.resource_list)
    {
      wl_pointer_send_button (resource, serial,
                              event->time,
                              button,
                              (event->type == GDK_BUTTON_PRESS ? 1 : 0));
    }
}

#endif

static gboolean
wakefield_compositor_button_press_event (GtkWidget      *widget,
                                         GdkEventButton *event)
{
  //broadcast_button (widget, event);
  return TRUE;
}

static gboolean
wakefield_compositor_button_release_event (GtkWidget      *widget,
                                           GdkEventButton *event)
{
  //broadcast_button (widget, event);
  return TRUE;
}

static gboolean
wakefield_compositor_motion_notify_event (GtkWidget      *widget,
                                          GdkEventMotion *event)
{
#if 0
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *resource;

  wl_resource_for_each (resource, &priv->seat.pointer.resource_list)
    {
      wl_pointer_send_motion (resource,
                              event->time,
                              wl_fixed_from_int (event->x),
                              wl_fixed_from_int (event->y));
    }
#endif

  return FALSE;
}

static gboolean
wakefield_compositor_enter_notify_event (GtkWidget        *widget,
                                         GdkEventCrossing *event)
{
#if 0
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *resource;
  uint32_t serial = wl_display_next_serial (priv->wl_display);

  wl_resource_for_each (resource, &priv->seat.pointer.resource_list)
    {
      wl_pointer_send_enter (resource, serial,
                             priv->surface->resource,
                             wl_fixed_from_int (event->x),
                             wl_fixed_from_int (event->y));
    }
#endif

  return FALSE;
}

static gboolean
wakefield_compositor_leave_notify_event (GtkWidget        *widget,
                                         GdkEventCrossing *event)
{
#if 0
  WakefieldCompositor *compositor = WAKEFIELD_COMPOSITOR (widget);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *resource;
  uint32_t serial = wl_display_next_serial (priv->wl_display);

  wl_resource_for_each (resource, &priv->seat.pointer.resource_list)
    {
      wl_pointer_send_leave (resource, serial, priv->surface->resource);
    }
#endif

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
  g_warning ("TODO: Set cursor\n");
}

static const struct wl_pointer_interface pointer_interface = {
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
  wl_resource_set_implementation (cr, &pointer_interface, pointer, unbind_resource);
  wl_list_insert (&pointer->resource_list, wl_resource_get_link (cr));
}

static void
wakefield_pointer_init (struct WakefieldPointer *pointer)
{
  wl_list_init (&pointer->resource_list);
  pointer->cursor_surface = NULL;
}

#define SEAT_VERSION 4

static const struct wl_seat_interface seat_interface = {
  seat_get_pointer,
  NULL, /* get_keyboard */
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
  wl_seat_send_capabilities (cr, WL_SEAT_CAPABILITY_POINTER);
  wl_seat_send_name (cr, "seat0");
}

static void
wakefield_seat_init (struct WakefieldSeat *seat,
                     struct wl_display    *wl_display)
{
  wakefield_pointer_init (&seat->pointer);
  /* wakefield_keyboard_init (&seat->keyboard); */

  wl_global_create (wl_display, &wl_seat_interface, SEAT_VERSION, seat, bind_seat);
}

static void
wl_output_destructor (struct wl_resource *resource)
{
}

static void
bind_output (struct wl_client *client,
             void *data,
             uint32_t version,
             uint32_t id)
{
  struct wl_resource *cr;

  cr = wl_resource_create (client, &wl_output_interface, version, id);
  wl_resource_set_destructor (cr, wl_output_destructor);
  /* wl_list_insert (&priv->output_resources, cr); */
  wl_output_send_scale (cr, 1);
}

#define WL_OUTPUT_VERSION 2

static void
wakefield_output_init (WakefieldCompositor *compositor)
{
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  /* wl_list_init (&priv->output_resources); */
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

static void
wl_compositor_create_surface (struct wl_client *client,
                              struct wl_resource *compositor_resource,
                              uint32_t id)
{
  WakefieldCompositor *compositor = wl_resource_get_user_data (compositor_resource);
  WakefieldCompositorPrivate *priv = wakefield_compositor_get_instance_private (compositor);
  struct wl_resource *surface;

  surface = wakefield_surface_new (client, compositor_resource, id);
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
  if (version > 1)
    {
      wl_resource_post_error (resource, 1,
                              "xdg-shell:: version not implemented yet.");
      return;
    }
}

static void
xdg_get_xdg_surface (struct wl_client *client,
                     struct wl_resource *resource,
                     uint32_t id,
                     struct wl_resource *surface_resource)
{
  wl_resource_post_error (resource, 1,
                          "xdg-shell:: get_surface not implemented yet.");
}

static void
xdg_get_xdg_popup (struct wl_client *client,
                   struct wl_resource *resource,
                   uint32_t id,
                   struct wl_resource *surface_resource,
                   struct wl_resource *parent_resource,
                   struct wl_resource *seat_resource,
                   uint32_t serial,
                   int32_t x, int32_t y)
{
  wl_resource_post_error (resource, 1,
                          "xdg-shell:: get_popup not implemented yet.");
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
  struct WakefieldShell *shell = &priv->shell;
  struct wl_resource *cr;

  cr = wl_resource_create (client,  &xdg_shell_interface, XDG_SHELL_VERSION, id);
  wl_resource_set_implementation (cr, &xdg_shell_interface, shell, unbind_resource);
  wl_list_insert (&shell->resource_list, wl_resource_get_link (cr));
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

  wakefield_seat_init (&priv->seat, priv->wl_display);
  wakefield_output_init (compositor);

  wl_list_init (&priv->surfaces);

  socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);

  /* XXX: For testing */
  wl_display_add_socket_auto (priv->wl_display);

  /* Attach the wl_event_loop to ours */
  g_source_attach (wayland_event_source_new (priv->wl_display), NULL);
}

static void
wakefield_compositor_class_init (WakefieldCompositorClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->realize = wakefield_compositor_realize;
  widget_class->unrealize = wakefield_compositor_unrealize;
  widget_class->map = wakefield_compositor_map;
  widget_class->unmap = wakefield_compositor_unmap;
  widget_class->size_allocate = wakefield_compositor_size_allocate;
  widget_class->draw = wakefield_compositor_draw;
  widget_class->enter_notify_event = wakefield_compositor_enter_notify_event;
  widget_class->leave_notify_event = wakefield_compositor_leave_notify_event;
  widget_class->button_press_event = wakefield_compositor_button_press_event;
  widget_class->button_release_event = wakefield_compositor_button_release_event;
  widget_class->motion_notify_event = wakefield_compositor_motion_notify_event;
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
