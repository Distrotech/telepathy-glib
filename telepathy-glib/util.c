/*
 * util.c - Source for telepathy-glib utility functions
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

/**
 * SECTION:util
 * @title: Utilities
 * @short_description: Non-Telepathy utility functions
 *
 * Some utility functions used in telepathy-glib which could have been in
 * GLib, but aren't.
 */

#include <telepathy-glib/util-internal.h>
#include <telepathy-glib/util.h>

#include <string.h>

/**
 * tp_verify:
 * @R: a requirement (constant expression) to be checked at compile-time
 *
 * Make an assertion at compile time, like C++0x's proposed static_assert
 * keyword. If @R is determined to be true, there is no overhead at runtime;
 * if @R is determined to be false, compilation will fail.
 *
 * This macro can be used at file scope (it expands to a dummy extern
 * declaration).
 *
 * (This is gnulib's verify macro, written by Paul Eggert, Bruno Haible and
 * Jim Meyering.)
 */

/**
 * tp_verify_true:
 * @R: a requirement (constant expression) to be checked at compile-time
 *
 * Make an assertion at compile time, like C++0x's proposed static_assert
 * keyword. If @R is determined to be true, there is no overhead at runtime,
 * and the macro evaluates to 1 as an integer constant expression;
 * if @R is determined to be false, compilation will fail.
 *
 * This macro can be used anywhere that an integer constant expression would
 * be allowed.
 *
 * (This is gnulib's verify_true macro, written by Paul Eggert, Bruno Haible
 * and Jim Meyering.)
 *
 * Returns: 1
 */

/**
 * tp_verify_statement:
 * @R: a requirement (constant expression) to be checked at compile-time
 *
 * Make an assertion at compile time, like C++0x's proposed static_assert
 * keyword. If @R is determined to be true, there is no overhead at runtime;
 * if @R is determined to be false, compilation will fail.
 *
 * This macro can be used anywhere that a statement would be allowed; it
 * is equivalent to ((void) tp_verify_true (R)).
 */

/**
 * tp_g_ptr_array_contains:
 * @haystack: The pointer array to be searched
 * @needle: The pointer to look for
 *
 * <!--no further documentation needed-->
 *
 * Returns: %TRUE if @needle is one of the elements of @haystack
 */

gboolean
tp_g_ptr_array_contains (GPtrArray *haystack, gpointer needle)
{
  guint i;

  for (i = 0; i < haystack->len; i++)
    {
      if (g_ptr_array_index (haystack, i) == needle)
        return TRUE;
    }

  return FALSE;
}


/**
 * tp_g_value_slice_new:
 * @type: The type desired for the new GValue
 *
 * Slice-allocate an empty #GValue. tp_g_value_slice_new_boolean() and similar
 * functions are likely to be more convenient to use for the types supported.
 *
 * Returns: a newly allocated, newly initialized #GValue, to be freed with
 * tp_g_value_slice_free() or g_slice_free().
 * Since: 0.5.14
 */
GValue *
tp_g_value_slice_new (GType type)
{
  GValue *ret = g_slice_new0 (GValue);

  g_value_init (ret, type);
  return ret;
}

