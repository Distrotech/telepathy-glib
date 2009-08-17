/*
 * dbus.c - Source for D-Bus utilities
 *
 * Copyright (C) 2005-2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2005-2008 Nokia Corporation
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
 * SECTION:dbus
 * @title: D-Bus utilities
 * @short_description: some D-Bus utility functions
 *
 * D-Bus utility functions used in telepathy-glib.
 */

/**
 * SECTION:asv
 * @title: Manipulating a{sv} mappings
 * @short_description: Functions to manipulate mappings from string to
 *  variant, as represented in dbus-glib by a #GHashTable from string
 *  to #GValue
 *
 * Mappings from string to variant (D-Bus signature a{sv}) are commonly used
 * to provide extensibility, but in dbus-glib they're somewhat awkward to deal
 * with.
 *
 * These functions provide convenient access to the values in such
 * a mapping.
 *
 * They also work around the fact that none of the #GHashTable public API
 * takes a const pointer to a #GHashTable, even the read-only methods that
 * logically ought to.
 *
 * Parts of telepathy-glib return const pointers to #GHashTable, to encourage
 * the use of this API.
 *
 * Since: 0.7.9
 */

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/dbus-internal.h>

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <gobject/gvaluecollector.h>

#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/util.h>

#include "telepathy-glib/_gen/signals-marshal.h"

#include "telepathy-glib/_gen/tp-cli-dbus-daemon-body.h"

#define DEBUG_FLAG TP_DEBUG_PROXY
#include "debug-internal.h"

/**
 * tp_asv_size:
 * @asv: a GHashTable
 *
 * Return the size of @asv as if via g_hash_table_size().
 *
 * The only difference is that this version takes a const #GHashTable and
 * casts it.
 *
 * Since: 0.7.12
 */
/* (#define + static inline in dbus.h) */

/**
 * tp_dbus_g_method_return_not_implemented:
 * @context: The D-Bus method invocation context
 *
 * Return the Telepathy error NotImplemented from the method invocation
 * given by @context.
 */
void
tp_dbus_g_method_return_not_implemented (DBusGMethodInvocation *context)
{
  GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "Not implemented" };

  dbus_g_method_return_error (context, &e);
}

static DBusGConnection *
starter_bus_conn (GError **error)
{
  static DBusGConnection *starter_bus = NULL;

  if (starter_bus == NULL)
    {
      starter_bus = dbus_g_bus_get (DBUS_BUS_STARTER, error);
    }

  return starter_bus;
}

/**
 * tp_get_bus:
 *
 * Returns a connection to the D-Bus daemon on which this process was
 * activated if it was launched by D-Bus service activation, or the session
 * bus otherwise.
 *
 * If dbus_bus_get() fails, exit with error code 1.
 *
 * Note that this function is not suitable for use in applications which can
 * be useful even in the absence of D-Bus - it is designed for use in
 * connection managers, which are not at all useful without a D-Bus
 * connection. See &lt;https://bugs.freedesktop.org/show_bug.cgi?id=18832&gt;.
 * Most processes should use tp_dbus_daemon_dup() instead.
 *
 * Returns: a connection to the starter or session D-Bus daemon.
 */
DBusGConnection *
tp_get_bus (void)
{
  GError *error = NULL;
  DBusGConnection *bus = starter_bus_conn (&error);

  if (bus == NULL)
    {
      g_warning ("Failed to connect to starter bus: %s", error->message);
      exit (1);
    }

  return bus;
}

/**
 * tp_get_bus_proxy:
 *
 * Return a #DBusGProxy for the bus daemon object.
 *
 * Returns: a proxy for the bus daemon object on the starter or session bus.
 *
 * Deprecated: 0.7.26: Use tp_dbus_daemon_dup() in new code.
 */
DBusGProxy *
tp_get_bus_proxy (void)
{
  static DBusGProxy *bus_proxy = NULL;

  if (bus_proxy == NULL)
    {
      DBusGConnection *bus = tp_get_bus ();

      bus_proxy = dbus_g_proxy_new_for_name (bus,
                                            "org.freedesktop.DBus",
                                            "/org/freedesktop/DBus",
                                            "org.freedesktop.DBus");

      if (bus_proxy == NULL)
        g_error ("Failed to get proxy object for bus.");
    }

  return bus_proxy;
}

/**
 * TpDBusNameType:
 * @TP_DBUS_NAME_TYPE_UNIQUE: accept unique names like :1.123
 *  (not including the name of the bus daemon itself)
 * @TP_DBUS_NAME_TYPE_WELL_KNOWN: accept well-known names like
 *  com.example.Service (not including the name of the bus daemon itself)
 * @TP_DBUS_NAME_TYPE_BUS_DAEMON: accept the name of the bus daemon
 *  itself, which has the syntax of a well-known name, but behaves like a
 *  unique name
 * @TP_DBUS_NAME_TYPE_NOT_BUS_DAEMON: accept either unique or well-known
 *  names, but not the bus daemon
 * @TP_DBUS_NAME_TYPE_ANY: accept any of the above
 *
 * A set of flags indicating which D-Bus bus names are acceptable.
 * They can be combined with the bitwise-or operator to accept multiple
 * types. %TP_DBUS_NAME_TYPE_NOT_BUS_DAEMON and %TP_DBUS_NAME_TYPE_ANY are
 * the bitwise-or of other appropriate types, for convenience.
 *
 * Since: 0.7.1
 */

