/*
 * base-connection-manager.c - Source for TpBaseConnectionManager
 *
 * Copyright (C) 2007-2008 Collabora Ltd.
 * Copyright (C) 2007-2008 Nokia Corporation
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
 * SECTION:base-connection-manager
 * @title: TpBaseConnectionManager
 * @short_description: base class for #TpSvcConnectionManager implementations
 * @see_also: #TpBaseConnection, #TpSvcConnectionManager, #run
 *
 * This base class makes it easier to write #TpSvcConnectionManager
 * implementations by managing the D-Bus object path and bus name,
 * and maintaining a table of active connections. Subclasses should usually
 * only need to override the members of the class data structure.
 */

#include <telepathy-glib/base-connection-manager.h>

#include <string.h>

#include <dbus/dbus-protocol.h>

#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG TP_DEBUG_PARAMS
#include "debug-internal.h"

/**
 * TpCMParamSpec:
 * @name: Name as passed over D-Bus
 * @dtype: D-Bus type signature. We currently support 16- and 32-bit integers
 *         (@gtype is INT), 16- and 32-bit unsigned integers (gtype is UINT),
 *         strings (gtype is STRING) and booleans (gtype is BOOLEAN).
 * @gtype: GLib type, derived from @dtype as above
 * @flags: Some combination of TP_CONN_MGR_PARAM_FLAG_foo
 * @def: Default value, as a (const gchar *) for string parameters, or
         using #GINT_TO_POINTER or #GUINT_TO_POINTER for integer parameters
 * @offset: Offset of the parameter in the opaque data structure, if
 *          appropriate. The member at that offset is expected to be a gint,
 *          guint, (gchar *) or gboolean, depending on @gtype. The default
 *          parameter setter, #tp_cm_param_setter_offset, uses this field.
 * @filter: A callback which is used to validate or normalize the user-provided
 *          value before it is written into the opaque data structure
 * @filter_data: Arbitrary opaque data intended for use by the filter function
 * @setter_data: Arbitrary opaque data intended for use by the setter function
 *               instead of or in addition to @offset.
 *
 * Structure representing a connection manager parameter, as accepted by
 * RequestConnection.
 *
 * In addition to the fields documented here, there is one gpointer field
 * which must currently be %NULL. A meaning may be defined for it in a
 * future version of telepathy-glib.
 */

/**
 * TpCMParamFilter:
 * @paramspec: The parameter specification. The filter is likely to use
 *  name (for the error message if the value is invalid) and filter_data.
 * @value: The value for that parameter provided by the user.
 *  May be changed to contain a different value of the same type, if
 *  some sort of normalization is required
 * @error: Used to raise %TP_ERROR_INVALID_ARGUMENT if the given value is
 *  rejected
 *
 * Signature of a callback used to validate and/or normalize user-provided
 * CM parameter values.
 *
 * Returns: %TRUE to accept, %FALSE (with @error set) to reject
 */

/**
 * TpCMParamSetter:
 * @paramspec: The parameter specification.  The setter is likely to use
 *  some combination of the name, offset and setter_data fields.
 * @value: The value for that parameter provided by the user.
 * @params: An opaque data structure, created by
 *  #TpCMProtocolSpec.params_new.
 *
 * The signature of a callback used to set a parameter within the opaque
 * data structure used for a protocol.
 *
 * Since: 0.7.0
 */

/**
 * TpCMProtocolSpec:
 * @name: The name which should be passed to RequestConnection for this
 *        protocol.
 * @parameters: An array of #TpCMParamSpec representing the valid parameters
 *              for this protocol, terminated by a #TpCMParamSpec whose name
 *              entry is NULL.
 * @params_new: A function which allocates an opaque data structure to store
 *              the parsed parameters for this protocol. The offset fields
 *              in the members of the @parameters array refer to offsets
 *              within this opaque structure.
 * @params_free: A function which deallocates the opaque data structure
 *               provided by #params_new, including deallocating its
 *               data members (currently, only strings) if necessary.
 * @set_param: A function which sets a parameter within the opaque data
 *             structure provided by #params_new. If %NULL,
 *             tp_cm_param_setter_offset() will be used. (New in 0.7.0 -
 *             previously, code equivalent to tp_cm_param_setter_offset() was
 *             always used.)
 *
 * Structure representing a connection manager protocol.
 *
 * In addition to the fields documented here, there are three gpointer fields
 * which must currently be %NULL. A meaning may be defined for these in a
 * future version of telepathy-glib.
 */