/**
 * tp_g_value_slice_new_boolean:
 * @b: a boolean value
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_BOOLEAN with value @b, to be freed with
 * tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_boolean (gboolean b)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_BOOLEAN);

  g_value_set_boolean (v, b);
  return v;
}

/**
 * tp_g_value_slice_new_int:
 * @n: an integer
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_INT with value @n, to be freed with
 * tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_int (gint n)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_INT);

  g_value_set_int (v, n);
  return v;
}

/**
 * tp_g_value_slice_new_int64:
 * @n: a 64-bit integer
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_INT64 with value @n, to be freed with
 * tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_int64 (gint64 n)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_INT64);

  g_value_set_int64 (v, n);
  return v;
}

/**
 * tp_g_value_slice_new_uint:
 * @n: an unsigned integer
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_UINT with value @n, to be freed with
 * tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_uint (guint n)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_UINT);

  g_value_set_uint (v, n);
  return v;
}

/**
 * tp_g_value_slice_new_uint64:
 * @n: a 64-bit unsigned integer
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_UINT64 with value @n, to be freed with
 * tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_uint64 (guint64 n)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_UINT64);

  g_value_set_uint64 (v, n);
  return v;
}

/**
 * tp_g_value_slice_new_double:
 * @d: a number
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_DOUBLE with value @n, to be freed with
 * tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_double (double n)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_DOUBLE);

  g_value_set_double (v, n);
  return v;
}

/**
 * tp_g_value_slice_new_string:
 * @string: a string to be copied into the value
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_STRING whose value is a copy of @string,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_string (const gchar *string)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_STRING);

  g_value_set_string (v, string);
  return v;
}

/**
 * tp_g_value_slice_new_static_string:
 * @string: a static string which must remain valid forever, to be pointed to
 *  by the value
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_STRING whose value is @string,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_static_string (const gchar *string)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_STRING);

  g_value_set_static_string (v, string);
  return v;
}

/**
 * tp_g_value_slice_new_take_string:
 * @string: a string which will be freed with g_free() by the returned #GValue
 *  (the caller must own it before calling this function, but no longer owns
 *  it after this function returns)
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %G_TYPE_STRING whose value is @string,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_take_string (gchar *string)
{
  GValue *v = tp_g_value_slice_new (G_TYPE_STRING);

  g_value_take_string (v, string);
  return v;
}

/**
 * tp_g_value_slice_new_boxed:
 * @type: a boxed type
 * @p: a pointer of type @type, which will be copied
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type @type whose value is a copy of @p,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_boxed (GType type,
                            gconstpointer p)
{
  GValue *v;

  g_return_val_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED, NULL);
  v = tp_g_value_slice_new (type);
  g_value_set_boxed (v, p);
  return v;
}

/**
 * tp_g_value_slice_new_static_boxed:
 * @type: a boxed type
 * @p: a pointer of type @type, which must remain valid forever
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type @type whose value is @p,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_static_boxed (GType type,
                                   gconstpointer p)
{
  GValue *v;

  g_return_val_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED, NULL);
  v = tp_g_value_slice_new (type);
  g_value_set_static_boxed (v, p);
  return v;
}

/**
 * tp_g_value_slice_new_take_boxed:
 * @type: a boxed type
 * @p: a pointer of type @type which will be freed with g_boxed_free() by the
 *  returned #GValue (the caller must own it before calling this function, but
 *  no longer owns it after this function returns)
 *
 * Slice-allocate and initialize a #GValue. This function is convenient to
 * use when constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type @type whose value is @p,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_take_boxed (GType type,
                                 gpointer p)
{
  GValue *v;

  g_return_val_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED, NULL);
  v = tp_g_value_slice_new (type);
  g_value_take_boxed (v, p);
  return v;
}

/**
 * tp_g_value_slice_free:
 * @value: A GValue which was allocated with the g_slice API
 *
 * Unset and free a slice-allocated GValue.
 *
 * <literal>(GDestroyNotify) tp_g_value_slice_free</literal> can be used
 * as a destructor for values in a #GHashTable, for example.
 */

void
tp_g_value_slice_free (GValue *value)
{
  g_value_unset (value);
  g_slice_free (GValue, value);
}


/**
 * tp_g_value_slice_dup:
 * @value: A GValue
 *
 * <!-- 'Returns' says it all -->
 *
 * Returns: a newly allocated copy of @value, to be freed with
 * tp_g_value_slice_free() or g_slice_free().
 * Since: 0.5.14
 */
GValue *
tp_g_value_slice_dup (const GValue *value)
{
  GValue *ret = tp_g_value_slice_new (G_VALUE_TYPE (value));

  g_value_copy (value, ret);
  return ret;
}