/**
 * tp_dbus_check_valid_bus_name:
 * @name: a possible bus name
 * @allow_types: some combination of %TP_DBUS_NAME_TYPE_UNIQUE,
 *  %TP_DBUS_NAME_TYPE_WELL_KNOWN or %TP_DBUS_NAME_TYPE_BUS_DAEMON
 *  (often this will be %TP_DBUS_NAME_TYPE_NOT_BUS_DAEMON or
 *  %TP_DBUS_NAME_TYPE_ANY)
 * @error: used to raise %TP_DBUS_ERROR_INVALID_BUS_NAME if %FALSE is returned
 *
 * Check that the given string is a valid D-Bus bus name of an appropriate
 * type.
 *
 * Returns: %TRUE if @name is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_dbus_check_valid_bus_name (const gchar *name,
                              TpDBusNameType allow_types,
                              GError **error)
{
  gboolean dot = FALSE;
  gboolean unique;
  gchar last;
  const gchar *ptr;

  g_return_val_if_fail (name != NULL, FALSE);

  if (name[0] == '\0')
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
          "The empty string is not a valid bus name");
      return FALSE;
    }

  if (!tp_strdiff (name, DBUS_SERVICE_DBUS))
    {
      if (allow_types & TP_DBUS_NAME_TYPE_BUS_DAEMON)
        return TRUE;

      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
          "The D-Bus daemon's bus name is not acceptable here");
      return FALSE;
    }

  unique = (name[0] == ':');
  if (unique && (allow_types & TP_DBUS_NAME_TYPE_UNIQUE) == 0)
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
          "A well-known bus name not starting with ':'%s is required",
          allow_types & TP_DBUS_NAME_TYPE_BUS_DAEMON
            ? " (or the bus daemon itself)"
            : "");
      return FALSE;
    }

  if (!unique && (allow_types & TP_DBUS_NAME_TYPE_WELL_KNOWN) == 0)
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
          "A unique bus name starting with ':'%s is required",
          allow_types & TP_DBUS_NAME_TYPE_BUS_DAEMON
            ? " (or the bus daemon itself)"
            : "");
      return FALSE;
    }

  if (strlen (name) > 255)
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
          "Invalid bus name: too long (> 255 characters)");
      return FALSE;
    }

  last = '\0';

  for (ptr = name + (unique ? 1 : 0); *ptr != '\0'; ptr++)
    {
      if (*ptr == '.')
        {
          dot = TRUE;

          if (last == '.')
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_BUS_NAME,
                  "Invalid bus name '%s': contains '..'", name);
              return FALSE;
            }
          else if (last == '\0')
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_BUS_NAME,
                  "Invalid bus name '%s': must not start with '.'", name);
              return FALSE;
            }
        }
      else if (g_ascii_isdigit (*ptr))
        {
          if (!unique)
            {
              if (last == '.')
                {
                  g_set_error (error, TP_DBUS_ERRORS,
                      TP_DBUS_ERROR_INVALID_BUS_NAME,
                      "Invalid bus name '%s': a digit may not follow '.' "
                      "except in a unique name starting with ':'", name);
                  return FALSE;
                }
              else if (last == '\0')
                {
                  g_set_error (error, TP_DBUS_ERRORS,
                      TP_DBUS_ERROR_INVALID_BUS_NAME,
                      "Invalid bus name '%s': must not start with a digit",
                      name);
                  return FALSE;
                }
            }
        }
      else if (!g_ascii_isalpha (*ptr) && *ptr != '_' && *ptr != '-')
        {
          g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
              "Invalid bus name '%s': contains invalid character '%c'",
              name, *ptr);
          return FALSE;
        }

      last = *ptr;
    }

  if (last == '.')
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
          "Invalid bus name '%s': must not end with '.'", name);
      return FALSE;
    }

  if (!dot)
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_BUS_NAME,
          "Invalid bus name '%s': must contain '.'", name);
      return FALSE;
    }

  return TRUE;
}

/**
 * tp_dbus_check_valid_interface_name:
 * @name: a possible interface name
 * @error: used to raise %TP_DBUS_ERROR_INVALID_INTERFACE_NAME if %FALSE is
 *  returned
 *
 * Check that the given string is a valid D-Bus interface name. This is
 * also appropriate to use to check for valid error names.
 *
 * Returns: %TRUE if @name is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_dbus_check_valid_interface_name (const gchar *name,
                                    GError **error)
{
  gboolean dot = FALSE;
  gchar last;
  const gchar *ptr;

  g_return_val_if_fail (name != NULL, FALSE);

  if (name[0] == '\0')
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
          "The empty string is not a valid interface name");
      return FALSE;
    }

  if (strlen (name) > 255)
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
          "Invalid interface name: too long (> 255 characters)");
      return FALSE;
    }

  last = '\0';

  for (ptr = name; *ptr != '\0'; ptr++)
    {
      if (*ptr == '.')
        {
          dot = TRUE;

          if (last == '.')
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
                  "Invalid interface name '%s': contains '..'", name);
              return FALSE;
            }
          else if (last == '\0')
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
                  "Invalid interface name '%s': must not start with '.'",
                  name);
              return FALSE;
            }
        }
      else if (g_ascii_isdigit (*ptr))
        {
          if (last == '\0')
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
                  "Invalid interface name '%s': must not start with a digit",
                  name);
              return FALSE;
            }
          else if (last == '.')
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
                  "Invalid interface name '%s': a digit must not follow '.'",
                  name);
              return FALSE;
            }
        }
      else if (!g_ascii_isalpha (*ptr) && *ptr != '_')
        {
          g_set_error (error, TP_DBUS_ERRORS,
              TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
              "Invalid interface name '%s': contains invalid character '%c'",
              name, *ptr);
          return FALSE;
        }

      last = *ptr;
    }

  if (last == '.')
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
          "Invalid interface name '%s': must not end with '.'", name);
      return FALSE;
    }

  if (!dot)
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_INTERFACE_NAME,
          "Invalid interface name '%s': must contain '.'", name);
      return FALSE;
    }

  return TRUE;
}

/**
 * tp_dbus_check_valid_member_name:
 * @name: a possible member name
 * @error: used to raise %TP_DBUS_ERROR_INVALID_MEMBER_NAME if %FALSE is
 *  returned
 *
 * Check that the given string is a valid D-Bus member (method or signal) name.
 *
 * Returns: %TRUE if @name is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_dbus_check_valid_member_name (const gchar *name,
                                 GError **error)
{
  const gchar *ptr;

  g_return_val_if_fail (name != NULL, FALSE);

  if (name[0] == '\0')
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_MEMBER_NAME,
          "The empty string is not a valid method or signal name");
      return FALSE;
    }

  if (strlen (name) > 255)
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_MEMBER_NAME,
          "Invalid method or signal name: too long (> 255 characters)");
      return FALSE;
    }

  for (ptr = name; *ptr != '\0'; ptr++)
    {
      if (g_ascii_isdigit (*ptr))
        {
          if (ptr == name)
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_MEMBER_NAME,
                  "Invalid method or signal name '%s': must not start with "
                  "a digit", name);
              return FALSE;
            }
        }
      else if (!g_ascii_isalpha (*ptr) && *ptr != '_')
        {
          g_set_error (error, TP_DBUS_ERRORS,
              TP_DBUS_ERROR_INVALID_MEMBER_NAME,
              "Invalid method or signal name '%s': contains invalid "
              "character '%c'",
              name, *ptr);
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * tp_dbus_check_valid_object_path:
 * @path: a possible object path
 * @error: used to raise %TP_DBUS_ERROR_INVALID_OBJECT_PATH if %FALSE is
 *  returned
 *
 * Check that the given string is a valid D-Bus object path.
 *
 * Returns: %TRUE if @path is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_dbus_check_valid_object_path (const gchar *path, GError **error)
{
  const gchar *ptr;

  g_return_val_if_fail (path != NULL, FALSE);

  if (path[0] != '/')
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_OBJECT_PATH,
          "Invalid object path '%s': must start with '/'",
          path);
      return FALSE;
    }

  if (path[1] == '\0')
    return TRUE;

  for (ptr = path + 1; *ptr != '\0'; ptr++)
    {
      if (*ptr == '/')
        {
          if (ptr[-1] == '/')
            {
              g_set_error (error, TP_DBUS_ERRORS,
                  TP_DBUS_ERROR_INVALID_OBJECT_PATH,
                  "Invalid object path '%s': contains '//'", path);
              return FALSE;
            }
        }
      else if (!g_ascii_isalnum (*ptr) && *ptr != '_')
        {
          g_set_error (error, TP_DBUS_ERRORS,
              TP_DBUS_ERROR_INVALID_OBJECT_PATH,
              "Invalid object path '%s': contains invalid character '%c'",
              path, *ptr);
          return FALSE;
        }
    }

  if (ptr[-1] == '/')
    {
        g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_INVALID_OBJECT_PATH,
            "Invalid object path '%s': is not '/' but does end with '/'",
            path);
        return FALSE;
    }

  return TRUE;
}

/**
 * TpDBusDaemonClass:
 *
 * The class of #TpDBusDaemon.
 *
 * Since: 0.7.1
 */
struct _TpDBusDaemonClass
{
  /*<private>*/
  TpProxyClass parent_class;
  gpointer priv;
};

/**
 * TpDBusDaemon:
 *
 * A subclass of #TpProxy that represents the D-Bus daemon. It mainly provides
 * functionality to manage well-known names on the bus.
 *
 * Since: 0.7.1
 */
struct _TpDBusDaemon
{
  /*<private>*/
  TpProxy parent;

  TpDBusDaemonPrivate *priv;
};

struct _TpDBusDaemonPrivate
{
  /* dup'd name => _NameOwnerWatch */
  GHashTable *name_owner_watches;
  /* reffed */
  DBusConnection *libdbus;
};

G_DEFINE_TYPE (TpDBusDaemon, tp_dbus_daemon, TP_TYPE_PROXY);

static gpointer starter_bus_daemon = NULL;

/**
 * tp_dbus_daemon_dup:
 * @error: Used to indicate error if %NULL is returned
 *
 * Returns a proxy for signals and method calls on the D-Bus daemon on which
 * this process was activated (if it was launched by D-Bus service
 * activation), or the session bus (otherwise).
 *
 * If it is not possible to connect to the appropriate bus, raise an error
 * and return %NULL.
 *
 * The returned #TpDBusDaemon is cached; the same #TpDBusDaemon object will
 * be returned by this function repeatedly, as long as at least one reference
 * exists.
 *
 * Returns: a reference to a proxy for signals and method calls on the bus
 *  daemon, or %NULL
 *
 * Since: 0.7.26
 */
TpDBusDaemon *
tp_dbus_daemon_dup (GError **error)
{
  DBusGConnection *conn;

  if (starter_bus_daemon != NULL)
    return g_object_ref (starter_bus_daemon);

  conn = starter_bus_conn (error);

  if (conn == NULL)
    return NULL;

  starter_bus_daemon = tp_dbus_daemon_new (conn);
  g_assert (starter_bus_daemon != NULL);
  g_object_add_weak_pointer (starter_bus_daemon, &starter_bus_daemon);

  return starter_bus_daemon;
}

/**
 * tp_dbus_daemon_new:
 * @connection: a connection to D-Bus
 *
 * Returns a proxy for signals and method calls on a particular bus
 * connection.
 *
 * Use tp_dbus_daemon_dup() instead if you just want a connection to the
 * starter or session bus (which is almost always the right thing for
 * Telepathy).
 *
 * Returns: a new proxy for signals and method calls on the bus daemon
 *  to which @connection is connected
 *
 * Since: 0.7.1
 */
TpDBusDaemon *
tp_dbus_daemon_new (DBusGConnection *connection)
{
  g_return_val_if_fail (connection != NULL, NULL);

  return TP_DBUS_DAEMON (g_object_new (TP_TYPE_DBUS_DAEMON,
        "dbus-connection", connection,
        "bus-name", DBUS_SERVICE_DBUS,
        "object-path", DBUS_PATH_DBUS,
        NULL));
}

typedef struct
{
  TpDBusDaemonNameOwnerChangedCb callback;
  gpointer user_data;
  GDestroyNotify destroy;
  gchar *last_owner;
} _NameOwnerWatch;

typedef struct
{
  TpDBusDaemonNameOwnerChangedCb callback;
  gpointer user_data;
  GDestroyNotify destroy;
} _NameOwnerSubWatch;

static void
_tp_dbus_daemon_name_owner_changed_multiple (TpDBusDaemon *self,
                                             const gchar *name,
                                             const gchar *new_owner,
                                             gpointer user_data)
{
  GArray *array = user_data;
  guint i;

  for (i = 0; i < array->len; i++)
    {
      _NameOwnerSubWatch *watch = &g_array_index (array, _NameOwnerSubWatch,
          i);

      watch->callback (self, name, new_owner, watch->user_data);
    }
}