/**
 * TpBaseConnectionManager:
 * @parent: The parent instance structure
 * @priv: Pointer to opaque private data
 *
 * A base class for connection managers. There are no interesting public
 * fields in the instance structure.
 */

/**
 * TpBaseConnectionManagerClass:
 * @parent_class: The parent class
 * @cm_dbus_name: The name of this connection manager, as used to construct
 *  D-Bus object paths and bus names. Must contain only letters, digits
 *  and underscores, and may not start with a digit. Must be filled in by
 *  subclasses in their class_init function.
 * @protocol_params: An array of #TpCMProtocolSpec structures representing
 *  the protocols this connection manager supports, terminated by a structure
 *  whose name member is %NULL.
 * @new_connection: A #TpBaseConnectionManagerNewConnFunc used to construct
 *  new connections. Must be filled in by subclasses in their class_init
 *  function.
 *
 * The class structure for #TpBaseConnectionManager.
 *
 * In addition to the fields documented here, there are four gpointer fields
 * which must currently be %NULL (a meaning may be defined for these in a
 * future version of telepathy-glib), and a pointer to opaque private data.
 *
 * Changed in 0.7.1: it is a fatal error for @cm_dbus_name not to conform to
 * the specification.
 */

/**
 * TpBaseConnectionManagerNewConnFunc:
 * @self: The connection manager implementation
 * @proto: The protocol name from the D-Bus request
 * @params_present: A set of integers representing the indexes into the
 *                  array of #TpCMParamSpec of those parameters that were
 *                  present in the request
 * @parsed_params: An opaque data structure as returned by the protocol's
 *                 params_new function, populated according to the
 *                 parameter specifications
 * @error: if not %NULL, used to indicate the error if %NULL is returned
 *
 * A function that will return a new connection according to the
 * parsed parameters; used to implement RequestConnection.
 *
 * The connection manager base class will register the bus name for the
 * new connection, and place a reference to it in its table of
 * connections until the connection's shutdown process finishes.
 *
 * Returns: the new connection object, or %NULL on error.
 */

static void service_iface_init (gpointer, gpointer);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE(TpBaseConnectionManager,
    tp_base_connection_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_MANAGER,
        service_iface_init))

#define TP_BASE_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((TpBaseConnectionManagerPrivate *) obj->priv)

typedef struct _TpBaseConnectionManagerPrivate
{
  /* if TRUE, the object has gone away */
  gboolean dispose_has_run;
  /* used as a set: key is TpBaseConnection *, value is TRUE */
  GHashTable *connections;
} TpBaseConnectionManagerPrivate;

/* signal enum */
enum
{
    NO_MORE_CONNECTIONS,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

static void
tp_base_connection_manager_dispose (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
}

static void
tp_base_connection_manager_finalize (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_destroy (priv->connections);

  G_OBJECT_CLASS (tp_base_connection_manager_parent_class)->finalize (object);
}

static GObject *
tp_base_connection_manager_constructor (GType type,
                                        guint n_params,
                                        GObjectConstructParam *params)
{
  GObjectClass *object_class =
      (GObjectClass *) tp_base_connection_manager_parent_class;
  TpBaseConnectionManager *self =
      TP_BASE_CONNECTION_MANAGER (object_class->constructor (type, n_params,
            params));
  TpBaseConnectionManagerClass *cls =
      TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);