struct _tp_g_hash_table_update
{
  GHashTable *target;
  GBoxedCopyFunc key_dup, value_dup;
};

static void
_tp_g_hash_table_update_helper (gpointer key,
                                gpointer value,
                                gpointer user_data)
{
  struct _tp_g_hash_table_update *data = user_data;
  gpointer new_key = (data->key_dup != NULL) ? (data->key_dup) (key) : key;
  gpointer new_value = (data->value_dup != NULL) ? (data->value_dup) (value)
                                                 : value;

  g_hash_table_replace (data->target, new_key, new_value);
}

/**
 * tp_g_hash_table_update:
 * @target: The hash table to be updated
 * @source: The hash table to update it with (read-only)
 * @key_dup: function to duplicate a key from @source so it can be be stored
 *           in @target. If NULL, the key is not copied, but is used as-is
 * @value_dup: function to duplicate a value from @source so it can be stored
 *             in @target. If NULL, the value is not copied, but is used as-is
 *
 * Add each item in @source to @target, replacing any existing item with the
 * same key. @key_dup and @value_dup are used to duplicate the items; in
 * principle they could also be used to convert between types.
 *
 * Since: 0.7.0
 */
void
tp_g_hash_table_update (GHashTable *target,
                        GHashTable *source,
                        GBoxedCopyFunc key_dup,
                        GBoxedCopyFunc value_dup)
{
  struct _tp_g_hash_table_update data = { target, key_dup,
      value_dup };

  g_return_if_fail (target != NULL);
  g_return_if_fail (source != NULL);
  g_return_if_fail (target != source);

  g_hash_table_foreach (source, _tp_g_hash_table_update_helper, &data);
}


/**
 * tp_strdiff:
 * @left: The first string to compare (may be NULL)
 * @right: The second string to compare (may be NULL)
 *
 * Return %TRUE if the given strings are different. Unlike #strcmp this
 * function will handle null pointers, treating them as distinct from any
 * string.
 *
 * Returns: %FALSE if @left and @right are both %NULL, or if
 *          neither is %NULL and both have the same contents; %TRUE otherwise
 */

gboolean
tp_strdiff (const gchar *left, const gchar *right)
{
  if ((NULL == left) != (NULL == right))
    return TRUE;

  else if (left == right)
    return FALSE;

  else
    return (0 != strcmp (left, right));
}



/**
 * tp_mixin_offset_cast:
 * @instance: A pointer to a structure
 * @offset: The offset of a structure member in bytes, which must not be 0
 *
 * Extend a pointer by an offset, provided the offset is not 0.
 * This is used to cast from an object instance to one of the telepathy-glib
 * mixin classes.
 *
 * Returns: a pointer @offset bytes beyond @instance
 */
gpointer
tp_mixin_offset_cast (gpointer instance, guint offset)
{
  g_return_val_if_fail (offset != 0, NULL);

  return ((guchar *) instance + offset);
}


/**
 * tp_mixin_instance_get_offset:
 * @instance: A pointer to a GObject-derived instance structure
 * @quark: A quark that was used to store the offset with g_type_set_qdata()
 *
 * If the type of @instance, or any of its ancestor types, has had an offset
 * attached using qdata with the given @quark, return that offset. If not,
 * this indicates a programming error and results are undefined.
 *
 * This is used to implement the telepathy-glib mixin classes.
 *
 * Returns: the offset of the mixin
 */
guint
tp_mixin_instance_get_offset (gpointer instance,
                              GQuark quark)
{
  GType t;

  for (t = G_OBJECT_TYPE (instance);
       t != 0;
       t = g_type_parent (t))
    {
      gpointer qdata = g_type_get_qdata (t, quark);

      if (qdata != NULL)
        return GPOINTER_TO_UINT (qdata);
    }

  g_return_val_if_reached (0);
}