static void
_tp_dbus_daemon_name_owner_changed_multiple_free (gpointer data)
{
  GArray *array = data;
  guint i;

  for (i = 0; i < array->len; i++)
    {
      _NameOwnerSubWatch *watch = &g_array_index (array, _NameOwnerSubWatch,
          i);

      if (watch->destroy)
        watch->destroy (watch->user_data);
    }

  g_array_free (array, TRUE);
}

static void
_tp_dbus_daemon_name_owner_changed (TpDBusDaemon *self,
                                    const gchar *name,
                                    const gchar *new_owner)
{
  _NameOwnerWatch *watch = g_hash_table_lookup (self->priv->name_owner_watches,
      name);

  DEBUG ("%s -> %s", name, new_owner);

  if (watch == NULL)
    return;

  /* This is partly to handle the case where an owner change happens
   * while GetNameOwner is in flight, partly to be able to optimize by only
   * calling GetNameOwner if we didn't already know, and partly because of a
   * dbus-glib bug that means we get every signal twice
   * (it thinks org.freedesktop.DBus is both a well-known name and a unique
   * name). */
  if (!tp_strdiff (watch->last_owner, new_owner))
    return;

  g_free (watch->last_owner);
  watch->last_owner = g_strdup (new_owner);

  watch->callback (self, name, new_owner, watch->user_data);
}

static dbus_int32_t daemons_slot = -1;

typedef struct {
    DBusConnection *libdbus;
    DBusMessage *message;
} NOCIdleContext;

static NOCIdleContext *
noc_idle_context_new (DBusConnection *libdbus,
                      DBusMessage *message)
{
  NOCIdleContext *context = g_slice_new (NOCIdleContext);

  context->libdbus = dbus_connection_ref (libdbus);
  context->message = dbus_message_ref (message);
  return context;
}

static void
noc_idle_context_free (gpointer data)
{
  NOCIdleContext *context = data;

  dbus_connection_unref (context->libdbus);
  dbus_message_unref (context->message);
  g_slice_free (NOCIdleContext, context);
}

static gboolean
noc_idle_context_invoke (gpointer data)
{
  NOCIdleContext *context = data;
  const gchar *name;
  const gchar *old_owner;
  const gchar *new_owner;
  DBusError dbus_error = DBUS_ERROR_INIT;
  GSList **daemons;

  if (daemons_slot == -1)
    return FALSE;

  if (!dbus_message_get_args (context->message, &dbus_error,
        DBUS_TYPE_STRING, &name,
        DBUS_TYPE_STRING, &old_owner,
        DBUS_TYPE_STRING, &new_owner,
        DBUS_TYPE_INVALID))
    {
      DEBUG ("Couldn't unpack NameOwnerChanged(s, s, s): %s: %s",
          dbus_error.name, dbus_error.message);
      dbus_error_free (&dbus_error);
      return FALSE;
    }

  daemons = dbus_connection_get_data (context->libdbus, daemons_slot);

  DEBUG ("NameOwnerChanged(%s, %s -> %s)", name, old_owner, new_owner);

  /* should always be non-NULL, barring bugs */
  if (G_LIKELY (daemons != NULL))
    {
      GSList *iter;

      for (iter = *daemons; iter != NULL; iter = iter->next)
        _tp_dbus_daemon_name_owner_changed (iter->data, name, new_owner);
    }

  return FALSE;
}

static DBusHandlerResult
_tp_dbus_daemon_name_owner_changed_filter (DBusConnection *libdbus,
                                           DBusMessage *message,
                                           void *unused G_GNUC_UNUSED)
{
  /* We have to do the real work in an idle, so we don't break re-entrant
   * calls (the dbus-glib event source isn't re-entrant) */
  if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS,
        "NameOwnerChanged") &&
      dbus_message_has_sender (message, DBUS_SERVICE_DBUS))
    g_idle_add_full (G_PRIORITY_HIGH, noc_idle_context_invoke,
        noc_idle_context_new (libdbus, message),
        noc_idle_context_free);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

typedef struct {
    TpDBusDaemon *self;
    gchar *name;
    DBusMessage *reply;
    gsize refs;
} GetNameOwnerContext;

static GetNameOwnerContext *
get_name_owner_context_new (TpDBusDaemon *self,
                            const gchar *name)
{
  GetNameOwnerContext *context = g_slice_new (GetNameOwnerContext);

  context->self = g_object_ref (self);
  context->name = g_strdup (name);
  context->reply = NULL;
  DEBUG ("New, 1 ref");
  context->refs = 1;
  return context;
}

static void
get_name_owner_context_unref (gpointer data)
{
  GetNameOwnerContext *context = data;

  DEBUG ("%lu -> %lu", (gulong) context->refs, (gulong) (context->refs-1));

  if (--context->refs == 0)
    {
      g_object_unref (context->self);
      g_free (context->name);

      if (context->reply != NULL)
        dbus_message_unref (context->reply);

      g_slice_free (GetNameOwnerContext, context);
    }
}

static gboolean
_tp_dbus_daemon_get_name_owner_idle (gpointer data)
{
  GetNameOwnerContext *context = data;
  const gchar *owner = "";

  if (context->reply == NULL)
    {
      DEBUG ("Connection disconnected or no reply to GetNameOwner(%s)",
          context->name);
    }
  else if (dbus_message_get_type (context->reply) ==
      DBUS_MESSAGE_TYPE_METHOD_RETURN)
    {
      if (!dbus_message_get_args (context->reply, NULL,
            DBUS_TYPE_STRING, &owner,
            DBUS_TYPE_INVALID))
        {
          DEBUG ("Malformed reply from GetNameOwner(%s), assuming no owner",
              context->name);
        }
    }
  else
    {
      if (DEBUGGING)
        {
          DBusError error = DBUS_ERROR_INIT;

          if (dbus_set_error_from_message (&error, context->reply))
            {
              DEBUG ("GetNameOwner(%s) raised %s: %s", context->name,
                  error.name, error.message);
              dbus_error_free (&error);
            }
          else
            {
              DEBUG ("Unexpected message type from GetNameOwner(%s)",
                  context->name);
            }
        }
    }

  _tp_dbus_daemon_name_owner_changed (context->self, context->name, owner);

  return FALSE;
}

/**
 * TpDBusDaemonNameOwnerChangedCb:
 * @bus_daemon: The D-Bus daemon
 * @name: The name whose ownership has changed or been discovered
 * @new_owner: The unique name that now owns @name
 * @user_data: Arbitrary user-supplied data as passed to
 *  tp_dbus_daemon_watch_name_owner()
 *
 * The signature of the callback called by tp_dbus_daemon_watch_name_owner().
 *
 * Since: 0.7.1
 */

static inline gchar *
_tp_dbus_daemon_get_noc_rule (const gchar *name)
{
  return g_strdup_printf ("type='signal',"
      "sender='" DBUS_SERVICE_DBUS "',"
      "path='" DBUS_PATH_DBUS "',"
      "interface='"DBUS_INTERFACE_DBUS "',"
      "member='NameOwnerChanged',"
      "arg0='%s'", name);
}

static void
_tp_dbus_daemon_get_name_owner_notify (DBusPendingCall *pc,
                                       gpointer data)
{
  GetNameOwnerContext *context = data;

  /* we recycle this function for the case where the connection is already
   * disconnected: in that case we use pc = NULL */
  if (pc != NULL)
    context->reply = dbus_pending_call_steal_reply (pc);

  /* We have to do the real work in an idle, so we don't break re-entrant
   * calls (the dbus-glib event source isn't re-entrant) */
  DEBUG ("%lu -> %lu", (gulong) context->refs, (gulong) (context->refs + 1));
  context->refs++;
  g_idle_add_full (G_PRIORITY_HIGH, _tp_dbus_daemon_get_name_owner_idle,
      context, get_name_owner_context_unref);

  if (pc != NULL)
    dbus_pending_call_unref (pc);
}

/**
 * tp_dbus_daemon_watch_name_owner:
 * @self: The D-Bus daemon
 * @name: The name whose ownership is to be watched
 * @callback: Callback to call when the ownership is discovered or changes
 * @user_data: Arbitrary data to pass to @callback
 * @destroy: Called to destroy @user_data when the name owner watch is
 *  cancelled due to tp_dbus_daemon_cancel_name_owner_watch()
 *
 * Arrange for @callback to be called with the owner of @name as soon as
 * possible (which might even be before this function returns!), then
 * again every time the ownership of @name changes.
 *
 * If multiple watches are registered for the same @name, they will be called
 * in the order they were registered.
 *
 * Since: 0.7.1
 */
