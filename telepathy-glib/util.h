/*
 * util.h - Headers for telepathy-glib utility functions
 *
 * Copyright (C) 2006-2007 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2006-2007 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#include <glib-object.h>

#ifndef __TP_UTIL_H__
#define __TP_UTIL_H__

G_BEGIN_DECLS

gboolean tp_g_ptr_array_contains (GPtrArray *haystack, gpointer needle);

GValue *tp_g_value_slice_new (GType type);

void tp_g_value_slice_free (GValue *value);

GValue *tp_g_value_slice_dup (const GValue *value);

void tp_g_hash_table_update (GHashTable *target, GHashTable *source,
    GBoxedCopyFunc key_dup, GBoxedCopyFunc value_dup);

gboolean tp_strdiff (const gchar *left, const gchar *right);

gpointer tp_mixin_offset_cast (gpointer instance, guint offset);

gchar *tp_escape_as_identifier (const gchar *name);

gboolean tp_asv_get_boolean (const GHashTable *asv, const gchar *key,
    gboolean *valid);
const GArray *tp_asv_get_bytes (const GHashTable *asv, const gchar *key);
const gchar *tp_asv_get_string (const GHashTable *asv, const gchar *key);
guint32 tp_asv_get_uint32 (const GHashTable *asv, const gchar *key,
    gboolean *valid);
const GValue *tp_asv_lookup (const GHashTable *asv, const gchar *key);

G_END_DECLS

#endif /* __TP_UTIL_H__ */