/**
 * tp_mixin_class_get_offset:
 * @klass: A pointer to a GObjectClass-derived class structure
 * @quark: A quark that was used to store the offset with g_type_set_qdata()
 *
 * If the type of @klass, or any of its ancestor types, has had an offset
 * attached using qdata with the given @quark, return that offset. If not,
 * this indicates a programming error and results are undefined.
 *
 * This is used to implement the telepathy-glib mixin classes.
 *
 * Returns: the offset of the mixin class
 */
guint
tp_mixin_class_get_offset (gpointer klass,
                           GQuark quark)
{
  GType t;

  for (t = G_OBJECT_CLASS_TYPE (klass);
       t != 0;
       t = g_type_parent (t))
    {
      gpointer qdata = g_type_get_qdata (t, quark);

      if (qdata != NULL)
        return GPOINTER_TO_UINT (qdata);
    }

  g_return_val_if_reached (0);
}


static inline gboolean
_esc_ident_bad (gchar c, gboolean is_first)
{
  return ((c < 'a' || c > 'z') &&
          (c < 'A' || c > 'Z') &&
          (c < '0' || c > '9' || is_first));
}


/**
 * tp_escape_as_identifier:
 * @name: The string to be escaped
 *
 * Escape an arbitrary string so it follows the rules for a C identifier,
 * and hence an object path component, interface element component,
 * bus name component or member name in D-Bus.
 *
 * Unlike g_strcanon this is a reversible encoding, so it preserves
 * distinctness.
 *
 * The escaping consists of replacing all non-alphanumerics, and the first
 * character if it's a digit, with an underscore and two lower-case hex
 * digits:
 *
 *    "0123abc_xyz\x01\xff" -> _30123abc_5fxyz_01_ff
 *
 * i.e. similar to URI encoding, but with _ taking the role of %, and a
 * smaller allowed set. As a special case, "" is escaped to "_" (just for
 * completeness, really).
 *
 * Returns: the escaped string, which must be freed by the caller with #g_free
 */
gchar *
tp_escape_as_identifier (const gchar *name)
{
  gboolean bad = FALSE;
  size_t len = 0;
  GString *op;
  const gchar *ptr, *first_ok;

  g_return_val_if_fail (name != NULL, NULL);

  /* fast path for empty name */
  if (name[0] == '\0')
    return g_strdup ("_");

  for (ptr = name; *ptr; ptr++)
    {
      if (_esc_ident_bad (*ptr, ptr == name))
        {
          bad = TRUE;
          len += 3;
        }
      else
        len++;
    }

  /* fast path if it's clean */
  if (!bad)
    return g_strdup (name);

  /* If strictly less than ptr, first_ok is the first uncopied safe character.
   */
  first_ok = name;
  op = g_string_sized_new (len);
  for (ptr = name; *ptr; ptr++)
    {
      if (_esc_ident_bad (*ptr, ptr == name))
        {
          /* copy preceding safe characters if any */
          if (first_ok < ptr)
            {
              g_string_append_len (op, first_ok, ptr - first_ok);
            }
          /* escape the unsafe character */
          g_string_append_printf (op, "_%02x", (unsigned char)(*ptr));
          /* restart after it */
          first_ok = ptr + 1;
        }
    }
  /* copy trailing safe characters if any */
  if (first_ok < ptr)
    {
      g_string_append_len (op, first_ok, ptr - first_ok);
    }
  return g_string_free (op, FALSE);
}


/**
 * tp_strv_contains:
 * @strv: a NULL-terminated array of strings, or %NULL (which is treated as an
 *        empty strv)
 * @str: a non-NULL string
 *
 * <!-- -->
 * Returns: TRUE if @str is an element of @strv, according to strcmp().
 *
 * Since: 0.7.15
 */
gboolean
tp_strv_contains (const gchar * const *strv,
                  const gchar *str)
{
  g_return_val_if_fail (str != NULL, FALSE);

  if (strv == NULL)
    return FALSE;

  while (*strv != NULL)
    {
      if (!tp_strdiff (str, *strv))
        return TRUE;
      strv++;
    }

  return FALSE;
}

