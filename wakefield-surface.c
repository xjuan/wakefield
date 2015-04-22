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

#include <sys/time.h>

#include "wakefield-private.h"
#include "xdg-shell-server-protocol.h"

struct WakefieldSurfacePendingState
{
  struct wl_resource *buffer;
  int scale;

  cairo_region_t *input_region;
  struct wl_list frame_callbacks;
};

struct WakefieldSurface
{
  WakefieldCompositor *compositor;
  struct wl_resource *resource;

  struct WakefieldXdgSurface *xdg_surface;

  cairo_region_t *damage;
  struct WakefieldSurfacePendingState pending, current;
};

struct WakefieldXdgSurface
{
  struct WakefieldSurface *surface;

  struct wl_resource *resource;
};

static cairo_format_t
cairo_format_for_wl_shm_format (enum wl_shm_format format)
{
  switch (format)
    {
    case WL_SHM_FORMAT_ARGB8888:
      return CAIRO_FORMAT_ARGB32;
    case WL_SHM_FORMAT_XRGB8888:
      return CAIRO_FORMAT_RGB24;
    default:
      g_assert_not_reached ();
    }
}

static uint32_t
get_time (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void
wakefield_surface_draw (struct wl_resource *surface_resource,
                        cairo_t                 *cr)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (surface_resource);
  struct wl_shm_buffer *shm_buffer;

  shm_buffer = wl_shm_buffer_get (surface->current.buffer);
  if (shm_buffer)
    {
      cairo_surface_t *cr_surface;

      wl_shm_buffer_begin_access (shm_buffer);

      cr_surface = cairo_image_surface_create_for_data (wl_shm_buffer_get_data (shm_buffer),
                                                        cairo_format_for_wl_shm_format (wl_shm_buffer_get_format (shm_buffer)),
                                                        wl_shm_buffer_get_width (shm_buffer),
                                                        wl_shm_buffer_get_height (shm_buffer),
                                                        wl_shm_buffer_get_stride (shm_buffer));
      cairo_surface_set_device_scale (cr_surface, surface->current.scale, surface->current.scale);

      cairo_set_source_surface (cr, cr_surface, 0, 0);

      /* XXX: Do scaling of our surface to match our allocation. */
      cairo_paint (cr);

      cairo_surface_destroy (cr_surface);

      wl_shm_buffer_end_access (shm_buffer);
    }

  /* Trigger frame callbacks. */
  {
    struct wl_resource *cr, *next;
    /* XXX: Should we use the frame clock for this? */
    uint32_t time = get_time ();

    wl_resource_for_each_safe (cr, next, &surface->current.frame_callbacks)
      {
        wl_callback_send_done (cr, time);
        wl_resource_destroy (cr);
      }

    wl_list_init (&surface->current.frame_callbacks);
  }
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
wl_surface_attach (struct wl_client *client,
                   struct wl_resource *surface_resource,
                   struct wl_resource *buffer_resource,
                   gint32 dx, gint32 dy)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (surface_resource);

  /* Ignore dx/dy in our case */
  surface->pending.buffer = buffer_resource;
}

static void
wl_surface_damage (struct wl_client *client,
                   struct wl_resource *surface_resource,
                   int32_t x, int32_t y, int32_t width, int32_t height)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (surface_resource);
  cairo_rectangle_int_t rectangle = { x, y, width, height };
  cairo_region_union_rectangle (surface->damage, &rectangle);
}

#define WL_CALLBACK_VERSION 1

static void
wl_surface_frame (struct wl_client *client,
                  struct wl_resource *surface_resource,
                  uint32_t callback_id)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (surface_resource);

  struct wl_resource *callback = wl_resource_create (client, &wl_callback_interface,
                                                     WL_CALLBACK_VERSION, callback_id);
  wl_resource_set_destructor (callback, unbind_resource);
  wl_list_insert (&surface->pending.frame_callbacks, wl_resource_get_link (callback));
}

static void
wl_surface_set_opaque_region (struct wl_client *client,
                              struct wl_resource *surface_resource,
                              struct wl_resource *region_resource)
{
  /* XXX: Do we need this? */
}

