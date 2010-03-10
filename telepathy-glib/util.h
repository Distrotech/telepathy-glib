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

#ifndef __TP_UTIL_H__
#define __TP_UTIL_H__
#define __TP_IN_UTIL_H__

#include <glib-object.h>

#include <telepathy-glib/verify.h>

#define tp_verify_statement(R) ((void) tp_verify_true (R))

G_BEGIN_DECLS

gboolean tp_g_ptr_array_contains (GPtrArray *haystack, gpointer needle);

GValue *tp_g_value_slice_new (GType type) G_GNUC_WARN_UNUSED_RESULT;

GValue *tp_g_value_slice_new_boolean (gboolean b) G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_int (gint n) G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_int64 (gint64 n) G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_byte (guchar n) G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_uint (guint n) G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_uint64 (guint64 n) G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_double (double d) G_GNUC_WARN_UNUSED_RESULT;

GValue *tp_g_value_slice_new_string (const gchar *string)
  G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_static_string (const gchar *string)
  G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_take_string (gchar *string)
  G_GNUC_WARN_UNUSED_RESULT;

GValue *tp_g_value_slice_new_boxed (GType type, gconstpointer p)
  G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_static_boxed (GType type, gconstpointer p)
  G_GNUC_WARN_UNUSED_RESULT;
GValue *tp_g_value_slice_new_take_boxed (GType type, gpointer p)
  G_GNUC_WARN_UNUSED_RESULT;

void tp_g_value_slice_free (GValue *value);

GValue *tp_g_value_slice_dup (const GValue *value) G_GNUC_WARN_UNUSED_RESULT;

void tp_g_hash_table_update (GHashTable *target, GHashTable *source,
    GBoxedCopyFunc key_dup, GBoxedCopyFunc value_dup);

gboolean tp_strdiff (const gchar *left, const gchar *right);

gpointer tp_mixin_offset_cast (gpointer instance, guint offset);
guint tp_mixin_instance_get_offset (gpointer instance, GQuark quark);
guint tp_mixin_class_get_offset (gpointer klass, GQuark quark);

gchar *tp_escape_as_identifier (const gchar *name) G_GNUC_WARN_UNUSED_RESULT;

gboolean tp_strv_contains (const gchar * const *strv, const gchar *str);

gint64 tp_g_key_file_get_int64 (GKeyFile *key_file, const gchar *group_name,
    const gchar *key, GError **error);
guint64 tp_g_key_file_get_uint64 (GKeyFile *key_file, const gchar *group_name,
    const gchar *key, GError **error);

gulong tp_g_signal_connect_object (gpointer instance,
    const gchar *detailed_signal, GCallback c_handler, gpointer gobject,
    GConnectFlags connect_flags);

GValueArray *tp_value_array_build (gsize length,
  GType type,
  ...) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#undef  __TP_IN_UTIL_H__
#endif /* __TP_UTIL_H__ */