/**
 * tp_g_key_file_get_int64:
 * @key_file: a non-%NULL #GKeyFile
 * @group_name: a non-%NULL group name
 * @key: a non-%NULL key
 * @error: return location for a #GError
 *
 * Returns the value associated with @key under @group_name as a signed
 * 64-bit integer. This is similar to g_key_file_get_integer() but can return
 * 64-bit results without truncation.
 *
 * Returns: the value associated with the key as a signed 64-bit integer, or
 * 0 if the key was not found or could not be parsed.
 *
 * Since: 0.7.31
 */
gint64
tp_g_key_file_get_int64 (GKeyFile *key_file,
                         const gchar *group_name,
                         const gchar *key,
                         GError **error)
{
  gchar *s, *end;
  gint64 v;

  g_return_val_if_fail (key_file != NULL, -1);
  g_return_val_if_fail (group_name != NULL, -1);
  g_return_val_if_fail (key != NULL, -1);

  s = g_key_file_get_value (key_file, group_name, key, error);

  if (s == NULL)
    return 0;

  v = g_ascii_strtoll (s, &end, 10);

  if (*s == '\0' || *end != '\0')
    {
      g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
          "Key '%s' in group '%s' has value '%s' where int64 was expected",
          key, group_name, s);
      return 0;
    }

  g_free (s);
  return v;
}

/**
 * tp_g_key_file_get_uint64:
 * @key_file: a non-%NULL #GKeyFile
 * @group_name: a non-%NULL group name
 * @key: a non-%NULL key
 * @error: return location for a #GError
 *
 * Returns the value associated with @key under @group_name as an unsigned
 * 64-bit integer. This is similar to g_key_file_get_integer() but can return
 * large positive results without truncation.
 *
 * Returns: the value associated with the key as an unsigned 64-bit integer,
 * or 0 if the key was not found or could not be parsed.
 *
 * Since: 0.7.31
 */
guint64
tp_g_key_file_get_uint64 (GKeyFile *key_file,
                          const gchar *group_name,
                          const gchar *key,
                          GError **error)
{
  gchar *s, *end;
  guint64 v;

  g_return_val_if_fail (key_file != NULL, -1);
  g_return_val_if_fail (group_name != NULL, -1);
  g_return_val_if_fail (key != NULL, -1);

  s = g_key_file_get_value (key_file, group_name, key, error);

  if (s == NULL)
    return 0;

  v = g_ascii_strtoull (s, &end, 10);

  if (*s == '\0' || *end != '\0')
    {
      g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
          "Key '%s' in group '%s' has value '%s' where uint64 was expected",
          key, group_name, s);
      return 0;
    }

  g_free (s);
  return v;
}

typedef struct {
    GObject *instance;
    GObject *observer;
    GClosure *closure;
    gulong handler_id;
} WeakHandlerCtx;

static WeakHandlerCtx *
whc_new (GObject *instance,
         GObject *observer)
{
  WeakHandlerCtx *ctx = g_slice_new0 (WeakHandlerCtx);

  ctx->instance = instance;
  ctx->observer = observer;

  return ctx;
}

static void
whc_free (WeakHandlerCtx *ctx)
{
  g_slice_free (WeakHandlerCtx, ctx);
}

static void observer_destroyed_cb (gpointer, GObject *);
static void closure_invalidated_cb (gpointer, GClosure *);

/*
 * If signal handlers are removed before the object is destroyed, this
 * callback will never get triggered.
 */
static void
instance_destroyed_cb (gpointer ctx_,
    GObject *where_the_instance_was)
{
  WeakHandlerCtx *ctx = ctx_;

  /* No need to disconnect the signal here, the instance has gone away. */
  g_object_weak_unref (ctx->observer, observer_destroyed_cb, ctx);
  g_closure_remove_invalidate_notifier (ctx->closure, ctx,
      closure_invalidated_cb);
  whc_free (ctx);
}