  g_assert (tp_connection_manager_check_valid_name (cls->cm_dbus_name, NULL));
  g_assert (cls->protocol_params != NULL);
  g_assert (cls->new_connection != NULL);

  return (GObject *) self;
}

static void
tp_base_connection_manager_class_init (TpBaseConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TpBaseConnectionManagerPrivate));
  object_class->constructor = tp_base_connection_manager_constructor;
  object_class->dispose = tp_base_connection_manager_dispose;
  object_class->finalize = tp_base_connection_manager_finalize;

  /**
   * TpBaseConnectionManager::no-more-connections:
   *
   * Emitted when the table of active connections becomes empty.
   * tp_run_connection_manager() uses this to detect when to shut down the
   * connection manager.
   */
  signals[NO_MORE_CONNECTIONS] =
    g_signal_new ("no-more-connections",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
tp_base_connection_manager_init (TpBaseConnectionManager *self)
{
  TpBaseConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TP_TYPE_BASE_CONNECTION_MANAGER, TpBaseConnectionManagerPrivate);

  self->priv = priv;

  priv->connections = g_hash_table_new (g_direct_hash, g_direct_equal);
}

/**
 * connection_shutdown_finished_cb:
 * @conn: #TpBaseConnection
 * @data: data passed in callback
 *
 * Signal handler called when a connection object disconnects.
 * When they become disconnected, we can unref and discard
 * them, and they will disappear from the bus.
 */
static void
connection_shutdown_finished_cb (TpBaseConnection *conn,
                                 gpointer data)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (data);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_assert (g_hash_table_lookup (priv->connections, conn));
  g_hash_table_remove (priv->connections, conn);

  g_object_unref (conn);

  DEBUG ("dereferenced connection");
  if (g_hash_table_size (priv->connections) == 0)
    {
      g_signal_emit (self, signals[NO_MORE_CONNECTIONS], 0);
    }
}

/* Parameter parsing */

static gboolean
get_parameters (const TpCMProtocolSpec *protos,
                const char *proto,
                const TpCMProtocolSpec **ret,
                GError **error)
{
  guint i;

  for (i = 0; protos[i].name; i++)
    {
      if (!strcmp (proto, protos[i].name))
        {
          *ret = protos + i;
          return TRUE;
        }
    }

  DEBUG ("unknown protocol %s", proto);

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "unknown protocol %s", proto);

  return FALSE;
}

static GValue *
param_default_value (const TpCMParamSpec *param)
{
  GValue *value;

  value = tp_g_value_slice_new (param->gtype);

  /* If HAS_DEFAULT is false, we don't really care what the value is, so we'll
   * just use whatever's in the user-supplied param spec. As long as we're
   * careful to accept NULL, that should be fine. */

  switch (param->dtype[0])
    {
      case DBUS_TYPE_STRING:
        g_assert (param->gtype == G_TYPE_STRING);
        if (param->def == NULL)
          g_value_set_static_string (value, "");
        else
          g_value_set_static_string (value, param->def);
        break;
      case DBUS_TYPE_INT16:
      case DBUS_TYPE_INT32:
        g_assert (param->gtype == G_TYPE_INT);
        g_value_set_int (value, GPOINTER_TO_INT (param->def));
        break;
      case DBUS_TYPE_UINT16:
      case DBUS_TYPE_UINT32:
        g_assert (param->gtype == G_TYPE_UINT);
        g_value_set_uint (value, GPOINTER_TO_UINT (param->def));
        break;
      case DBUS_TYPE_BOOLEAN:
        g_assert (param->gtype == G_TYPE_BOOLEAN);
        g_value_set_boolean (value, GPOINTER_TO_INT (param->def));
        break;
      default:
        g_error ("parameter_defaults: encountered unknown type %s on "
            "argument %s", param->dtype, param->name);
    }

  return value;
}