void
tp_dbus_daemon_watch_name_owner (TpDBusDaemon *self,
                                 const gchar *name,
                                 TpDBusDaemonNameOwnerChangedCb callback,
                                 gpointer user_data,
                                 GDestroyNotify destroy)
{
  _NameOwnerWatch *watch = g_hash_table_lookup (self->priv->name_owner_watches,
      name);

  g_return_if_fail (TP_IS_DBUS_DAEMON (self));
  g_return_if_fail (tp_dbus_check_valid_bus_name (name,
        TP_DBUS_NAME_TYPE_ANY, NULL));
  g_return_if_fail (name != NULL);
  g_return_if_fail (callback != NULL);

  if (watch == NULL)
    {
      gchar *match_rule;
      DBusMessage *message;
      DBusPendingCall *pc = NULL;
      GetNameOwnerContext *context = get_name_owner_context_new (self, name);

      /* Allocate a single watch (common case) */
      watch = g_slice_new (_NameOwnerWatch);
      watch->callback = callback;
      watch->user_data = user_data;
      watch->destroy = destroy;
      watch->last_owner = NULL;

      g_hash_table_insert (self->priv->name_owner_watches, g_strdup (name),
          watch);

      /* We want to be notified about name owner changes for this one.
       * Assume the match addition will succeed; there's no good way to cope
       * with failure here... */
      match_rule = _tp_dbus_daemon_get_noc_rule (name);
      DEBUG ("Adding match rule %s", match_rule);
      dbus_bus_add_match (self->priv->libdbus, match_rule, NULL);

      message = dbus_message_new_method_call (DBUS_SERVICE_DBUS,
          DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");

      if (message == NULL)
        g_error ("Out of memory");

      /* We already checked that @name was in (a small subset of) UTF-8,
       * so OOM is the only thing that can go wrong. The use of &name here
       * is because libdbus is strange. */
      if (!dbus_message_append_args (message,
            DBUS_TYPE_STRING, &name,
            DBUS_TYPE_INVALID))
        g_error ("Out of memory");

      if (!dbus_connection_send_with_reply (self->priv->libdbus,
          message, &pc, -1))
        g_error ("Out of memory");
      /* pc is unreffed by _tp_dbus_daemon_get_name_owner_notify */

      if (pc == NULL || dbus_pending_call_get_completed (pc))
        {
          /* pc can be NULL when the connection is already disconnected */
          _tp_dbus_daemon_get_name_owner_notify (pc, context);
          get_name_owner_context_unref (context);
        }
      else if (!dbus_pending_call_set_notify (pc,
            _tp_dbus_daemon_get_name_owner_notify,
            context, get_name_owner_context_unref))
        {
          g_error ("Out of memory");
        }
    }
  else
    {
      _NameOwnerSubWatch tmp = { callback, user_data, destroy };

      if (watch->callback == _tp_dbus_daemon_name_owner_changed_multiple)
        {
          /* The watch is already a "multiplexer", just append to it */
          GArray *array = watch->user_data;

          g_array_append_val (array, tmp);
        }
      else
        {
          /* Replace the old contents of the watch with one that dispatches
           * the signal to (potentially) more than one watcher */
          GArray *array = g_array_sized_new (FALSE, FALSE,
              sizeof (_NameOwnerSubWatch), 2);

          /* The new watcher */
          g_array_append_val (array, tmp);
          /* The old watcher */
          tmp.callback = watch->callback;
          tmp.user_data = watch->user_data;
          tmp.destroy = watch->destroy;
          g_array_prepend_val (array, tmp);

          watch->callback = _tp_dbus_daemon_name_owner_changed_multiple;
          watch->user_data = array;
          watch->destroy = _tp_dbus_daemon_name_owner_changed_multiple_free;
        }

      if (watch->last_owner != NULL)
        {
          /* FIXME: should avoid reentrancy? */
          callback (self, name, watch->last_owner, user_data);
        }
    }
}

static void
_tp_dbus_daemon_stop_watching (TpDBusDaemon *self,
                               const gchar *name,
                               _NameOwnerWatch *watch)
{
  gchar *match_rule;

  if (watch->destroy)
    watch->destroy (watch->user_data);

  g_free (watch->last_owner);
  g_slice_free (_NameOwnerWatch, watch);

  match_rule = _tp_dbus_daemon_get_noc_rule (name);
  DEBUG ("Removing match rule %s", match_rule);
  dbus_bus_remove_match (self->priv->libdbus, match_rule, NULL);
  g_free (match_rule);
}

/**
 * tp_dbus_daemon_cancel_name_owner_watch:
 * @self: the D-Bus daemon
 * @name: the name that was being watched
 * @callback: the callback that was called
 * @user_data: the user data that was provided
 *
 * If there was a previous call to tp_dbus_daemon_watch_name_owner()
 * with exactly the given @name, @callback and @user_data, remove it.
 *
 * If more than one watch matching the details provided was active, remove
 * only the most recently added one.
 *
 * Returns: %TRUE if there was such a watch, %FALSE otherwise
 *
 * Since: 0.7.1
 */
gboolean
tp_dbus_daemon_cancel_name_owner_watch (TpDBusDaemon *self,
                                        const gchar *name,
                                        TpDBusDaemonNameOwnerChangedCb callback,
                                        gconstpointer user_data)
{
  _NameOwnerWatch *watch = g_hash_table_lookup (self->priv->name_owner_watches,
      name);

  g_return_val_if_fail (TP_IS_DBUS_DAEMON (self), FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  if (watch == NULL)
    {
      /* No watch at all */
      return FALSE;
    }
  else if (watch->callback == callback && watch->user_data == user_data)
    {
      /* Simple case: there is one name-owner watch and it's what we wanted */
      _tp_dbus_daemon_stop_watching (self, name, watch);
      g_hash_table_remove (self->priv->name_owner_watches, name);
      return TRUE;
    }
  else if (watch->callback == _tp_dbus_daemon_name_owner_changed_multiple)
    {
      /* Complicated case: this watch is a "multiplexer", we need to check
       * its contents */
      GArray *array = watch->user_data;
      guint i;

      for (i = 1; i <= array->len; i++)
        {
          _NameOwnerSubWatch *entry = &g_array_index (array,
              _NameOwnerSubWatch, array->len - i);

          if (entry->callback == callback && entry->user_data == user_data)
            {
              if (entry->destroy != NULL)
                entry->destroy (entry->user_data);

              g_array_remove_index (array, array->len - i);

              if (array->len == 0)
                {
                  _tp_dbus_daemon_stop_watching (self, name, watch);
                  g_hash_table_remove (self->priv->name_owner_watches, name);
                }

              return TRUE;
            }
        }
    }

  /* We haven't found it */
  return FALSE;
}

/* for internal use (TpChannel, TpConnection _new convenience functions) */
gboolean
_tp_dbus_daemon_get_name_owner (TpDBusDaemon *self,
                                gint timeout_ms,
                                const gchar *well_known_name,
                                gchar **unique_name,
                                GError **error)
{
  DBusGConnection *gconn;
  DBusConnection *dbc;
  DBusMessage *message;
  DBusMessage *reply;
  DBusError dbus_error;
  const char *name_in_reply;
  const GError *invalidated;

  g_return_val_if_fail (TP_IS_DBUS_DAEMON (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  invalidated = tp_proxy_get_invalidated (self);

  if (invalidated != NULL)
    {
      if (error != NULL)
        *error = g_error_copy (invalidated);

      return FALSE;
    }

  gconn = tp_proxy_get_dbus_connection (self);
  dbc = dbus_g_connection_get_connection (gconn);

  message = dbus_message_new_method_call (DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
      DBUS_INTERFACE_DBUS, "GetNameOwner");

  if (message == NULL)
    g_error ("Out of memory");

  if (!dbus_message_append_args (message,
        DBUS_TYPE_STRING, &well_known_name,
        DBUS_TYPE_INVALID))
    g_error ("Out of memory");

  dbus_error_init (&dbus_error);
  reply = dbus_connection_send_with_reply_and_block (dbc, message,
      timeout_ms, &dbus_error);

  dbus_message_unref (message);

  if (reply == NULL)
    {
      if (!tp_strdiff (dbus_error.name, DBUS_ERROR_NO_MEMORY))
        g_error ("Out of memory");

      /* FIXME: ideally we'd use dbus-glib's error mapping for this */
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_NAME_OWNER_LOST,
          "%s: %s", dbus_error.name, dbus_error.message);

      dbus_error_free (&dbus_error);
      return FALSE;
    }

  if (!dbus_message_get_args (reply, &dbus_error,
        DBUS_TYPE_STRING, &name_in_reply,
        DBUS_TYPE_INVALID))
    {
      g_set_error (error, TP_DBUS_ERRORS, TP_DBUS_ERROR_NAME_OWNER_LOST,
          "%s: %s", dbus_error.name, dbus_error.message);

      dbus_error_free (&dbus_error);
      dbus_message_unref (reply);
      return FALSE;
    }

  if (unique_name != NULL)
    *unique_name = g_strdup (name_in_reply);

  dbus_message_unref (reply);

  return TRUE;
}

/**
 * tp_dbus_daemon_request_name:
 * @self: a TpDBusDaemon
 * @well_known_name: a well-known name to acquire
 * @idempotent: whether to consider it to be a success if this process
 *              already owns the name
 * @error: used to raise an error if %FALSE is returned
 *
 * Claim the given well-known name without queueing, allowing replacement
 * or replacing an existing name-owner. This makes a synchronous call to the
 * bus daemon.
 *
 * Returns: %TRUE if @well_known_name was claimed, or %FALSE and sets @error if
 *          an error occurred.
 *
 * Since: 0.7.30
 */
gboolean
tp_dbus_daemon_request_name (TpDBusDaemon *self,
                             const gchar *well_known_name,
                             gboolean idempotent,
                             GError **error)
{
  TpProxy *as_proxy = (TpProxy *) self;
  DBusGConnection *gconn;
  DBusConnection *dbc;
  DBusError dbus_error;
  int result;
  const GError *invalidated;

  g_return_val_if_fail (TP_IS_DBUS_DAEMON (self), FALSE);
  g_return_val_if_fail (tp_dbus_check_valid_bus_name (well_known_name,
        TP_DBUS_NAME_TYPE_WELL_KNOWN, error), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  invalidated = tp_proxy_get_invalidated (self);

  if (invalidated != NULL)
    {
      if (error != NULL)
        *error = g_error_copy (invalidated);

      return FALSE;
    }

  gconn = as_proxy->dbus_connection;
  dbc = dbus_g_connection_get_connection (gconn);

  dbus_error_init (&dbus_error);
  result = dbus_bus_request_name (dbc, well_known_name,
      DBUS_NAME_FLAG_DO_NOT_QUEUE, &dbus_error);

  switch (result)
    {
    case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
      return TRUE;

    case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
      if (idempotent)
        {
          return TRUE;
        }
      else
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Name '%s' already in use by this process", well_known_name);
          return FALSE;
        }

    case DBUS_REQUEST_NAME_REPLY_EXISTS:
    case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
      /* the latter shouldn't actually happen since we said DO_NOT_QUEUE */
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Name '%s' already in use by another process", well_known_name);
      return FALSE;

    case -1:
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s: %s", dbus_error.name, dbus_error.message);
      dbus_error_free (&dbus_error);
      return FALSE;

    default:
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "RequestName('%s') returned %d and I don't know what that means",
          well_known_name, result);
      return FALSE;
    }
}

/**
 * tp_dbus_daemon_release_name:
 * @self: a TpDBusDaemon
 * @well_known_name: a well-known name owned by this process to release
 * @error: used to raise an error if %FALSE is returned
 *
 * Release the given well-known name. This makes a synchronous call to the bus
 * daemon.
 *
 * Returns: %TRUE if @well_known_name was released, or %FALSE and sets @error
 *          if an error occurred.
 *
 * Since: 0.7.30
 */
gboolean
tp_dbus_daemon_release_name (TpDBusDaemon *self,
                             const gchar *well_known_name,
                             GError **error)
{
  TpProxy *as_proxy = (TpProxy *) self;
  DBusGConnection *gconn;
  DBusConnection *dbc;
  DBusError dbus_error;
  int result;
  const GError *invalidated;

  g_return_val_if_fail (TP_IS_DBUS_DAEMON (self), FALSE);
  g_return_val_if_fail (tp_dbus_check_valid_bus_name (well_known_name,
        TP_DBUS_NAME_TYPE_WELL_KNOWN, error), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  invalidated = tp_proxy_get_invalidated (self);

  if (invalidated != NULL)
    {
      if (error != NULL)
        *error = g_error_copy (invalidated);

      return FALSE;
    }

  gconn = as_proxy->dbus_connection;
  dbc = dbus_g_connection_get_connection (gconn);
  dbus_error_init (&dbus_error);
  result = dbus_bus_release_name (dbc, well_known_name, &dbus_error);

  switch (result)
    {
    case DBUS_RELEASE_NAME_REPLY_RELEASED:
      return TRUE;

    case DBUS_RELEASE_NAME_REPLY_NOT_OWNER:
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_YOURS,
          "Name '%s' owned by another process", well_known_name);
      return FALSE;

    case DBUS_RELEASE_NAME_REPLY_NON_EXISTENT:
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Name '%s' not owned", well_known_name);
      return FALSE;

    case -1:
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "%s: %s", dbus_error.name, dbus_error.message);
      dbus_error_free (&dbus_error);
      return FALSE;

    default:
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "ReleaseName('%s') returned %d and I don't know what that means",
          well_known_name, result);
      return FALSE;
    }
}