static void
wl_surface_set_input_region (struct wl_client *client,
                             struct wl_resource *surface_resource,
                             struct wl_resource *region_resource)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (surface_resource);
  g_clear_pointer (&surface->pending.input_region, cairo_region_destroy);
  if (region_resource)
    {
      surface->pending.input_region = wakefield_region_get_region (region_resource);
    }
}

static void
wl_surface_commit (struct wl_client *client,
                   struct wl_resource *resource)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (resource);
  struct wl_shm_buffer *shm_buffer;
  cairo_region_t *clear_region = NULL;
  cairo_rectangle_int_t rect = { 0, };

  if (surface->current.buffer)
    {
      shm_buffer = wl_shm_buffer_get (surface->current.buffer);
      if (shm_buffer)
        {
          rect.width = wl_shm_buffer_get_width (shm_buffer) / surface->current.scale;
          rect.height = wl_shm_buffer_get_height (shm_buffer) / surface->current.scale;

          clear_region = cairo_region_create_rectangle (&rect);
        }
      wl_buffer_send_release (surface->current.buffer);
    }

  if (surface->pending.buffer)
    {
      shm_buffer = wl_shm_buffer_get (surface->pending.buffer);
      if (clear_region && shm_buffer)
        {
          rect.width = wl_shm_buffer_get_width (shm_buffer) / surface->pending.scale;
          rect.height = wl_shm_buffer_get_height (shm_buffer) / surface->pending.scale;

          cairo_region_subtract_rectangle (clear_region, &rect);
        }
      surface->current.buffer = surface->pending.buffer;
    }

  /* XXX: Should we reallocate / redraw the entire region if the buffer
   * scale changes? */
  if (surface->pending.scale > 0)
    surface->current.scale = surface->pending.scale;

  wl_list_insert_list (&surface->current.frame_callbacks,
                       &surface->pending.frame_callbacks);
  wl_list_init (&surface->pending.frame_callbacks);

  if (clear_region)
    {
      cairo_region_union (surface->damage, clear_region);
      cairo_region_destroy (clear_region);
    }

  /* process damage */
  gtk_widget_queue_draw_region (GTK_WIDGET (surface->compositor), surface->damage);

  /* ... and then empty it */
  {
    cairo_rectangle_int_t nothing = { 0, 0, 0, 0 };
    cairo_region_intersect_rectangle (surface->damage, &nothing);
  }

  /* XXX: Stop leak when we start using the input region. */
  surface->pending.input_region = NULL;

  surface->pending.buffer = NULL;
  surface->pending.scale = 1;
}

static void
wl_surface_set_buffer_transform (struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t transform)
{
  /* TODO */
}

static void
wl_surface_set_buffer_scale (struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t scale)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (resource);
  surface->pending.scale = scale;
}

static void
destroy_pending_state (struct WakefieldSurfacePendingState *state)
{
  struct wl_resource *cr, *next;
  wl_resource_for_each_safe (cr, next, &state->frame_callbacks)
    wl_resource_destroy (cr);
  g_clear_pointer (&state->input_region, cairo_region_destroy);
}

static void
wl_surface_finalize (struct wl_resource *resource)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (resource);

  if (surface->xdg_surface)
    surface->xdg_surface->surface = NULL;

  wl_list_remove (wl_resource_get_link (resource));

  wakefield_compositor_surface_destroyed (surface->compositor, surface->resource);

  destroy_pending_state (&surface->pending);
  destroy_pending_state (&surface->current);

  g_slice_free (struct WakefieldSurface, surface);
}

static const struct wl_surface_interface surface_implementation = {
  resource_release,
  wl_surface_attach,
  wl_surface_damage,
  wl_surface_frame,
  wl_surface_set_opaque_region,
  wl_surface_set_input_region,
  wl_surface_commit,
  wl_surface_set_buffer_transform,
  wl_surface_set_buffer_scale
};

struct wl_resource *
wakefield_surface_new (WakefieldCompositor *compositor,
                       struct wl_client *client,
                       struct wl_resource *compositor_resource,
                       uint32_t id)
{
  struct WakefieldSurface *surface;

  surface = g_slice_new0 (struct WakefieldSurface);
  surface->compositor = compositor;
  surface->damage = cairo_region_create ();

  surface->resource = wl_resource_create (client, &wl_surface_interface, wl_resource_get_version (compositor_resource), id);
  wl_resource_set_implementation (surface->resource, &surface_implementation, surface, wl_surface_finalize);

  wl_list_init (&surface->pending.frame_callbacks);
  wl_list_init (&surface->current.frame_callbacks);

  surface->current.scale = 1;
  surface->pending.scale = 1;

  return surface->resource;
}