/**
 * tp_cm_param_setter_offset:
 * @paramspec: A parameter specification with offset set to some
 *  meaningful value.
 * @value: The value for that parameter, either provided by the user or
 *  constructed from the parameter's default.
 * @params: An opaque data structure such that the address at (@params +
 *  @paramspec->offset) is a valid pointer to a variable of the
 *  appropriate type.
 *
 * A #TpCMParamSetter which sets parameters by dereferencing an offset
 * from @params.  If @paramspec->offset is G_MAXSIZE, the parameter is
 * deemed obsolete, and is accepted but ignored.
 *
 * Since: 0.7.0
 */
void
tp_cm_param_setter_offset (const TpCMParamSpec *paramspec,
                           const GValue *value,
                           gpointer params)
{
  char *params_mem = params;

  if (paramspec->offset == G_MAXSIZE)
    {
      /* quietly ignore any obsolete params provided */
      return;
    }

  switch (paramspec->dtype[0])
    {
      case DBUS_TYPE_STRING:
        {
          gchar **save_to = (gchar **) (params_mem + paramspec->offset);
          const gchar *str;

          g_assert (paramspec->gtype == G_TYPE_STRING);
          str = g_value_get_string (value);
          g_free (*save_to);
          if (str == NULL)
            {
              *save_to = g_strdup ("");
            }
          else
            {
              *save_to = g_value_dup_string (value);
            }
          if (DEBUGGING)
            {
              if (strstr (paramspec->name, "password") != NULL)
                DEBUG ("%s = <hidden>", paramspec->name);
              else
                DEBUG ("%s = \"%s\"", paramspec->name, *save_to);
            }
        }
        break;
      case DBUS_TYPE_INT16:
      case DBUS_TYPE_INT32:
        {
          gint *save_to = (gint *) (params_mem + paramspec->offset);
          gint i = g_value_get_int (value);

          g_assert (paramspec->gtype == G_TYPE_INT);
          *save_to = i;
          DEBUG ("%s = %d = 0x%x", paramspec->name, i, i);
        }
        break;
      case DBUS_TYPE_UINT16:
      case DBUS_TYPE_UINT32:
        {
          guint *save_to = (guint *) (params_mem + paramspec->offset);
          guint i = g_value_get_uint (value);

          g_assert (paramspec->gtype == G_TYPE_UINT);
          *save_to = i;
          DEBUG ("%s = %u = 0x%x", paramspec->name, i, i);
        }
        break;
      case DBUS_TYPE_BOOLEAN:
        {
          gboolean *save_to = (gboolean *) (params_mem + paramspec->offset);
          gboolean b = g_value_get_boolean (value);

          g_assert (paramspec->gtype == G_TYPE_BOOLEAN);
          g_assert (b == TRUE || b == FALSE);
          *save_to = b;
          DEBUG ("%s = %s", paramspec->name, b ? "TRUE" : "FALSE");
        }
        break;
      case DBUS_TYPE_ARRAY:
        switch (paramspec->dtype[1])
          {
            case DBUS_TYPE_BYTE:
              {
                GArray **save_to = (GArray **) (params_mem + paramspec->offset);
                GArray *a = g_value_get_boxed (value);

                if (*save_to != NULL)
                  {
                    g_array_free (*save_to, TRUE);
                  }
                *save_to = g_array_sized_new (FALSE, FALSE, sizeof(guint8), a->len);
                g_array_append_vals (*save_to, a->data, a->len);
                DEBUG ("%s = ...[%u]", paramspec->name, a->len);
              }
              break;
            default:
              g_error ("%s: encountered unhandled D-Bus array type %s on "
                       "argument %s", G_STRFUNC, paramspec->dtype,
                       paramspec->name);
              g_assert_not_reached ();
          }
        break;
      default:
        g_error ("%s: encountered unhandled D-Bus type %s on argument %s",
                 G_STRFUNC, paramspec->dtype, paramspec->name);
        g_assert_not_reached ();
    }
}