static void
free_daemon_list (gpointer p)
{
  GSList **slistp = p;

  g_slist_free (*slistp);
  g_slice_free (GSList *, slistp);
}

static GObject *
tp_dbus_daemon_constructor (GType type,
                            guint n_params,
                            GObjectConstructParam *params)
{
  GObjectClass *object_class =
      (GObjectClass *) tp_dbus_daemon_parent_class;
  TpDBusDaemon *self = TP_DBUS_DAEMON (object_class->constructor (type,
        n_params, params));
  TpProxy *as_proxy = (TpProxy *) self;
  GSList **daemons;

  g_assert (!tp_strdiff (as_proxy->bus_name, DBUS_SERVICE_DBUS));
  g_assert (!tp_strdiff (as_proxy->object_path, DBUS_PATH_DBUS));

  self->priv->libdbus = dbus_connection_ref (
      dbus_g_connection_get_connection (
        tp_proxy_get_dbus_connection (self)));

  /* one ref per TpDBusDaemon, released in finalize */
  if (!dbus_connection_allocate_data_slot (&daemons_slot))
    g_error ("Out of memory");

  daemons = dbus_connection_get_data (self->priv->libdbus, daemons_slot);

  if (daemons == NULL)
    {
      daemons = g_slice_new (GSList *);

      *daemons = NULL;
      dbus_connection_set_data (self->priv->libdbus, daemons_slot, daemons,
          free_daemon_list);

      /* we add this filter at most once per DBusConnection */
      if (!dbus_connection_add_filter (self->priv->libdbus,
            _tp_dbus_daemon_name_owner_changed_filter, NULL, NULL))
        g_error ("Out of memory");
    }

  *daemons = g_slist_prepend (*daemons, self);

  return (GObject *) self;
}

static void
tp_dbus_daemon_init (TpDBusDaemon *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TYPE_DBUS_DAEMON,
      TpDBusDaemonPrivate);

  self->priv->name_owner_watches = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, NULL);
}

static void
tp_dbus_daemon_dispose (GObject *object)
{
  TpDBusDaemon *self = TP_DBUS_DAEMON (object);
  GSList **daemons;

  if (self->priv->name_owner_watches != NULL)
    {
      GHashTable *tmp = self->priv->name_owner_watches;
      GHashTableIter iter;
      gpointer k, v;

      self->priv->name_owner_watches = NULL;
      g_hash_table_iter_init (&iter, tmp);

      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          _tp_dbus_daemon_stop_watching (self, k, v);
          g_hash_table_iter_remove (&iter);
        }

      g_hash_table_destroy (tmp);
    }

  if (self->priv->libdbus != NULL)
    {
      /* remove myself from the list to be notified on NoC */
      daemons = dbus_connection_get_data (self->priv->libdbus, daemons_slot);

      /* should always be non-NULL, barring bugs */
      if (G_LIKELY (daemons != NULL))
        {
          *daemons = g_slist_remove (*daemons, self);
        }

      dbus_connection_unref (self->priv->libdbus);
      self->priv->libdbus = NULL;
    }

  G_OBJECT_CLASS (tp_dbus_daemon_parent_class)->dispose (object);
}

static void
tp_dbus_daemon_finalize (GObject *object)
{
  GObjectFinalizeFunc chain_up = G_OBJECT_CLASS (tp_dbus_daemon_parent_class)->finalize;

  /* one ref per TpDBusDaemon, from constructor */
  dbus_connection_free_data_slot (&daemons_slot);

  if (chain_up != NULL)
    chain_up (object);
}

/**
 * tp_dbus_daemon_init_known_interfaces:
 *
 * Ensure that the known interfaces for TpDBusDaemon have been set up.
 * This is done automatically when necessary, but for correct
 * overriding of library interfaces by local extensions, you should
 * call this function before calling
 * tp_proxy_or_subclass_hook_on_interface_add() with first argument
 * %TP_TYPE_DBUS_DAEMON.
 *
 * Since: 0.7.32
 */
void
tp_dbus_daemon_init_known_interfaces (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      tp_proxy_init_known_interfaces ();
      tp_proxy_or_subclass_hook_on_interface_add (TP_TYPE_DBUS_DAEMON,
          tp_cli_dbus_daemon_add_signals);

      g_once_init_leave (&once, 1);
    }
}

static void
tp_dbus_daemon_class_init (TpDBusDaemonClass *klass)
{
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  tp_dbus_daemon_init_known_interfaces ();

  g_type_class_add_private (klass, sizeof (TpDBusDaemonPrivate));

  object_class->constructor = tp_dbus_daemon_constructor;
  object_class->dispose = tp_dbus_daemon_dispose;
  object_class->finalize = tp_dbus_daemon_finalize;

  proxy_class->interface = TP_IFACE_QUARK_DBUS_DAEMON;
}

/* Auto-generated implementation of _tp_register_dbus_glib_marshallers */
#include "_gen/register-dbus-glib-marshallers-body.h"