static void
xdg_surface_finalize (struct wl_resource *xdg_resource)
{
  struct WakefieldXdgSurface *xdg_surface = wl_resource_get_user_data (xdg_resource);

  wl_list_remove (wl_resource_get_link (xdg_resource));

  if (xdg_surface->surface)
    xdg_surface->surface->xdg_surface = NULL;

  g_slice_free (struct WakefieldXdgSurface, xdg_surface);
}

static void
xdg_surface_destroy (struct wl_client *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_surface_set_parent (struct wl_client *client,
                        struct wl_resource *resource,
                        struct wl_resource *parent_resource)
{
}

static void
xdg_surface_set_app_id (struct wl_client *client,
                        struct wl_resource *resource,
                        const char *app_id)
{
}

static void
xdg_surface_show_window_menu (struct wl_client *client,
                              struct wl_resource *surface_resource,
                              struct wl_resource *seat_resource,
                              uint32_t serial,
                              int32_t x,
                              int32_t y)
{
}

static void
xdg_surface_set_title (struct wl_client *client,
                       struct wl_resource *resource, const char *title)
{
}

static void
xdg_surface_move (struct wl_client *client, struct wl_resource *resource,
                  struct wl_resource *seat_resource, uint32_t serial)
{
}

static void
xdg_surface_resize (struct wl_client *client, struct wl_resource *resource,
                    struct wl_resource *seat_resource, uint32_t serial,
                    uint32_t edges)
{
}

static void
xdg_surface_ack_configure (struct wl_client *client,
                           struct wl_resource *resource,
                           uint32_t serial)
{
}

static void
xdg_surface_set_window_geometry (struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height)
{
}

static void
xdg_surface_set_maximized (struct wl_client *client,
                           struct wl_resource *resource)
{
}

static void
xdg_surface_unset_maximized (struct wl_client *client,
                             struct wl_resource *resource)
{
}

static void
xdg_surface_set_fullscreen (struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *output_resource)
{
}

static void
xdg_surface_unset_fullscreen (struct wl_client *client,
                              struct wl_resource *resource)
{
}

static void
xdg_surface_set_minimized (struct wl_client *client,
                           struct wl_resource *resource)
{
}

static const struct xdg_surface_interface xdg_surface_implementation = {
  xdg_surface_destroy,
  xdg_surface_set_parent,
  xdg_surface_set_title,
  xdg_surface_set_app_id,
  xdg_surface_show_window_menu,
  xdg_surface_move,
  xdg_surface_resize,
  xdg_surface_ack_configure,
  xdg_surface_set_window_geometry,
  xdg_surface_set_maximized,
  xdg_surface_unset_maximized,
  xdg_surface_set_fullscreen,
  xdg_surface_unset_fullscreen,
  xdg_surface_set_minimized,
};

struct wl_resource *
wakefield_xdg_surface_get_surface (struct wl_resource *xdg_surface_resource)
{
  struct WakefieldXdgSurface *xdg_surface = wl_resource_get_user_data (xdg_surface_resource);

  if (xdg_surface->surface)
    return xdg_surface->surface->resource;

  return NULL;
}

struct wl_resource *
wakefield_xdg_surface_new (struct wl_client *client,
                           struct wl_resource *shell_resource,
                           uint32_t id,
                           struct wl_resource *surface_resource)
{
  struct WakefieldSurface *surface = wl_resource_get_user_data (surface_resource);
  struct WakefieldXdgSurface *xdg_surface;

  xdg_surface = g_slice_new0 (struct WakefieldXdgSurface);
  xdg_surface->surface = surface;

  surface->xdg_surface = xdg_surface;

  xdg_surface->resource = wl_resource_create (client, &xdg_surface_interface, wl_resource_get_version (shell_resource), id);
  wl_resource_set_implementation (xdg_surface->resource, &xdg_surface_implementation, xdg_surface, xdg_surface_finalize);

  return xdg_surface->resource;
}