static void
set_param_from_default (const TpCMParamSpec *paramspec,
                        const TpCMParamSetter set_param,
                        gpointer params)
{
  GValue *value = param_default_value (paramspec);
  set_param (paramspec, value, params);
  tp_g_value_slice_free (value);
}

static gboolean
set_param_from_value (const TpCMParamSpec *paramspec,
                      GValue *value,
                      const TpCMParamSetter set_param,
                      void *params,
                      GError **error)
{
  if (G_VALUE_TYPE (value) != paramspec->gtype)
    {
      DEBUG ("expected type %s for parameter %s, got %s",
               g_type_name (paramspec->gtype), paramspec->name,
               G_VALUE_TYPE_NAME (value));
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "expected type %s for account parameter %s, got %s",
          g_type_name (paramspec->gtype), paramspec->name,
          G_VALUE_TYPE_NAME (value));
      return FALSE;
    }

  if (paramspec->filter != NULL)
    {
      if (!(paramspec->filter) (paramspec, value, error))
        {
          DEBUG ("parameter %s rejected by filter function: %s",
              paramspec->name, error ? (*error)->message : "(error ignored)");
          return FALSE;
        }

      /* the filter may not change the type of the GValue */
      g_return_val_if_fail (G_VALUE_TYPE (value) == paramspec->gtype, FALSE);
    }

  set_param (paramspec, value, params);

  return TRUE;
}

static void
report_unknown_param (gpointer key, gpointer value, gpointer user_data)
{
  const char *arg = (const char *) key;
  GString **error_str = (GString **) user_data;
  *error_str = g_string_append_c (*error_str, ' ');
  *error_str = g_string_append (*error_str, arg);
}

static gboolean
parse_parameters (const TpCMParamSpec *paramspec,
                  GHashTable *provided,
                  TpIntSet *params_present,
                  const TpCMParamSetter set_param,
                  void *params,
                  GError **error)
{
  int i;
  guint mandatory_flag = TP_CONN_MGR_PARAM_FLAG_REQUIRED;
  GValue *value;

  value = g_hash_table_lookup (provided, "register");
  if (value != NULL && G_VALUE_TYPE(value) == G_TYPE_BOOLEAN &&
      g_value_get_boolean (value))
    {
      mandatory_flag = TP_CONN_MGR_PARAM_FLAG_REGISTER;
    }

  for (i = 0; paramspec[i].name; i++)
    {
      value = g_hash_table_lookup (provided, paramspec[i].name);

      if (value == NULL)
        {
          if (paramspec[i].flags & mandatory_flag)
            {
              DEBUG ("missing mandatory param %s", paramspec[i].name);
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "missing mandatory account parameter %s", paramspec[i].name);
              return FALSE;
            }
          else if (paramspec[i].flags & TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT)
            {
              /* FIXME: Should we add it to params_present? */
              set_param_from_default (&paramspec[i], set_param, params);
            }
          else
            {
              DEBUG ("%s not given, using default behaviour",
                  paramspec[i].name);
            }
        }
      else
        {
          if (!set_param_from_value (&paramspec[i], value, set_param, params,
                error))
            {
              return FALSE;
            }

          tp_intset_add (params_present, i);

          g_hash_table_remove (provided, paramspec[i].name);
        }
    }

  if (g_hash_table_size (provided) != 0)
    {
      gchar *error_txt;
      GString *error_str = g_string_new ("unknown parameters provided:");

      g_hash_table_foreach (provided, report_unknown_param, &error_str);
      error_txt = g_string_free (error_str, FALSE);

      DEBUG ("%s", error_txt);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "%s", error_txt);
      g_free (error_txt);
      return FALSE;
    }

  return TRUE;
}


/**
 * tp_base_connection_manager_get_parameters
 *
 * Implements D-Bus method GetParameters
 * on interface org.freedesktop.Telepathy.ConnectionManager
 */