/**
 * tp_g_value_slice_new_bytes:
 * @length: number of bytes to copy
 * @bytes: location of an array of bytes to be copied (this may be %NULL
 *  if and only if length is 0)
 *
 * Slice-allocate a #GValue containing a byte-array, using
 * tp_g_value_slice_new_boxed(). This function is convenient to use when
 * constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %DBUS_TYPE_G_UCHAR_ARRAY whose value is a copy
 * of @length bytes from @bytes, to be freed with tp_g_value_slice_free() or
 * g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_bytes (guint length,
                            gconstpointer bytes)
{
  GArray *arr;

  g_return_val_if_fail (length == 0 || bytes != NULL, NULL);
  arr = g_array_sized_new (FALSE, FALSE, 1, length);

  if (length > 0)
    g_array_append_vals (arr, bytes, length);

  return tp_g_value_slice_new_take_boxed (DBUS_TYPE_G_UCHAR_ARRAY, arr);
}

/**
 * tp_g_value_slice_new_take_bytes:
 * @bytes: a non-NULL #GArray of guchar, ownership of which will be taken by
 *  the #GValue
 *
 * Slice-allocate a #GValue containing @bytes, using
 * tp_g_value_slice_new_boxed(). This function is convenient to use when
 * constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %DBUS_TYPE_G_UCHAR_ARRAY whose value is
 * @bytes, to be freed with tp_g_value_slice_free() or
 * g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_take_bytes (GArray *bytes)
{
  g_return_val_if_fail (bytes != NULL, NULL);
  return tp_g_value_slice_new_take_boxed (DBUS_TYPE_G_UCHAR_ARRAY, bytes);
}

/**
 * tp_g_value_slice_new_object_path:
 * @path: a valid D-Bus object path which will be copied
 *
 * Slice-allocate a #GValue containing an object path, using
 * tp_g_value_slice_new_boxed(). This function is convenient to use when
 * constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %DBUS_TYPE_G_OBJECT_PATH whose value is a copy
 * of @path, to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_object_path (const gchar *path)
{
  g_return_val_if_fail (tp_dbus_check_valid_object_path (path, NULL), NULL);
  return tp_g_value_slice_new_boxed (DBUS_TYPE_G_OBJECT_PATH, path);
}

/**
 * tp_g_value_slice_new_static_object_path:
 * @path: a valid D-Bus object path which must remain valid forever
 *
 * Slice-allocate a #GValue containing an object path, using
 * tp_g_value_slice_new_static_boxed(). This function is convenient to use when
 * constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %DBUS_TYPE_G_OBJECT_PATH whose value is @path,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_static_object_path (const gchar *path)
{
  g_return_val_if_fail (tp_dbus_check_valid_object_path (path, NULL), NULL);
  return tp_g_value_slice_new_static_boxed (DBUS_TYPE_G_OBJECT_PATH, path);
}

/**
 * tp_g_value_slice_new_take_object_path:
 * @path: a valid D-Bus object path which will be freed with g_free() by the
 *  returned #GValue (the caller must own it before calling this function, but
 *  no longer owns it after this function returns)
 *
 * Slice-allocate a #GValue containing an object path, using
 * tp_g_value_slice_new_take_boxed(). This function is convenient to use when
 * constructing hash tables from string to #GValue, for example.
 *
 * Returns: a #GValue of type %DBUS_TYPE_G_OBJECT_PATH whose value is @path,
 * to be freed with tp_g_value_slice_free() or g_slice_free()
 *
 * Since: 0.7.27
 */
GValue *
tp_g_value_slice_new_take_object_path (gchar *path)
{
  g_return_val_if_fail (tp_dbus_check_valid_object_path (path, NULL), NULL);
  return tp_g_value_slice_new_take_boxed (DBUS_TYPE_G_OBJECT_PATH, path);
}

/**
 * tp_asv_new:
 * @first_key: the name of the first key (or NULL)
 * @...: type and value for the first key, followed by a NULL-terminated list
 *  of (key, type, value) tuples
 *
 * Creates a new #GHashTable for use with a{sv} maps, containing the values
 * passed in as parameters.
 *
 * The #GHashTable is synonymous with:
 * <informalexample><programlisting>
 * GHashTable *asv = g_hash_table_new_full (g_str_hash, g_str_equal,
 *    NULL, (GDestroyNotify) tp_g_value_slice_free);
 * </programlisting></informalexample>
 * Followed by manual insertion of each of the parameters.
 *
 * Parameters are stored in slice-allocated GValues and should be set using
 * tp_asv_set_*() and retrieved using tp_asv_get_*().
 *
 * tp_g_value_slice_new() and tp_g_value_slice_dup() may also be used to insert
 * into the map if required.
 * <informalexample><programlisting>
 * g_hash_table_insert (parameters, "account",
 *    tp_g_value_slice_new_string ("bob@mcbadgers.com"));
 * </programlisting></informalexample>
 *
 * <example>
 *  <title>Using tp_asv_new()</title>
 *  <programlisting>
 * GHashTable *parameters = tp_asv_new (
 *    "answer", G_TYPE_INT, 42,
 *    "question", G_TYPE_STRING, "We just don't know",
 *    NULL);</programlisting>
 * </example>
 *
 * Allocated values will be automatically free'd when overwritten, removed or
 * the hash table destroyed with g_hash_table_destroy().
 *
 * Returns: a newly created #GHashTable for storing a{sv} maps, free with
 * g_hash_table_destroy().
 * Since: 0.7.29
 */
GHashTable *
tp_asv_new (const gchar *first_key, ...)
{
  va_list var_args;
  char *key;
  GType type;
  GValue *value;
  char *error = NULL; /* NB: not a GError! */

  /* create a GHashTable */
  GHashTable *asv = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  va_start (var_args, first_key);

  for (key = (char *) first_key; key != NULL; key = va_arg (var_args, char *))
  {
    type = va_arg (var_args, GType);

    value = tp_g_value_slice_new (type);
    G_VALUE_COLLECT (value, var_args, 0, &error);

    if (error != NULL)
    {
      g_critical ("key %s: %s", key, error);
      g_free (error);
      error = NULL;
      tp_g_value_slice_free (value);
      continue;
    }

    g_hash_table_insert (asv, key, value);
  }

  va_end (var_args);

  return asv;
}

/**
 * tp_asv_get_boolean:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 * @valid: Either %NULL, or a location to store %TRUE if the key actually
 *  exists and has a boolean value
 *
 * If a value for @key in @asv is present and boolean, return it,
 * and set *@valid to %TRUE if @valid is not %NULL.
 *
 * Otherwise return %FALSE, and set *@valid to %FALSE if @valid is not %NULL.
 *
 * Returns: a boolean value for @key
 * Since: 0.7.9
 */
gboolean
tp_asv_get_boolean (const GHashTable *asv,
                    const gchar *key,
                    gboolean *valid)
{
  GValue *value;

  g_return_val_if_fail (asv != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL || !G_VALUE_HOLDS_BOOLEAN (value))
    {
      if (valid != NULL)
        *valid = FALSE;

      return FALSE;
    }

  if (valid != NULL)
    *valid = TRUE;

  return g_value_get_boolean (value);
}

/**
 * tp_asv_set_boolean:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_boolean(), tp_g_value_slice_new_boolean()
 * Since: 0.7.29
 */
void
tp_asv_set_boolean (GHashTable *asv,
                    const gchar *key,
                    gboolean value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key, tp_g_value_slice_new_boolean (value));
}

/**
 * tp_asv_get_bytes:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 *
 * If a value for @key in @asv is present and is an array of bytes
 * (its GType is %DBUS_TYPE_G_UCHAR_ARRAY), return it.
 *
 * Otherwise return %NULL.
 *
 * The returned value is not copied, and is only valid as long as the value
 * for @key in @asv is not removed or altered. Copy it with
 * g_boxed_copy (DBUS_TYPE_G_UCHAR_ARRAY, ...) if you need to keep
 * it for longer.
 *
 * Returns: the string value of @key, or %NULL
 * Since: 0.7.9
 */
const GArray *
tp_asv_get_bytes (const GHashTable *asv,
                   const gchar *key)
{
  GValue *value;

  g_return_val_if_fail (asv != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL || !G_VALUE_HOLDS (value, DBUS_TYPE_G_UCHAR_ARRAY))
    return NULL;

  return g_value_get_boxed (value);
}

/**
 * tp_asv_set_bytes:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @length: the number of bytes to copy
 * @bytes: location of an array of bytes to be copied (this may be %NULL
 * if and only if length is 0)
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_bytes(), tp_g_value_slice_new_bytes()
 * Since: 0.7.29
 */
void
tp_asv_set_bytes (GHashTable *asv,
                  const gchar *key,
                  guint length,
                  gconstpointer bytes)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (!(length > 0 && bytes == NULL));

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_bytes (length, bytes));
}

/**
 * tp_asv_take_bytes:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: a non-NULL #GArray of %guchar, ownership of which will be taken by
 * the #GValue
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_bytes(), tp_g_value_slice_new_take_bytes()
 * Since: 0.7.29
 */
void
tp_asv_take_bytes (GHashTable *asv,
                   const gchar *key,
                   GArray *value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (value != NULL);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_take_bytes (value));
}

