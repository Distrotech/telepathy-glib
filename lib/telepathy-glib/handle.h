/*
 * tp-handle.h - Header for basic Telepathy-GLib handle functionality
 *
 * Copyright (C) 2005, 2007 Collabora Ltd.
 * Copyright (C) 2005, 2007 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __TP_HANDLE_H__
#define __TP_HANDLE_H__

#include <glib.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>

G_BEGIN_DECLS

/**
 * TpHandle:
 *
 * Type representing Telepathy handles within telepathy-glib.
 *
 * This is guint despite the wire protocol having 32-bit integers, because
 * dbus-glib expects GArrays of guint and so on. If the dbus-glib ABI changes
 * in future, telepathy-glib is likely to have a matching ABI change.
 */
typedef guint TpHandle;

/**
 * TP_TYPE_HANDLE:
 *
 * The GType of a TpHandle, currently G_TYPE_UINT.
 *
 * This won't change unless in an ABI-incompatible version of telepathy-glib.
 */
#define TP_TYPE_HANDLE G_TYPE_UINT

/* Must be static inline because it references NUM_TP_HANDLE_TYPES -
 * if it wasn't inlined, a newer libtelepathy-glib with a larger number
 * of handle types might accept handle types that won't fit in the
 * connection manager's array of length NUM_TP_HANDLE_TYPES
 */

/**
 * tp_handle_type_is_valid:
 * @type: A handle type, valid or not, to be checked
 * @error: Set if the handle type is invalid
 *
 * If the given handle type is valid, return %TRUE. If not, set @error
 * and return %FALSE.
 *
 * Returns: %TRUE if the handle type is valid.
 */
static inline
/* spacer so gtkdoc documents this function as though not static */
gboolean tp_handle_type_is_valid (TpHandleType type, GError **error);

static inline gboolean
tp_handle_type_is_valid (TpHandleType type, GError **error)
{
  if (type > TP_HANDLE_TYPE_NONE && type < NUM_TP_HANDLE_TYPES)
    return TRUE;

  tp_g_set_error_invalid_handle_type (type, error);
  return FALSE;
}

G_END_DECLS

#endif