static void
tp_base_connection_manager_get_parameters (TpSvcConnectionManager *iface,
                                           const gchar *proto,
                                           DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error = NULL;
  const TpCMProtocolSpec *protospec = NULL;
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (iface);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  GType param_type = TP_STRUCT_TYPE_PARAM_SPEC;
  guint i;

  g_assert (TP_IS_BASE_CONNECTION_MANAGER (iface));
  g_assert (cls->protocol_params != NULL);

  if (!get_parameters (cls->protocol_params, proto, &protospec, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_ptr_array_new ();

  for (i = 0; protospec->parameters[i].name != NULL; i++)
    {
      GValue *def_value;
      GValue param = { 0, };

      g_value_init (&param, param_type);
      g_value_set_static_boxed (&param,
        dbus_g_type_specialized_construct (param_type));

      def_value = param_default_value (protospec->parameters + i);
      dbus_g_type_struct_set (&param,
        0, protospec->parameters[i].name,
        1, protospec->parameters[i].flags,
        2, protospec->parameters[i].dtype,
        3, def_value,
        G_MAXUINT);
      g_value_unset (def_value);
      g_slice_free (GValue, def_value);

      g_ptr_array_add (ret, g_value_get_boxed (&param));
    }

  tp_svc_connection_manager_return_from_get_parameters (context, ret);

  for (i = 0; i < ret->len; i++)
    {
      g_boxed_free (param_type, g_ptr_array_index (ret, i));
    }

  g_ptr_array_free (ret, TRUE);
}


/**
 * tp_base_connection_manager_list_protocols
 *
 * Implements D-Bus method ListProtocols
 * on interface org.freedesktop.Telepathy.ConnectionManager
 */
static void
tp_base_connection_manager_list_protocols (TpSvcConnectionManager *iface,
                                           DBusGMethodInvocation *context)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (iface);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  const char **protocols;
  guint i = 0;

  while (cls->protocol_params[i].name)
    i++;

  protocols = g_new0 (const char *, i + 1);
  for (i = 0; cls->protocol_params[i].name; i++)
    {
      protocols[i] = cls->protocol_params[i].name;
    }
  g_assert (protocols[i] == NULL);

  tp_svc_connection_manager_return_from_list_protocols (
      context, protocols);
  g_free (protocols);
}


/**
 * tp_base_connection_manager_request_connection
 *
 * Implements D-Bus method RequestConnection
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
tp_base_connection_manager_request_connection (TpSvcConnectionManager *iface,
                                               const gchar *proto,
                                               GHashTable *parameters,
                                               DBusGMethodInvocation *context)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (iface);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn;
  gchar *bus_name;
  gchar *object_path;
  GError *error = NULL;
  void *params = NULL;
  TpIntSet *params_present = NULL;
  const TpCMProtocolSpec *protospec = NULL;
  TpCMParamSetter set_param;

  g_assert (TP_IS_BASE_CONNECTION_MANAGER (iface));

  if (!tp_connection_manager_check_valid_protocol_name (proto, &error))
    goto ERROR;

  if (!get_parameters (cls->protocol_params, proto, &protospec, &error))
    {
      goto ERROR;
    }

  g_assert (protospec->parameters != NULL);
  g_assert (protospec->params_new != NULL);
  g_assert (protospec->params_free != NULL);

  params_present = tp_intset_new ();
  params = protospec->params_new ();

  set_param = protospec->set_param;
  if (set_param == NULL)
    set_param = tp_cm_param_setter_offset;

  if (!parse_parameters (protospec->parameters, parameters, params_present,
        set_param, params, &error))
    {
      goto ERROR;
    }

  conn = (cls->new_connection) (self, proto, params_present, params, &error);
  if (!conn)
    {
      goto ERROR;
    }

  /* register on bus and save bus name and object path */
  if (!tp_base_connection_register (conn, cls->cm_dbus_name,
        &bus_name, &object_path, &error))
    {
      DEBUG ("failed: %s", error->message);

      g_object_unref (G_OBJECT (conn));
      goto ERROR;
    }

  /* bind to status change signals from the connection object */
  g_signal_connect (conn, "shutdown-finished",
                    G_CALLBACK (connection_shutdown_finished_cb),
                    self);

  /* store the connection, using a hash table as a set */
  g_hash_table_insert (priv->connections, conn, GINT_TO_POINTER(TRUE));

  /* emit the new connection signal */
  tp_svc_connection_manager_emit_new_connection (
      self, bus_name, object_path, proto);

  tp_svc_connection_manager_return_from_request_connection (
      context, bus_name, object_path);

  g_free (bus_name);
  g_free (object_path);
  goto OUT;

ERROR:
  dbus_g_method_return_error (context, error);
  g_error_free (error);

OUT:
  if (params_present)
    tp_intset_destroy (params_present);
  if (params)
    protospec->params_free (params);
}