/**
 * tp_asv_get_string:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 *
 * If a value for @key in @asv is present and is a string, return it.
 *
 * Otherwise return %NULL.
 *
 * The returned value is not copied, and is only valid as long as the value
 * for @key in @asv is not removed or altered. Copy it with g_strdup() if you
 * need to keep it for longer.
 *
 * Returns: the string value of @key, or %NULL
 * Since: 0.7.9
 */
const gchar *
tp_asv_get_string (const GHashTable *asv,
                   const gchar *key)
{
  GValue *value;

  g_return_val_if_fail (asv != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL || !G_VALUE_HOLDS_STRING (value))
    return NULL;

  return g_value_get_string (value);
}

/**
 * tp_asv_set_string:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_string(), tp_g_value_slice_new_string()
 * Since: 0.7.29
 */
void
tp_asv_set_string (GHashTable *asv,
                   const gchar *key,
                   const gchar *value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key, tp_g_value_slice_new_string (value));
}

/**
 * tp_asv_take_string:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_string(),
 * tp_g_value_slice_new_take_string()
 * Since: 0.7.29
 */
void
tp_asv_take_string (GHashTable *asv,
                    const gchar *key,
                    gchar *value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_take_string (value));
}

/**
 * tp_asv_set_static_string:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_string(),
 * tp_g_value_slice_new_static_string()
 * Since: 0.7.29
 */
void
tp_asv_set_static_string (GHashTable *asv,
                          const gchar *key,
                          const gchar *value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_static_string (value));
}

/**
 * tp_asv_get_int32:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 * @valid: Either %NULL, or a location in which to store %TRUE on success or
 *    %FALSE on failure
 *
 * If a value for @key in @asv is present, has an integer type used by
 * dbus-glib (guchar, gint, guint, gint64 or guint64) and fits in the
 * range of a gint32, return it, and if @valid is not %NULL, set *@valid to
 * %TRUE.
 *
 * Otherwise, return 0, and if @valid is not %NULL, set *@valid to %FALSE.
 *
 * Returns: the 32-bit signed integer value of @key, or 0
 * Since: 0.7.9
 */
gint32
tp_asv_get_int32 (const GHashTable *asv,
                  const gchar *key,
                  gboolean *valid)
{
  gint64 i;
  guint64 u;
  gint32 ret;
  GValue *value;

  g_return_val_if_fail (asv != NULL, 0);
  g_return_val_if_fail (key != NULL, 0);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL)
    goto return_invalid;

  switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_UCHAR:
      ret = g_value_get_uchar (value);
      break;

    case G_TYPE_UINT:
      u = g_value_get_uint (value);

      if (G_UNLIKELY (u > G_MAXINT32))
        goto return_invalid;

      ret = u;
      break;

    case G_TYPE_INT:
      ret = g_value_get_int (value);
      break;

    case G_TYPE_INT64:
      i = g_value_get_int64 (value);

      if (G_UNLIKELY (i < G_MININT32 || i > G_MAXINT32))
        goto return_invalid;

      ret = i;
      break;

    case G_TYPE_UINT64:
      u = g_value_get_uint64 (value);

      if (G_UNLIKELY (u > G_MAXINT32))
        goto return_invalid;

      ret = u;
      break;

    default:
      goto return_invalid;
    }

  if (valid != NULL)
    *valid = TRUE;

  return ret;

return_invalid:
  if (valid != NULL)
    *valid = FALSE;

  return 0;
}

/**
 * tp_asv_set_int32:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_int32(), tp_g_value_slice_new_int()
 * Since: 0.7.29
 */
void
tp_asv_set_int32 (GHashTable *asv,
                  const gchar *key,
                  gint32 value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key, tp_g_value_slice_new_int (value));
}

/**
 * tp_asv_get_uint32:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 * @valid: Either %NULL, or a location in which to store %TRUE on success or
 *    %FALSE on failure
 *
 * If a value for @key in @asv is present, has an integer type used by
 * dbus-glib (guchar, gint, guint, gint64 or guint64) and fits in the
 * range of a guint32, return it, and if @valid is not %NULL, set *@valid to
 * %TRUE.
 *
 * Otherwise, return 0, and if @valid is not %NULL, set *@valid to %FALSE.
 *
 * Returns: the 32-bit unsigned integer value of @key, or 0
 * Since: 0.7.9
 */
guint32
tp_asv_get_uint32 (const GHashTable *asv,
                   const gchar *key,
                   gboolean *valid)
{
  gint64 i;
  guint64 u;
  guint32 ret;
  GValue *value;

  g_return_val_if_fail (asv != NULL, 0);
  g_return_val_if_fail (key != NULL, 0);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL)
    goto return_invalid;

  switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_UCHAR:
      ret = g_value_get_uchar (value);
      break;

    case G_TYPE_UINT:
      ret = g_value_get_uint (value);
      break;

    case G_TYPE_INT:
      i = g_value_get_int (value);

      if (G_UNLIKELY (i < 0))
        goto return_invalid;

      ret = i;
      break;

    case G_TYPE_INT64:
      i = g_value_get_int64 (value);

      if (G_UNLIKELY (i < 0 || i > G_MAXUINT32))
        goto return_invalid;

      ret = i;
      break;

    case G_TYPE_UINT64:
      u = g_value_get_uint64 (value);

      if (G_UNLIKELY (u > G_MAXUINT32))
        goto return_invalid;

      ret = u;
      break;

    default:
      goto return_invalid;
    }

  if (valid != NULL)
    *valid = TRUE;

  return ret;

return_invalid:
  if (valid != NULL)
    *valid = FALSE;

  return 0;
}

/**
 * tp_asv_set_uint32:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_uint32(), tp_g_value_slice_new_uint()
 * Since: 0.7.29
 */
void
tp_asv_set_uint32 (GHashTable *asv,
                   const gchar *key,
                   guint32 value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key, tp_g_value_slice_new_uint (value));
}

/**
 * tp_asv_get_int64:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 * @valid: Either %NULL, or a location in which to store %TRUE on success or
 *    %FALSE on failure
 *
 * If a value for @key in @asv is present, has an integer type used by
 * dbus-glib (guchar, gint, guint, gint64 or guint64) and fits in the
 * range of a gint64, return it, and if @valid is not %NULL, set *@valid to
 * %TRUE.
 *
 * Otherwise, return 0, and if @valid is not %NULL, set *@valid to %FALSE.
 *
 * Returns: the 64-bit signed integer value of @key, or 0
 * Since: 0.7.9
 */
gint64
tp_asv_get_int64 (const GHashTable *asv,
                  const gchar *key,
                  gboolean *valid)
{
  gint64 ret;
  guint64 u;
  GValue *value;

  g_return_val_if_fail (asv != NULL, 0);
  g_return_val_if_fail (key != NULL, 0);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL)
    goto return_invalid;

  switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_UCHAR:
      ret = g_value_get_uchar (value);
      break;

    case G_TYPE_UINT:
      ret = g_value_get_uint (value);
      break;

    case G_TYPE_INT:
      ret = g_value_get_int (value);
      break;

    case G_TYPE_INT64:
      ret = g_value_get_int64 (value);
      break;

    case G_TYPE_UINT64:
      u = g_value_get_uint64 (value);

      if (G_UNLIKELY (u > G_MAXINT64))
        goto return_invalid;

      ret = u;
      break;

    default:
      goto return_invalid;
    }

  if (valid != NULL)
    *valid = TRUE;

  return ret;

return_invalid:
  if (valid != NULL)
    *valid = FALSE;

  return 0;
}

/**
 * tp_asv_set_int64:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_int64(), tp_g_value_slice_new_int64()
 * Since: 0.7.29
 */
void
tp_asv_set_int64 (GHashTable *asv,
                  const gchar *key,
                  gint64 value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key, tp_g_value_slice_new_int64 (value));
}

/**
 * tp_asv_get_uint64:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 * @valid: Either %NULL, or a location in which to store %TRUE on success or
 *    %FALSE on failure
 *
 * If a value for @key in @asv is present, has an integer type used by
 * dbus-glib (guchar, gint, guint, gint64 or guint64) and is non-negative,
 * return it, and if @valid is not %NULL, set *@valid to %TRUE.
 *
 * Otherwise, return 0, and if @valid is not %NULL, set *@valid to %FALSE.
 *
 * Returns: the 64-bit unsigned integer value of @key, or 0
 * Since: 0.7.9
 */
guint64
tp_asv_get_uint64 (const GHashTable *asv,
                   const gchar *key,
                   gboolean *valid)
{
  gint64 tmp;
  guint64 ret;
  GValue *value;

  g_return_val_if_fail (asv != NULL, 0);
  g_return_val_if_fail (key != NULL, 0);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL)
    goto return_invalid;

  switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_UCHAR:
      ret = g_value_get_uchar (value);
      break;

    case G_TYPE_UINT:
      ret = g_value_get_uint (value);
      break;

    case G_TYPE_INT:
      tmp = g_value_get_int (value);

      if (G_UNLIKELY (tmp < 0))
        goto return_invalid;

      ret = tmp;
      break;

    case G_TYPE_INT64:
      tmp = g_value_get_int64 (value);

      if (G_UNLIKELY (tmp < 0))
        goto return_invalid;

      ret = tmp;
      break;

    case G_TYPE_UINT64:
      ret = g_value_get_uint64 (value);
      break;

    default:
      goto return_invalid;
    }

  if (valid != NULL)
    *valid = TRUE;

  return ret;