/* Triggered when the observer is destroyed. */
static void
observer_destroyed_cb (gpointer ctx_,
    GObject *where_the_observer_was)
{
  WeakHandlerCtx *ctx = ctx_;

  g_closure_remove_invalidate_notifier (ctx->closure, ctx,
      closure_invalidated_cb);
  g_signal_handler_disconnect (ctx->instance, ctx->handler_id);
  g_object_weak_unref (ctx->instance, instance_destroyed_cb, ctx);
  whc_free (ctx);
}

/* Triggered when either object is destroyed or the handler is disconnected. */
static void
closure_invalidated_cb (gpointer ctx_,
    GClosure *where_the_closure_was)
{
  WeakHandlerCtx *ctx = ctx_;

  g_object_weak_unref (ctx->instance, instance_destroyed_cb, ctx);
  g_object_weak_unref (ctx->observer, observer_destroyed_cb, ctx);
  whc_free (ctx);
}

/**
 * tp_g_signal_connect_object:
 * @instance: the instance to connect to.
 * @detailed_signal: a string of the form "signal-name::detail".
 * @c_handler: the #GCallback to connect.
 * @gobject: the object to pass as data to @c_handler.
 * @connect_flags: a combination of #GConnnectFlags.
 *
 * Connects a #GCallback function to a signal for a particular object, as if
 * with g_signal_connect(). Additionally, arranges for the signal handler to be
 * disconnected if @observer is destroyed.
 *
 * This is similar to g_signal_connect_data(), but uses a closure which
 * ensures that the @gobject stays alive during the call to @c_handler
 * by temporarily adding a reference count to @gobject.
 *
 * This is similar to g_signal_connect_object(), but doesn't have the
 * documented bug that everyone is too scared to fix. Also, it does not allow
 * you to pass in NULL as @gobject
 *
 * This is intended to be a convenient way for objects to use themselves as
 * user_data for callbacks without having to explicitly disconnect all the
 * handlers in their finalizers.
 *
 * Returns: the handler id.
 */
gulong
tp_g_signal_connect_object (gpointer instance,
    const gchar *detailed_signal,
    GCallback c_handler,
    gpointer gobject,
    GConnectFlags connect_flags)
{
  GObject *instance_obj = G_OBJECT (instance);
  WeakHandlerCtx *ctx = whc_new (instance_obj, gobject);

  g_return_val_if_fail (G_TYPE_CHECK_INSTANCE (instance), 0);
  g_return_val_if_fail (detailed_signal != NULL, 0);
  g_return_val_if_fail (c_handler != NULL, 0);
  g_return_val_if_fail (G_IS_OBJECT (gobject), 0);

  if (connect_flags & G_CONNECT_SWAPPED)
    ctx->closure = g_cclosure_new_object_swap (c_handler, gobject);
  else
    ctx->closure = g_cclosure_new_object (c_handler, gobject);

  ctx->handler_id = g_signal_connect_closure (instance, detailed_signal,
      ctx->closure, 0);

  g_object_weak_ref (instance_obj, instance_destroyed_cb, ctx);
  g_object_weak_ref (gobject, observer_destroyed_cb, ctx);
  g_closure_add_invalidate_notifier (ctx->closure, ctx,
      closure_invalidated_cb);

  return ctx->handler_id;
}

/*
 * _tp_quark_array_copy:
 * @quarks: A 0-terminated list of quarks to copy
 *
 * Copy a zero-terminated array into a GArray. The trailing
 * 0 is not counted in the @len member of the returned
 * array, but the @data member is guaranteed to be
 * zero-terminated.
 *
 * Returns: A new GArray containing a copy of @quarks.
 */
GArray *
_tp_quark_array_copy (const GQuark *quarks)
{
  GArray *array;
  const GQuark *q;

  array = g_array_new (TRUE, TRUE, sizeof (GQuark));

  for (q = quarks; q != NULL && *q != 0; q++)
    {
      g_array_append_val (array, *q);
    }

  return array;
}