/**
 * tp_base_connection_manager_register:
 * @self: The connection manager implementation
 *
 * Register the connection manager with an appropriate object path as
 * determined from its @cm_dbus_name, and register the appropriate well-known
 * bus name.
 *
 * Returns: %TRUE on success, %FALSE (having emitted a warning to stderr)
 *          on failure
 */

gboolean
tp_base_connection_manager_register (TpBaseConnectionManager *self)
{
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GError *error = NULL;
  guint request_name_result;
  TpBaseConnectionManagerClass *cls;
  GString *string;

  g_assert (TP_IS_BASE_CONNECTION_MANAGER (self));
  cls = TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);

  bus = tp_get_bus ();
  bus_proxy = tp_get_bus_proxy ();

  string = g_string_new (TP_CM_BUS_NAME_BASE);
  g_string_append (string, cls->cm_dbus_name);

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", &error,
                          G_TYPE_STRING, string->str,
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    g_error ("Failed to request bus name: %s", error->message);

  if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS)
    {
      g_warning ("Failed to acquire bus name, connection manager already "
          "running?");

      g_string_free (string, TRUE);
      return FALSE;
    }

  g_string_assign (string, TP_CM_OBJECT_PATH_BASE);
  g_string_append (string, cls->cm_dbus_name);
  dbus_g_connection_register_g_object (bus, string->str, G_OBJECT (self));

  g_string_free (string, TRUE);

  return TRUE;
}

static void
service_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionManagerClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_manager_implement_##x (klass, \
    tp_base_connection_manager_##x)
  IMPLEMENT(get_parameters);
  IMPLEMENT(list_protocols);
  IMPLEMENT(request_connection);
#undef IMPLEMENT
}

/**
 * tp_cm_param_filter_uint_nonzero:
 * @paramspec: The parameter specification for a guint parameter
 * @value: A GValue containing a guint, which will not be altered
 * @error: Used to return an error if the guint is 0
 *
 * A #TpCMParamFilter which rejects zero, useful for server port numbers.
 *
 * Returns: %TRUE to accept, %FALSE (with @error set) to reject
 */
gboolean
tp_cm_param_filter_uint_nonzero (const TpCMParamSpec *paramspec,
                                 GValue *value,
                                 GError **error)
{
  if (g_value_get_uint (value) == 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account parameter '%s' may not be set to zero",
          paramspec->name);
      return FALSE;
    }
  return TRUE;
}

/**
 * tp_cm_param_filter_string_nonempty:
 * @paramspec: The parameter specification for a string parameter
 * @value: A GValue containing a string, which will not be altered
 * @error: Used to return an error if the string is empty
 *
 * A #TpCMParamFilter which rejects empty strings.
 *
 * Returns: %TRUE to accept, %FALSE (with @error set) to reject
 */
gboolean
tp_cm_param_filter_string_nonempty (const TpCMParamSpec *paramspec,
                                    GValue *value,
                                    GError **error)
{
  const gchar *str = g_value_get_string (value);

  if (str == NULL || str[0] == '\0')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account parameter '%s' may not be set to an empty string",
          paramspec->name);
      return FALSE;
    }
  return TRUE;
}