return_invalid:
  if (valid != NULL)
    *valid = FALSE;

  return 0;
}

/**
 * tp_asv_set_uint64:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_uint64(), tp_g_value_slice_new_uint64()
 * Since: 0.7.29
 */
void
tp_asv_set_uint64 (GHashTable *asv,
                   const gchar *key,
                   guint64 value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key, tp_g_value_slice_new_uint64 (value));
}

/**
 * tp_asv_get_double:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 * @valid: Either %NULL, or a location in which to store %TRUE on success or
 *    %FALSE on failure
 *
 * If a value for @key in @asv is present and has any numeric type used by
 * dbus-glib (guchar, gint, guint, gint64, guint64 or gdouble),
 * return it as a double, and if @valid is not %NULL, set *@valid to %TRUE.
 *
 * Otherwise, return 0.0, and if @valid is not %NULL, set *@valid to %FALSE.
 *
 * Returns: the double precision floating-point value of @key, or 0.0
 * Since: 0.7.9
 */
gdouble
tp_asv_get_double (const GHashTable *asv,
                   const gchar *key,
                   gboolean *valid)
{
  gdouble ret;
  GValue *value;

  g_return_val_if_fail (asv != NULL, 0.0);
  g_return_val_if_fail (key != NULL, 0.0);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL)
    goto return_invalid;

  switch (G_VALUE_TYPE (value))
    {
    case G_TYPE_DOUBLE:
      ret = g_value_get_double (value);
      break;

    case G_TYPE_UCHAR:
      ret = g_value_get_uchar (value);
      break;

    case G_TYPE_UINT:
      ret = g_value_get_uint (value);
      break;

    case G_TYPE_INT:
      ret = g_value_get_int (value);
      break;

    case G_TYPE_INT64:
      ret = g_value_get_int64 (value);
      break;

    case G_TYPE_UINT64:
      ret = g_value_get_uint64 (value);
      break;

    default:
      goto return_invalid;
    }

  if (valid != NULL)
    *valid = TRUE;

  return ret;

return_invalid:
  if (valid != NULL)
    *valid = FALSE;

  return 0;
}

/**
 * tp_asv_set_double:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_double(), tp_g_value_slice_new_double()
 * Since: 0.7.29
 */
void
tp_asv_set_double (GHashTable *asv,
                   const gchar *key,
                   gdouble value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key, tp_g_value_slice_new_double (value));
}

/**
 * tp_asv_get_object_path:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 *
 * If a value for @key in @asv is present and is an object path, return it.
 *
 * Otherwise return %NULL.
 *
 * The returned value is not copied, and is only valid as long as the value
 * for @key in @asv is not removed or altered. Copy it with g_strdup() if you
 * need to keep it for longer.
 *
 * Returns: the object-path value of @key, or %NULL
 * Since: 0.7.9
 */
const gchar *
tp_asv_get_object_path (const GHashTable *asv,
                        const gchar *key)
{
  GValue *value;

  g_return_val_if_fail (asv != NULL, 0);
  g_return_val_if_fail (key != NULL, 0);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL || !G_VALUE_HOLDS (value, DBUS_TYPE_G_OBJECT_PATH))
    return NULL;

  return g_value_get_boxed (value);
}

/**
 * tp_asv_set_object_path:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_object_path(),
 * tp_g_value_slice_new_object_path()
 * Since: 0.7.29
 */
void
tp_asv_set_object_path (GHashTable *asv,
                        const gchar *key,
                        const gchar *value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_object_path (value));
}

/**
 * tp_asv_take_object_path:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_object_path(),
 * tp_g_value_slice_new_take_object_path()
 * Since: 0.7.29
 */
void
tp_asv_take_object_path (GHashTable *asv,
                         const gchar *key,
                         gchar *value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_take_object_path (value));
}

/**
 * tp_asv_set_static_object_path:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_object_path(),
 * tp_g_value_slice_new_static_object_path()
 * Since: 0.7.29
 */
void
tp_asv_set_static_object_path (GHashTable *asv,
                               const gchar *key,
                               const gchar *value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_static_object_path (value));
}

/**
 * tp_asv_get_boxed:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 * @type: The type that the key's value should have, which must be derived
 *  from %G_TYPE_BOXED
 *
 * If a value for @key in @asv is present and is of the desired type,
 * return it.
 *
 * Otherwise return %NULL.
 *
 * The returned value is not copied, and is only valid as long as the value
 * for @key in @asv is not removed or altered. Copy it, for instance with
 * g_boxed_copy(), if you need to keep it for longer.
 *
 * Returns: the value of @key, or %NULL
 * Since: 0.7.9
 */
gpointer
tp_asv_get_boxed (const GHashTable *asv,
                  const gchar *key,
                  GType type)
{
  GValue *value;

  g_return_val_if_fail (asv != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);
  g_return_val_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED, NULL);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL || !G_VALUE_HOLDS (value, type))
    return NULL;

  return g_value_get_boxed (value);
}

/**
 * tp_asv_set_boxed:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @type: the type of the key's value, which must be derived from %G_TYPE_BOXED
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_boxed(), tp_g_value_slice_new_boxed()
 * Since: 0.7.29
 */
void
tp_asv_set_boxed (GHashTable *asv,
                  const gchar *key,
                  GType type,
                  gconstpointer value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_boxed (type, value));
}

/**
 * tp_asv_take_boxed:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @type: the type of the key's value, which must be derived from %G_TYPE_BOXED
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_boxed(), tp_g_value_slice_new_take_boxed()
 * Since: 0.7.29
 */
void
tp_asv_take_boxed (GHashTable *asv,
                   const gchar *key,
                   GType type,
                   gpointer value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_take_boxed (type, value));
}

/**
 * tp_asv_set_static_boxed:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @type: the type of the key's value, which must be derived from %G_TYPE_BOXED
 * @value: value
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_boxed(),
 * tp_g_value_slice_new_static_boxed()
 * Since: 0.7.29
 */
void
tp_asv_set_static_boxed (GHashTable *asv,
                         const gchar *key,
                         GType type,
                         gconstpointer value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_static_boxed (type, value));
}

/**
 * tp_asv_get_strv:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 *
 * If a value for @key in @asv is present and is an array of strings (strv),
 * return it.
 *
 * Otherwise return %NULL.
 *
 * The returned value is not copied, and is only valid as long as the value
 * for @key in @asv is not removed or altered. Copy it with g_strdupv() if you
 * need to keep it for longer.
 *
 * Returns: the %NULL-terminated string-array value of @key, or %NULL
 * Since: 0.7.9
 */
const gchar * const *
tp_asv_get_strv (const GHashTable *asv,
                 const gchar *key)
{
  GValue *value;

  g_return_val_if_fail (asv != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  value = g_hash_table_lookup ((GHashTable *) asv, key);

  if (value == NULL || !G_VALUE_HOLDS (value, G_TYPE_STRV))
    return NULL;

  return g_value_get_boxed (value);
}

/**
 * tp_asv_set_strv:
 * @asv: a #GHashTable created with tp_asv_new()
 * @key: string key
 * @value: a %NULL-terminated string array
 *
 * Stores the value in the map.
 *
 * The value is stored as a slice-allocated GValue.
 *
 * See Also: tp_asv_new(), tp_asv_get_strv()
 * Since: 0.7.29
 */
void
tp_asv_set_strv (GHashTable *asv,
                 const gchar *key,
                 gchar **value)
{
  g_return_if_fail (asv != NULL);
  g_return_if_fail (key != NULL);

  g_hash_table_insert (asv, (char *) key,
      tp_g_value_slice_new_boxed (G_TYPE_STRV, value));
}

/**
 * tp_asv_lookup:
 * @asv: A GHashTable where the keys are strings and the values are GValues
 * @key: The key to look up
 *
 * If a value for @key in @asv is present, return it. Otherwise return %NULL.
 *
 * The returned value is not copied, and is only valid as long as the value
 * for @key in @asv is not removed or altered. Copy it with (for instance)
 * g_value_copy() if you need to keep it for longer.
 *
 * Returns: the value of @key, or %NULL
 * Since: 0.7.9
 */
const GValue *
tp_asv_lookup (const GHashTable *asv,
               const gchar *key)
{
  g_return_val_if_fail (asv != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  return g_hash_table_lookup ((GHashTable *) asv, key);
}

/**
 * tp_asv_dump:
 * @asv: a #GHashTable created with tp_asv_new()
 *
 * Dumps the a{sv} map to the debugging console.
 *
 * The purpose of this function is give the programmer the ability to easily
 * inspect the contents of an a{sv} map for debugging purposes.
 */
void
tp_asv_dump (GHashTable *asv)
{
  GHashTableIter iter;
  char *key;
  GValue *value;

  g_return_if_fail (asv != NULL);

  g_hash_table_iter_init (&iter, asv);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &value))
  {
    char *str = g_strdup_value_contents (value);
    g_debug ("'%s' : %s", key, str);
    g_free (str);
  }
}
