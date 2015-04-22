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

#include "wakefield-compositor.h"

#include <wayland-server.h>

struct wl_display *wakefield_compositor_get_display (WakefieldCompositor *compositor);

struct wl_resource *wakefield_surface_new  (WakefieldCompositor *compositor,
                                            struct wl_client    *client,
                                            struct wl_resource  *compositor_resource,
                                            uint32_t             id);
void                wakefield_surface_draw (struct wl_resource  *some_surface_resource,
                                            cairo_t             *cr);

struct wl_resource *wakefield_xdg_surface_new (struct wl_client   *client,
                                               struct wl_resource *shell_resource,
                                               uint32_t            id,
                                               struct wl_resource *surface_resource);

cairo_region_t *wakefield_region_get_region (struct wl_resource *region_resource);

struct WakefieldDataDevice *wakefield_data_device_new (WakefieldCompositor *compositor);
