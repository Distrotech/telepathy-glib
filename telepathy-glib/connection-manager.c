/*
 * connection-manager.c - proxy for a Telepathy connection manager
 *
 * Copyright (C) 2007-2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007-2009 Nokia Corporation
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

#include "telepathy-glib/connection-manager.h"

#include <string.h>

#include "telepathy-glib/defs.h"
#include "telepathy-glib/enums.h"
#include "telepathy-glib/errors.h"
#include "telepathy-glib/gtypes.h"
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-internal.h>
#include <telepathy-glib/proxy-subclass.h>
#include "telepathy-glib/util.h"

#define DEBUG_FLAG TP_DEBUG_MANAGER
#include "telepathy-glib/debug-internal.h"

#include "telepathy-glib/_gen/tp-cli-connection-manager-body.h"

/**
 * SECTION:connection-manager
 * @title: TpConnectionManager
 * @short_description: proxy object for a Telepathy connection manager
 * @see_also: #TpConnection
 *
 * #TpConnectionManager objects represent Telepathy connection managers. They
 * can be used to open connections.
 *
 * Since: 0.7.1
 */

/**
 * TpConnectionManagerListCb:
 * @cms: %NULL-terminated array of #TpConnectionManager (the objects will
 *   be unreferenced and the array will be freed after the callback returns,
 *   so the callback must reference any CMs it stores a pointer to),
 *   or %NULL on error
 * @n_cms: number of connection managers in @cms (not including the final
 *  %NULL)
 * @error: %NULL on success, or an error that occurred
 * @user_data: user-supplied data
 * @weak_object: user-supplied weakly referenced object
 *
 * Signature of the callback supplied to tp_list_connection_managers().
 *
 * Since 0.11.UNRELEASED, tp_list_connection_managers() will
 * wait for %TP_CONNECTION_MANAGER_FEATURE_CORE to be prepared (so
 * tp_connection_manager_is_prepared() will return %TRUE) on each
 * connection manager passed to @callback, unless an error occurred while
 * launching that connection manager.
 *
 * Since: 0.7.1
 */

/**
 * TP_CONNECTION_MANAGER_FEATURE_CORE:
 *
 * Expands to a call to a function that returns a quark for the "core" feature
 * on a #TpConnectionManager.
 *
 * When this feature is prepared, [...]
 *
 * (These are the same guarantees offered by the older
 * tp_connection_manager_call_when_ready() mechanism.)
 *
 * One can ask for a feature to be prepared using the
 * tp_proxy_prepare_async() function, and waiting for it to callback.
 *
 * Since: 0.11.UNRELEASED
 */

GQuark
tp_connection_manager_get_feature_quark_core (void)
{
  return g_quark_from_static_string ("tp-connection-manager-feature-core");
}

/**
 * TpCMInfoSource:
 * @TP_CM_INFO_SOURCE_NONE: no information available
 * @TP_CM_INFO_SOURCE_FILE: information came from a .manager file
 * @TP_CM_INFO_SOURCE_LIVE: information came from the connection manager
 *
 * Describes possible sources of information on connection managers'
 * supported protocols.
 *
 * Since: 0.7.1
 */

/**
 * TpConnectionManagerClass:
 *
 * The class of a #TpConnectionManager.
 *
 * Since: 0.7.1
 */

enum
{
  SIGNAL_ACTIVATED,
  SIGNAL_GOT_INFO,
  SIGNAL_EXITED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

enum
{
  PROP_INFO_SOURCE = 1,
  PROP_MANAGER_FILE,
  PROP_ALWAYS_INTROSPECT,
  PROP_CONNECTION_MANAGER,
  N_PROPS
};

/**
 * TpConnectionManager:
 * @parent: The parent class instance
 * @name: The identifier of the connection manager (e.g. "gabble").
 *  Should be considered read-only
 * @protocols: If info_source > %TP_CM_INFO_SOURCE_NONE, a %NULL-terminated
 *  array of pointers to #TpConnectionManagerProtocol structures; otherwise
 *  %NULL. Should be considered read-only
 * @running: %TRUE if the CM is currently known to be running. Should be
 *  considered read-only
 * @always_introspect: %TRUE if the CM will be introspected automatically.
 *  Should be considered read-only: use the
 *  #TpConnectionManager:always-introspect property if you want to change it
 * @info_source: The source of @protocols, or %TP_CM_INFO_SOURCE_NONE
 *  if no info has been discovered yet
 * @reserved_flags: Reserved for future use
 * @priv: Pointer to opaque private data
 *
 * A proxy object for a Telepathy connection manager.
 *
 * This might represent a connection manager which is currently running
 * (in which case it can be introspected) or not (in which case its
 * capabilities can be read from .manager files in the filesystem).
 * Accordingly, this object never emits #TpProxy::invalidated unless all
 * references to it are discarded.
 *
 * Various fields and methods on this object do not work until
 * %TP_CONNECTION_MANAGER_FEATURE_CORE is prepared. Use
 * tp_proxy_prepare_async() to wait for this to happen.
 *
 * Since: 0.7.1
 */

/**
 * TpConnectionManagerParam:
 * @name: The name of this parameter
 * @dbus_signature: This parameter's D-Bus signature
 * @default_value: This parameter's default value, or an arbitrary value
 *  of an appropriate type if %TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT is not
 *  set on this parameter, or an unset GValue if the signature is not
 *  recognised by telepathy-glib
 * @flags: This parameter's flags (a combination of #TpConnMgrParamFlags)
 * @priv: Pointer to opaque private data
 *
 * Structure representing a connection manager parameter.
 *
 * Since: 0.7.1
 */

/**
 * TpConnectionManagerProtocol:
 * @name: The name of this connection manager
 * @params: Array of #TpConnectionManagerParam structures, terminated by
 *  a structure whose @name is %NULL
 *
 * Structure representing a protocol supported by a connection manager.
 * Note that the size of this structure may change, so its size must not be
 * relied on.
 *
 * Since: 0.7.1
 */

struct _TpConnectionManagerPrivate {
    /* absolute path to .manager file */
    gchar *manager_file;

    /* source ID for reading the manager file later */
    guint manager_file_read_idle_id;

    /* source ID for introspecting later */
    guint introspect_idle_id;

    /* TRUE if dispose() has run already */
    unsigned disposed:1;

    /* GPtrArray of TpConnectionManagerProtocol *. This is the implementation
     * for self->protocols.
     *
     * NULL if file_info and live_info are both FALSE
     * Protocols from file, if file_info is TRUE but live_info is FALSE
     * Protocols from last time introspecting the CM succeeded, if live_info
     * is TRUE */
    GPtrArray *protocols;

    /* If we're waiting for a GetParameters, then GPtrArray of g_strdup'd
     * gchar * representing protocols we haven't yet introspected.
     * Otherwise NULL */
    GPtrArray *pending_protocols;
    /* If we're waiting for a GetParameters, then GPtrArray of
     * TpConnectionManagerProtocol * for the introspection that is in
     * progress (will replace ->protocols when finished).
     * Otherwise NULL */
    GPtrArray *found_protocols;

    /* list of WhenReadyContext */
    GList *waiting_for_ready;

    /* the method call currently pending, or NULL if none. */
    TpProxyPendingCall *introspection_call;

    /* FALSE if initial name-owner (if any) hasn't been found yet */
    gboolean name_known;
    /* TRUE if someone asked us to activate but we're putting it off until
     * name_known */
    gboolean want_activation;
};

G_DEFINE_TYPE (TpConnectionManager,
    tp_connection_manager,
    TP_TYPE_PROXY);


static void
_tp_connection_manager_param_copy_contents (
    const TpConnectionManagerParam *in,
    TpConnectionManagerParam *out)
{
  out->name = g_strdup (in->name);
  out->dbus_signature = g_strdup (in->dbus_signature);
  out->flags = in->flags;

  if (G_IS_VALUE (&in->default_value))
    {
      g_value_init (&out->default_value, G_VALUE_TYPE (&in->default_value));
      g_value_copy (&in->default_value, &out->default_value);
    }
}


static void
_tp_connection_manager_param_free_contents (TpConnectionManagerParam *param)
{
  g_free (param->name);
  g_free (param->dbus_signature);

  if (G_IS_VALUE (&param->default_value))
    g_value_unset (&param->default_value);
}


/**
 * tp_connection_manager_param_copy:
 * @in: the #TpConnectionManagerParam to copy
 *
 * <!-- Returns: says it all -->
 *
 * Returns: a newly (slice) allocated #TpConnectionManagerParam, free with
 *  tp_connection_manager_param_free()
 */
TpConnectionManagerParam *
tp_connection_manager_param_copy (const TpConnectionManagerParam *in)
{
  TpConnectionManagerParam *out = g_slice_new0 (TpConnectionManagerParam);

  _tp_connection_manager_param_copy_contents (in, out);

  return out;
}


/**
 * tp_connection_manager_param_free:
 * @param: the #TpConnectionManagerParam to free
 *
 * Frees @param, which was copied with tp_connection_manager_param_copy().
 */
void
tp_connection_manager_param_free (TpConnectionManagerParam *param)
{
  _tp_connection_manager_param_free_contents (param);

  g_slice_free (TpConnectionManagerParam, param);
}


/**
 * tp_connection_manager_protocol_copy:
 * @in: the #TpConnectionManagerProtocol to copy
 *
 * <!-- Returns: says it all -->
 *
 * Returns: a newly (slice) allocated #TpConnectionManagerProtocol, free with
 *  tp_connection_manager_protocol_free()
 */
TpConnectionManagerProtocol *
tp_connection_manager_protocol_copy (const TpConnectionManagerProtocol *in)
{
  TpConnectionManagerProtocol *out = g_slice_new0 (TpConnectionManagerProtocol);
  TpConnectionManagerParam *param;
  GArray *params = g_array_new (TRUE, TRUE,
      sizeof (TpConnectionManagerParam));

  out->name = g_strdup (in->name);

  for (param = in->params; param->name != NULL; param++)
    {
      TpConnectionManagerParam copy = { 0, };

      _tp_connection_manager_param_copy_contents (param, &copy);
      g_array_append_val (params, copy);
    }

  out->params = (TpConnectionManagerParam *) g_array_free (params, FALSE);

  return out;
}


/**
 * tp_connection_manager_protocol_free:
 * @proto: the #TpConnectionManagerProtocol to free
 *
 * Frees @proto, which was copied with tp_connection_manager_protocol_copy().
 */
void
tp_connection_manager_protocol_free (TpConnectionManagerProtocol *proto)
{
  TpConnectionManagerParam *param;

  g_free (proto->name);

  for (param = proto->params; param->name != NULL; param++)
    {
      _tp_connection_manager_param_free_contents (param);
    }

  g_free (proto->params);

  g_slice_free (TpConnectionManagerProtocol, proto);
}


/**
 * TP_TYPE_CONNECTION_MANAGER_PARAM:
 *
 * The boxed type of a #TpConnectionManagerParam.
 */


GType
tp_connection_manager_param_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      type = g_boxed_type_register_static (
          g_intern_static_string ("TpConnectionManagerParam"),
          (GBoxedCopyFunc) tp_connection_manager_param_copy,
          (GBoxedFreeFunc) tp_connection_manager_param_free);
    }

  return type;
}


/**
 * TP_TYPE_CONNECTION_MANAGER_PROTOCOL:
 *
 * The boxed type of a #TpConnectionManagerProtocol.
 */


GType
tp_connection_manager_protocol_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      type = g_boxed_type_register_static (
          g_intern_static_string ("TpConnectionManagerProtocol"),
          (GBoxedCopyFunc) tp_connection_manager_protocol_copy,
          (GBoxedFreeFunc) tp_connection_manager_protocol_free);
    }

  return type;
}


typedef struct {
    TpConnectionManager *cm;
    TpConnectionManagerWhenReadyCb callback;
    gpointer user_data;
    GDestroyNotify destroy;
    TpWeakRef *weak_ref;
} WhenReadyContext;

static void
when_ready_context_free (gpointer d)
{
  WhenReadyContext *c = d;

  if (c->weak_ref != NULL)
    {
      tp_weak_ref_destroy (c->weak_ref);
      c->weak_ref = NULL;
    }

  if (c->cm != NULL)
    {
      g_object_unref (c->cm);
      c->cm = NULL;
    }

  if (c->destroy != NULL)
    c->destroy (c->user_data);

  g_slice_free (WhenReadyContext, c);
}

static void
tp_connection_manager_ready_or_failed (TpConnectionManager *self,
                                       const GError *error)
{
  if (self->info_source > TP_CM_INFO_SOURCE_NONE)
    {
      /* we have info already, so suppress any error and return the old info */
      error = NULL;
    }
  else
    {
      g_assert (error != NULL);
    }

  if (error == NULL)
    {
      _tp_proxy_set_feature_prepared ((TpProxy *) self,
          TP_CONNECTION_MANAGER_FEATURE_CORE, TRUE);
    }
  else
    {
      _tp_proxy_set_features_failed ((TpProxy *) self, error);
    }
}

static void
tp_connection_manager_ready_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  WhenReadyContext *c = user_data;
  GError *error = NULL;
  GObject *weak_object = NULL;

  g_return_if_fail (source_object == (GObject *) c->cm);

  if (c->weak_ref != NULL)
    {
      weak_object = tp_weak_ref_dup_object (c->weak_ref);

      if (weak_object == NULL)
        goto finally;
    }

  if (tp_proxy_prepare_finish (source_object, res, &error))
    {
      c->callback (c->cm, NULL, c->user_data, weak_object);
    }
  else
    {
      g_assert (error != NULL);
      c->callback (c->cm, error, c->user_data, weak_object);
      g_error_free (error);
    }

finally:
  if (weak_object != NULL)
    g_object_unref (weak_object);

  when_ready_context_free (c);
}

/**
 * TpConnectionManagerWhenReadyCb:
 * @cm: a connection manager
 * @error: %NULL on success, or the reason why tp_connection_manager_is_ready()
 *         would return %FALSE
 * @user_data: the @user_data passed to tp_connection_manager_call_when_ready()
 * @weak_object: the @weak_object passed to
 *               tp_connection_manager_call_when_ready()
 *
 * Called as the result of tp_connection_manager_call_when_ready(). If the
 * connection manager's protocol and parameter information could be retrieved,
 * @error is %NULL and @cm is considered to be ready. Otherwise, @error is
 * non-%NULL and @cm is not ready.
 */

/**
 * tp_connection_manager_call_when_ready:
 * @self: a connection manager
 * @callback: callback to call when information has been retrieved or on
 *            error
 * @user_data: arbitrary data to pass to the callback
 * @destroy: called to destroy @user_data
 * @weak_object: object to reference weakly; if it is destroyed, @callback
 *               will not be called, but @destroy will still be called
 *
 * Call the @callback from the main loop when information about @cm's
 * supported protocols and parameters has been retrieved.
 *
 * Since: 0.7.26
 */
void
tp_connection_manager_call_when_ready (TpConnectionManager *self,
                                       TpConnectionManagerWhenReadyCb callback,
                                       gpointer user_data,
                                       GDestroyNotify destroy,
                                       GObject *weak_object)
{
  WhenReadyContext *c;

  g_return_if_fail (TP_IS_CONNECTION_MANAGER (self));
  g_return_if_fail (callback != NULL);

  c = g_slice_new0 (WhenReadyContext);

  c->cm = g_object_ref (self);
  c->callback = callback;
  c->user_data = user_data;
  c->destroy = destroy;

  if (weak_object != NULL)
    {
      c->weak_ref = tp_weak_ref_new (weak_object, NULL, NULL);
    }

  tp_proxy_prepare_async (self, NULL, tp_connection_manager_ready_cb, c);
}

static void tp_connection_manager_continue_introspection
    (TpConnectionManager *self);

static void
tp_connection_manager_got_parameters (TpConnectionManager *self,
                                      const GPtrArray *parameters,
                                      const GError *error,
                                      gpointer user_data,
                                      GObject *user_object)
{
  gchar *protocol = user_data;
  GArray *output;
  guint i;
  TpConnectionManagerProtocol *proto_struct;

  DEBUG ("Protocol name: %s", protocol);

  g_assert (self->priv->introspection_call != NULL);
  self->priv->introspection_call = NULL;

  if (error != NULL)
    {
      DEBUG ("Error getting params for %s, skipping it", protocol);
      goto out;
    }

   output = g_array_sized_new (TRUE, TRUE,
      sizeof (TpConnectionManagerParam), parameters->len);

  for (i = 0; i < parameters->len; i++)
    {
      GValue structure = { 0 };
      GValue *tmp;
      /* Points to the zeroed entry just after the end of the array
       * - but we're about to extend the array to make it valid */
      TpConnectionManagerParam *param = &g_array_index (output,
          TpConnectionManagerParam, output->len);

      g_value_init (&structure, TP_STRUCT_TYPE_PARAM_SPEC);
      g_value_set_static_boxed (&structure, g_ptr_array_index (parameters, i));

      g_array_set_size (output, output->len + 1);

      if (!dbus_g_type_struct_get (&structure,
            0, &param->name,
            1, &param->flags,
            2, &param->dbus_signature,
            3, &tmp,
            G_MAXUINT))
        {
          DEBUG ("Unparseable parameter #%d for %s, ignoring", i, protocol);
          /* *shrug* that one didn't work, let's skip it */
          g_array_set_size (output, output->len - 1);
          continue;
        }

      g_value_init (&param->default_value,
          G_VALUE_TYPE (tmp));
      g_value_copy (tmp, &param->default_value);
      g_value_unset (tmp);
      g_free (tmp);

      param->priv = NULL;

      DEBUG ("\tParam name: %s", param->name);
      DEBUG ("\tParam flags: 0x%x", param->flags);
      DEBUG ("\tParam sig: %s", param->dbus_signature);

      if ((!tp_strdiff (param->name, "password") ||
          g_str_has_suffix (param->name, "-password")) &&
          (param->flags & TP_CONN_MGR_PARAM_FLAG_SECRET) == 0)
        {
          DEBUG ("\tTreating as secret due to its name (please fix %s)",
              self->name);
          param->flags |= TP_CONN_MGR_PARAM_FLAG_SECRET;
        }

#ifdef ENABLE_DEBUG
        {
          gchar *repr = g_strdup_value_contents (&(param->default_value));

          DEBUG ("\tParam default value: %s of type %s", repr,
              G_VALUE_TYPE_NAME (&(param->default_value)));
          g_free (repr);
        }
#endif
    }

  proto_struct = g_slice_new (TpConnectionManagerProtocol);
  proto_struct->name = g_strdup (protocol);
  proto_struct->params =
      (TpConnectionManagerParam *) g_array_free (output, FALSE);
  g_ptr_array_add (self->priv->found_protocols, proto_struct);

out:
  tp_connection_manager_continue_introspection (self);
}

static void
tp_connection_manager_free_protocols (GPtrArray *protocols)
{
  guint i;

  for (i = 0; i < protocols->len; i++)
    {
      TpConnectionManagerProtocol *proto = g_ptr_array_index (protocols, i);

      if (proto == NULL)
        continue;

      tp_connection_manager_protocol_free (proto);
    }

  g_ptr_array_free (protocols, TRUE);
}

static void tp_connection_manager_ready_or_failed (TpConnectionManager *self,
                                       const GError *error);

static void
tp_connection_manager_end_introspection (TpConnectionManager *self,
                                         const GError *error)
{
  guint i;

  if (self->priv->introspection_call != NULL)
    {
      tp_proxy_pending_call_cancel (self->priv->introspection_call);
      self->priv->introspection_call = NULL;
    }

  if (self->priv->found_protocols != NULL)
    {
      tp_connection_manager_free_protocols (self->priv->found_protocols);
      self->priv->found_protocols = NULL;
    }

  if (self->priv->pending_protocols != NULL)
    {
      for (i = 0; i < self->priv->pending_protocols->len; i++)
        g_free (self->priv->pending_protocols->pdata[i]);

      g_ptr_array_free (self->priv->pending_protocols, TRUE);
      self->priv->pending_protocols = NULL;
    }

  DEBUG ("End of introspection, info source %u", self->info_source);
  g_signal_emit (self, signals[SIGNAL_GOT_INFO], 0, self->info_source);
  tp_connection_manager_ready_or_failed (self, error);
}

static void
tp_connection_manager_continue_introspection (TpConnectionManager *self)
{
  gchar *next_protocol;

  g_assert (self->priv->pending_protocols != NULL);

  if (self->priv->pending_protocols->len == 0)
    {
      GPtrArray *tmp;
      guint old;

      g_ptr_array_add (self->priv->found_protocols, NULL);

      /* swap found_protocols and protocols, so we'll free the old protocols
       * as part of end_introspection */
      tmp = self->priv->protocols;
      self->priv->protocols = self->priv->found_protocols;
      self->priv->found_protocols = tmp;

      self->protocols = (const TpConnectionManagerProtocol * const *)
          self->priv->protocols->pdata;

      old = self->info_source;
      self->info_source = TP_CM_INFO_SOURCE_LIVE;

      if (old != TP_CM_INFO_SOURCE_LIVE)
        g_object_notify ((GObject *) self, "info-source");

      tp_connection_manager_end_introspection (self, NULL);

      return;
    }

  next_protocol = g_ptr_array_remove_index_fast (self->priv->pending_protocols,
      0);
  self->priv->introspection_call =
      tp_cli_connection_manager_call_get_parameters (self, -1, next_protocol,
          tp_connection_manager_got_parameters, next_protocol, g_free,
          NULL);
}

static void
tp_connection_manager_got_protocols (TpConnectionManager *self,
                                     const gchar **protocols,
                                     const GError *error,
                                     gpointer user_data,
                                     GObject *user_object)
{
  guint i = 0;
  const gchar **iter;

  g_assert (self->priv->introspection_call != NULL);
  self->priv->introspection_call = NULL;

  if (error != NULL)
    {
      DEBUG ("Failed: %s", error->message);

      if (!self->running)
        {
          /* ListProtocols failed to start it - we assume this is because
           * activation failed */
          g_signal_emit (self, signals[SIGNAL_EXITED], 0);
        }

      tp_connection_manager_end_introspection (self, error);
      return;
    }

  for (iter = protocols; *iter != NULL; iter++)
    i++;

  DEBUG ("Succeeded with %u protocols", i);

  g_assert (self->priv->found_protocols == NULL);
  /* Allocate one more pointer - we're going to append NULL afterwards */
  self->priv->found_protocols = g_ptr_array_sized_new (i + 1);

  g_assert (self->priv->pending_protocols == NULL);
  self->priv->pending_protocols = g_ptr_array_sized_new (i);

  for (iter = protocols; *iter != NULL; iter++)
    {
      g_ptr_array_add (self->priv->pending_protocols, g_strdup (*iter));
    }

  tp_connection_manager_continue_introspection (self);
}

static gboolean
introspection_in_progress (TpConnectionManager *self)
{
  return (self->priv->introspection_call != NULL ||
      self->priv->found_protocols != NULL);
}

static gboolean
tp_connection_manager_idle_introspect (gpointer data)
{
  TpConnectionManager *self = data;

  /* Start introspecting if we want to and we're not already */
  if (!introspection_in_progress (self) &&
      (self->always_introspect ||
       self->info_source == TP_CM_INFO_SOURCE_NONE))
    {
      DEBUG ("calling ListProtocols on CM");
      self->priv->introspection_call =
        tp_cli_connection_manager_call_list_protocols (self, -1,
            tp_connection_manager_got_protocols, NULL, NULL, NULL);
    }

  self->priv->introspect_idle_id = 0;

  return FALSE;
}

static gboolean tp_connection_manager_idle_read_manager_file (gpointer data);

static void
tp_connection_manager_name_owner_changed_cb (TpDBusDaemon *bus,
                                             const gchar *name,
                                             const gchar *new_owner,
                                             gpointer user_data)
{
  TpConnectionManager *self = user_data;

  /* make sure self exists for the duration of this callback */
  g_object_ref (self);

  if (new_owner[0] == '\0')
    {
      GError e = { TP_DBUS_ERRORS, TP_DBUS_ERROR_NAME_OWNER_LOST,
          "Connection manager process exited during introspection" };

      self->running = FALSE;

      /* cancel pending introspection, if any */
      if (introspection_in_progress (self))
        tp_connection_manager_end_introspection (self, &e);

      /* If our name wasn't known already, a change to "" is just the initial
       * state, so we didn't *exit* as such. */
      if (self->priv->name_known)
        {
          g_signal_emit (self, signals[SIGNAL_EXITED], 0);
        }
    }
  else
    {
      /* represent an atomic change of ownership as if it was an exit and
       * restart */
      if (self->running)
        tp_connection_manager_name_owner_changed_cb (bus, name, "", self);

      self->running = TRUE;
      g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0);

      if (self->priv->introspect_idle_id == 0)
        self->priv->introspect_idle_id = g_idle_add (
            tp_connection_manager_idle_introspect, self);
    }

  /* if we haven't started introspecting yet, now would be a good time */
  if (!self->priv->name_known)
    {
      g_assert (self->priv->manager_file_read_idle_id == 0);

      /* now we know whether we're running or not, we can try reading the
       * .manager file... */
      self->priv->manager_file_read_idle_id = g_idle_add (
          tp_connection_manager_idle_read_manager_file, self);

      if (self->priv->want_activation && self->priv->introspect_idle_id == 0)
        {
          /* ... but if activation was requested, we should also do that */
          self->priv->introspect_idle_id = g_idle_add (
              tp_connection_manager_idle_introspect, self);
        }

      /* Unfreeze automatic reading of .manager file if manager-file changes */
      self->priv->name_known = TRUE;
    }

  g_object_unref (self);
}

static gboolean
init_gvalue_from_dbus_sig (const gchar *sig,
                           GValue *value)
{
  g_assert (!G_IS_VALUE (value));

  switch (sig[0])
    {
    case 'b':
      g_value_init (value, G_TYPE_BOOLEAN);
      return TRUE;

    case 's':
      g_value_init (value, G_TYPE_STRING);
      return TRUE;

    case 'q':
    case 'u':
      g_value_init (value, G_TYPE_UINT);
      return TRUE;

    case 'y':
      g_value_init (value, G_TYPE_UCHAR);
      return TRUE;

    case 'n':
    case 'i':
      g_value_init (value, G_TYPE_INT);
      return TRUE;

    case 'x':
      g_value_init (value, G_TYPE_INT64);
      return TRUE;

    case 't':
      g_value_init (value, G_TYPE_UINT64);
      return TRUE;

    case 'o':
      g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
      g_value_set_static_boxed (value, "/");
      return TRUE;

    case 'd':
      g_value_init (value, G_TYPE_DOUBLE);
      return TRUE;

    case 'v':
      g_value_init (value, G_TYPE_VALUE);
      return TRUE;

    case 'a':
      switch (sig[1])
        {
        case 's':
          g_value_init (value, G_TYPE_STRV);
          return TRUE;

        case 'y':
          g_value_init (value, DBUS_TYPE_G_UCHAR_ARRAY);
          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
parse_default_value (GValue *value,
                     const gchar *sig,
                     gchar *string,
                     GKeyFile *file,
                     const gchar *group,
                     const gchar *key)
{
  GError *error = NULL;
  gchar *s, *p;

  switch (sig[0])
    {
    case 'b':
      g_value_set_boolean (value, g_key_file_get_boolean (file, group, key,
            &error));

      if (error == NULL)
        return TRUE;

      /* In telepathy-glib < 0.7.26 we accepted true and false in
       * any case combination, 0, and 1. The desktop file spec specifies
       * "true" and "false" only, while GKeyFile currently accepts 0 and 1 too.
       * So, on error, let's fall back to more lenient parsing that explicitly
       * allows everything we historically allowed. */
      g_error_free (error);
      s = g_key_file_get_value (file, group, key, NULL);

      if (s == NULL)
        return FALSE;

      for (p = s; *p != '\0'; p++)
        {
          *p = g_ascii_tolower (*p);
        }

      if (!tp_strdiff (s, "1") || !tp_strdiff (s, "true"))
        {
          g_value_set_boolean (value, TRUE);
        }
      else if (!tp_strdiff (s, "0") || !tp_strdiff (s, "false"))
        {
          g_value_set_boolean (value, TRUE);
        }
      else
        {
          g_free (s);
          return FALSE;
        }

      g_free (s);
      return TRUE;

    case 's':
      s = g_key_file_get_string (file, group, key, NULL);

      g_value_take_string (value, s);
      return (s != NULL);

    case 'y':
    case 'q':
    case 'u':
    case 't':
        {
          guint64 v = tp_g_key_file_get_uint64 (file, group, key, &error);

          if (error != NULL)
            {
              g_error_free (error);
              return FALSE;
            }

          if (sig[0] == 't')
            {
              g_value_set_uint64 (value, v);
              return TRUE;
            }

          if (sig[0] == 'y')
            {
              if (v > G_MAXUINT8)
                {
                  return FALSE;
                }

              g_value_set_uchar (value, v);
              return TRUE;
            }

          if (v > G_MAXUINT32 || (sig[0] == 'q' && v > G_MAXUINT16))
            return FALSE;

          g_value_set_uint (value, v);
          return TRUE;
        }

    case 'n':
    case 'i':
    case 'x':
      if (string[0] == '\0')
        {
          return FALSE;
        }
      else
        {
          gint64 v = tp_g_key_file_get_int64 (file, group, key, &error);

          if (error != NULL)
            {
              g_error_free (error);
              return FALSE;
            }

          if (sig[0] == 'x')
            {
              g_value_set_int64 (value, v);
              return TRUE;
            }

          if (v > G_MAXINT32 || (sig[0] == 'q' && v > G_MAXINT16))
            return FALSE;

          if (v < G_MININT32 || (sig[0] == 'n' && v < G_MININT16))
            return FALSE;

          g_value_set_int (value, v);
          return TRUE;
        }

    case 'o':
      s = g_key_file_get_string (file, group, key, NULL);

      if (s == NULL || !tp_dbus_check_valid_object_path (s, NULL))
        {
          g_free (s);
          return FALSE;
        }

      g_value_take_boxed (value, s);

      return TRUE;

    case 'd':
      g_value_set_double (value, g_key_file_get_double (file, group, key,
            &error));

      if (error != NULL)
        {
          g_error_free (error);
          return FALSE;
        }

      return TRUE;

    case 'a':
      switch (sig[1])
        {
        case 's':
            {
              g_value_take_boxed (value,
                  g_key_file_get_string_list (file, group, key, NULL, &error));

              if (error != NULL)
                {
                  g_error_free (error);
                  return FALSE;
                }

              return TRUE;
            }
        }
    }

  if (G_IS_VALUE (value))
    g_value_unset (value);

  return FALSE;
}

static GPtrArray *
tp_connection_manager_read_file (const gchar *cm_name,
    const gchar *filename,
    GError **error)
{
  GKeyFile *file;
  gchar **groups, **group;
  guint i;
  TpConnectionManagerProtocol *proto_struct;
  GPtrArray *protocols;

  file = g_key_file_new ();

  if (!g_key_file_load_from_file (file, filename, G_KEY_FILE_NONE, error))
    return NULL;

  groups = g_key_file_get_groups (file, NULL);

  if (groups == NULL || *groups == NULL)
    return g_ptr_array_sized_new (1);

  i = 0;
  for (group = groups; *group != NULL; group++)
    {
      if (g_str_has_prefix (*group, "Protocol "))
        i++;
    }

  /* Reserve space for the caller to add a NULL at the end, so +1 */
  protocols = g_ptr_array_sized_new (i + 1);

  for (group = groups; *group != NULL; group++)
    {
      gchar **keys, **key;
      GArray *output;

      if (!g_str_has_prefix (*group, "Protocol "))
        continue;

      proto_struct = g_slice_new (TpConnectionManagerProtocol);

      keys = g_strsplit (*group, " ", 2);
      proto_struct->name = g_strdup (keys[1]);
      g_strfreev (keys);

      DEBUG ("Protocol %s", proto_struct->name);

      keys = g_key_file_get_keys (file, *group, NULL, NULL);

      i = 0;
      for (key = keys; key != NULL && *key != NULL; key++)
        {
          if (g_str_has_prefix (*key, "param-"))
            i++;
        }

      output = g_array_sized_new (TRUE, TRUE,
          sizeof (TpConnectionManagerParam), i);

      for (key = keys; key != NULL && *key != NULL; key++)
        {
          if (g_str_has_prefix (*key, "param-"))
            {
              gchar **strv, **iter;
              gchar *value, *def;
              /* Points to the zeroed entry just after the end of the array
               * - but we're about to extend the array to make it valid */
              TpConnectionManagerParam *param = &g_array_index (output,
                  TpConnectionManagerParam, output->len);

              value = g_key_file_get_string (file, *group, *key, NULL);
              if (value == NULL)
                continue;

              /* zero_terminated=TRUE and clear_=TRUE */
              g_assert (param->name == NULL);

              g_array_set_size (output, output->len + 1);

              /* strlen ("param-") == 6 */
              param->name = g_strdup (*key + 6);

              strv = g_strsplit (value, " ", 0);
              g_free (value);

              param->dbus_signature = g_strdup (strv[0]);

              for (iter = strv + 1; *iter != NULL; iter++)
                {
                  if (!tp_strdiff (*iter, "required"))
                    param->flags |= TP_CONN_MGR_PARAM_FLAG_REQUIRED;
                  if (!tp_strdiff (*iter, "register"))
                    param->flags |= TP_CONN_MGR_PARAM_FLAG_REGISTER;
                  if (!tp_strdiff (*iter, "secret"))
                    param->flags |= TP_CONN_MGR_PARAM_FLAG_SECRET;
                  if (!tp_strdiff (*iter, "dbus-property"))
                    param->flags |= TP_CONN_MGR_PARAM_FLAG_DBUS_PROPERTY;
                }

              g_strfreev (strv);

              if ((!tp_strdiff (param->name, "password") ||
                  g_str_has_suffix (param->name, "-password")) &&
                  (param->flags & TP_CONN_MGR_PARAM_FLAG_SECRET) == 0)
                {
                  DEBUG ("\tTreating %s as secret due to its name (please "
                      "fix %s)", param->name, cm_name);
                  param->flags |= TP_CONN_MGR_PARAM_FLAG_SECRET;
                }

              def = g_strdup_printf ("default-%s", param->name);
              value = g_key_file_get_string (file, *group, def, NULL);

              init_gvalue_from_dbus_sig (param->dbus_signature,
                  &param->default_value);

              if (value != NULL && parse_default_value (&param->default_value,
                    param->dbus_signature, value, file, *group, def))
                param->flags |= TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT;

              g_free (value);
              g_free (def);

              DEBUG ("\tParam name: %s", param->name);
              DEBUG ("\tParam flags: 0x%x", param->flags);
              DEBUG ("\tParam sig: %s", param->dbus_signature);

#ifdef ENABLE_DEBUG
              if (G_IS_VALUE (&param->default_value))
                {
                  gchar *repr = g_strdup_value_contents
                      (&(param->default_value));

                  DEBUG ("\tParam default value: %s of type %s", repr,
                      G_VALUE_TYPE_NAME (&(param->default_value)));
                  g_free (repr);
                }
              else
                {
                  DEBUG ("\tParam default value: not set");
                }
#endif
            }
        }

      g_strfreev (keys);

      proto_struct->params =
          (TpConnectionManagerParam *) g_array_free (output, FALSE);

      g_ptr_array_add (protocols, proto_struct);
    }

  g_strfreev (groups);
  g_key_file_free (file);

  return protocols;
}

static gboolean
tp_connection_manager_idle_read_manager_file (gpointer data)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (data);

  self->priv->manager_file_read_idle_id = 0;

  if (self->priv->protocols == NULL)
    {
      if (self->priv->manager_file != NULL &&
          self->priv->manager_file[0] != '\0')
        {
          GError *error = NULL;
          GPtrArray *protocols = tp_connection_manager_read_file (
              self->name, self->priv->manager_file, &error);

          DEBUG ("Read %s", self->priv->manager_file);

          if (protocols == NULL)
            {
              DEBUG ("Failed to load %s: %s", self->priv->manager_file,
                  error->message);
              g_error_free (error);
              error = NULL;
            }
          else
            {
              g_ptr_array_add (protocols, NULL);
              self->priv->protocols = protocols;

              self->protocols = (const TpConnectionManagerProtocol * const *)
                  self->priv->protocols->pdata;

              DEBUG ("Got info from file");
              /* previously it must have been NONE */
              self->info_source = TP_CM_INFO_SOURCE_FILE;

              g_object_ref (self);
              g_object_notify ((GObject *) self, "info-source");

              g_signal_emit (self, signals[SIGNAL_GOT_INFO], 0,
                  self->info_source);
              tp_connection_manager_ready_or_failed (self, NULL);
              g_object_unref (self);

              goto out;
            }
        }

      if (self->priv->introspect_idle_id == 0)
        {
          DEBUG ("no .manager file or failed to parse it, trying to activate "
              "CM instead");
          tp_connection_manager_idle_introspect (self);
        }
      else
        {
          DEBUG ("no .manager file, but will activate CM soon anyway");
        }
      /* else we're going to introspect soon anyway */
    }

out:
  return FALSE;
}

static gchar *
tp_connection_manager_find_manager_file (const gchar *name)
{
  gchar *filename;
  const gchar * const * data_dirs;

  g_assert (name != NULL);

  filename = g_strdup_printf ("%s/telepathy/managers/%s.manager",
      g_get_user_data_dir (), name);

  DEBUG ("in XDG_DATA_HOME: trying %s", filename);

  if (g_file_test (filename, G_FILE_TEST_EXISTS))
    return filename;

  g_free (filename);

  for (data_dirs = g_get_system_data_dirs ();
       *data_dirs != NULL;
       data_dirs++)
    {
      filename = g_strdup_printf ("%s/telepathy/managers/%s.manager",
          *data_dirs, name);

      DEBUG ("in XDG_DATA_DIRS: trying %s", filename);

      if (g_file_test (filename, G_FILE_TEST_EXISTS))
        return filename;

      g_free (filename);
    }

  return NULL;
}

static GObject *
tp_connection_manager_constructor (GType type,
                                   guint n_params,
                                   GObjectConstructParam *params)
{
  GObjectClass *object_class =
      (GObjectClass *) tp_connection_manager_parent_class;
  TpConnectionManager *self =
      TP_CONNECTION_MANAGER (object_class->constructor (type, n_params,
            params));
  TpProxy *as_proxy = (TpProxy *) self;
  const gchar *object_path = as_proxy->object_path;
  const gchar *bus_name = as_proxy->bus_name;

  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (bus_name != NULL, NULL);

  /* Watch my D-Bus name */
  tp_dbus_daemon_watch_name_owner (as_proxy->dbus_daemon,
      as_proxy->bus_name, tp_connection_manager_name_owner_changed_cb, self,
      NULL);

  self->name = strrchr (object_path, '/') + 1;
  g_assert (self->name != NULL);

  if (self->priv->manager_file == NULL)
    {
      self->priv->manager_file =
          tp_connection_manager_find_manager_file (self->name);
    }

  return (GObject *) self;
}

static void
tp_connection_manager_init (TpConnectionManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TYPE_CONNECTION_MANAGER,
      TpConnectionManagerPrivate);
}

static void
tp_connection_manager_dispose (GObject *object)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);
  TpProxy *as_proxy = (TpProxy *) self;

  if (self->priv->disposed)
    goto finally;

  self->priv->disposed = TRUE;

  tp_dbus_daemon_cancel_name_owner_watch (as_proxy->dbus_daemon,
      as_proxy->bus_name, tp_connection_manager_name_owner_changed_cb,
      object);

finally:
  G_OBJECT_CLASS (tp_connection_manager_parent_class)->dispose (object);
}

static void
tp_connection_manager_finalize (GObject *object)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);
  guint i;

  g_free (self->priv->manager_file);

  if (self->priv->manager_file_read_idle_id != 0)
    g_source_remove (self->priv->manager_file_read_idle_id);

  if (self->priv->introspect_idle_id != 0)
    g_source_remove (self->priv->introspect_idle_id);

  if (self->priv->protocols != NULL)
    {
      tp_connection_manager_free_protocols (self->priv->protocols);
    }

  if (self->priv->pending_protocols != NULL)
    {
      for (i = 0; i < self->priv->pending_protocols->len; i++)
        g_free (self->priv->pending_protocols->pdata[i]);

      g_ptr_array_free (self->priv->pending_protocols, TRUE);
    }

  if (self->priv->found_protocols != NULL)
    {
      tp_connection_manager_free_protocols (self->priv->found_protocols);
    }

  G_OBJECT_CLASS (tp_connection_manager_parent_class)->finalize (object);
}

static void
tp_connection_manager_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION_MANAGER:
      g_value_set_string (value, self->name);
      break;

    case PROP_INFO_SOURCE:
      g_value_set_uint (value, self->info_source);
      break;

    case PROP_MANAGER_FILE:
      g_value_set_string (value, self->priv->manager_file);
      break;

    case PROP_ALWAYS_INTROSPECT:
      g_value_set_boolean (value, self->always_introspect);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
tp_connection_manager_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);

  switch (property_id)
    {
    case PROP_MANAGER_FILE:
      g_free (self->priv->manager_file);

      /* If initial code has already run, change the definition of where
       * we expect to find the .manager file and trigger re-introspection.
       * Otherwise, just take the value - when name_known becomes TRUE we
       * queue the first-time manager file lookup anyway.
       */
      if (self->priv->name_known)
        {
          const gchar *tmp = g_value_get_string (value);

          if (tmp == NULL)
            {
              self->priv->manager_file =
                  tp_connection_manager_find_manager_file (self->name);
            }
          else
            {
              self->priv->manager_file = g_strdup (tmp);
            }

          if (self->priv->manager_file_read_idle_id == 0)
            self->priv->manager_file_read_idle_id = g_idle_add (
                tp_connection_manager_idle_read_manager_file, self);
        }
      else
        {
          self->priv->manager_file = g_value_dup_string (value);
        }

      break;

    case PROP_ALWAYS_INTROSPECT:
        {
          gboolean old = self->always_introspect;

          self->always_introspect = g_value_get_boolean (value);

          if (self->running && !old && self->always_introspect)
            {
              /* It's running, we weren't previously auto-introspecting,
               * but we are now. Try it when idle
               */
              if (self->priv->introspect_idle_id == 0)
                self->priv->introspect_idle_id = g_idle_add (
                    tp_connection_manager_idle_introspect, self);
            }
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

/**
 * tp_connection_manager_init_known_interfaces:
 *
 * Ensure that the known interfaces for TpConnectionManager have been set up.
 * This is done automatically when necessary, but for correct
 * overriding of library interfaces by local extensions, you should
 * call this function before calling
 * tp_proxy_or_subclass_hook_on_interface_add() with first argument
 * %TP_TYPE_CONNECTION_MANAGER.
 *
 * Since: 0.7.32
 */
void
tp_connection_manager_init_known_interfaces (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      GType tp_type = TP_TYPE_CONNECTION_MANAGER;

      tp_proxy_init_known_interfaces ();
      tp_proxy_or_subclass_hook_on_interface_add (tp_type,
          tp_cli_connection_manager_add_signals);
      tp_proxy_subclass_add_error_mapping (tp_type,
          TP_ERROR_PREFIX, TP_ERRORS, TP_TYPE_ERROR);

      g_once_init_leave (&once, 1);
    }
}

enum {
    FEAT_CORE,
    N_FEAT
};

static const TpProxyFeature *
tp_connection_manager_list_features (TpProxyClass *cls G_GNUC_UNUSED)
{
  static TpProxyFeature features[N_FEAT + 1] = { { 0 } };

  if (G_LIKELY (features[0].name != 0))
    return features;

  features[FEAT_CORE].name = TP_CONNECTION_MANAGER_FEATURE_CORE;
  features[FEAT_CORE].core = TRUE;

  /* assert that the terminator at the end is there */
  g_assert (features[N_FEAT].name == 0);

  return features;
}

static void
tp_connection_manager_class_init (TpConnectionManagerClass *klass)
{
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  tp_connection_manager_init_known_interfaces ();

  g_type_class_add_private (klass, sizeof (TpConnectionManagerPrivate));

  object_class->constructor = tp_connection_manager_constructor;
  object_class->get_property = tp_connection_manager_get_property;
  object_class->set_property = tp_connection_manager_set_property;
  object_class->dispose = tp_connection_manager_dispose;
  object_class->finalize = tp_connection_manager_finalize;

  proxy_class->interface = TP_IFACE_QUARK_CONNECTION_MANAGER;
  proxy_class->list_features = tp_connection_manager_list_features;

  /**
   * TpConnectionManager:info-source:
   *
   * Where we got the current information on supported protocols
   * (a #TpCMInfoSource).
   *
   * Since 0.7.26, the #GObject::notify signal is emitted for this
   * property.
   */
  param_spec = g_param_spec_uint ("info-source", "CM info source",
      "Where we got the current information on supported protocols",
      TP_CM_INFO_SOURCE_NONE, TP_CM_INFO_SOURCE_LIVE, TP_CM_INFO_SOURCE_NONE,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
  g_object_class_install_property (object_class, PROP_INFO_SOURCE,
      param_spec);

  /**
   * TpConnectionManager:connection-manager:
   *
   * The name of the connection manager, e.g. "gabble" (read-only).
   */
  param_spec = g_param_spec_string ("connection-manager", "CM name",
      "The name of the connection manager, e.g. \"gabble\" (read-only)",
      NULL,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
  g_object_class_install_property (object_class, PROP_CONNECTION_MANAGER,
      param_spec);

  /**
   * TpConnectionManager:manager-file:
   *
   * The absolute path of the .manager file. If set to %NULL (the default),
   * the XDG data directories will be searched for a .manager file of the
   * correct name.
   *
   * If set to the empty string, no .manager file will be read.
   */
  param_spec = g_param_spec_string ("manager-file", ".manager filename",
      "The .manager filename",
      NULL,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
  g_object_class_install_property (object_class, PROP_MANAGER_FILE,
      param_spec);

  /**
   * TpConnectionManager:always-introspect:
   *
   * If %TRUE, always introspect the connection manager as it comes online,
   * even if we already have its info from a .manager file. Default %FALSE.
   */
  param_spec = g_param_spec_boolean ("always-introspect", "Always introspect?",
      "Opportunistically introspect the CM when it's run", FALSE,
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
  g_object_class_install_property (object_class, PROP_ALWAYS_INTROSPECT,
      param_spec);

  /**
   * TpConnectionManager::activated:
   * @self: the connection manager proxy
   *
   * Emitted when the connection manager's well-known name appears on the bus.
   */
  signals[SIGNAL_ACTIVATED] = g_signal_new ("activated",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  /**
   * TpConnectionManager::exited:
   * @self: the connection manager proxy
   *
   * Emitted when the connection manager's well-known name disappears from
   * the bus or when activation fails.
   */
  signals[SIGNAL_EXITED] = g_signal_new ("exited",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  /**
   * TpConnectionManager::got-info:
   * @self: the connection manager proxy
   * @source: a #TpCMInfoSource
   *
   * Emitted when the connection manager's capabilities have been discovered.
   *
   * This signal is not very helpful. Since 0.7.26, using
   * tp_connection_manager_call_when_ready() instead is recommended.
   */
  signals[SIGNAL_GOT_INFO] = g_signal_new ("got-info",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__UINT,
      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * tp_connection_manager_new:
 * @dbus: Proxy for the D-Bus daemon
 * @name: The connection manager name (such as "gabble")
 * @manager_filename: The #TpConnectionManager:manager-file property, which may
 *                    (and generally should) be %NULL.
 * @error: used to return an error if %NULL is returned
 *
 * Convenience function to create a new connection manager proxy. If
 * its protocol and parameter information are required, you should call
 * tp_connection_manager_call_when_ready() on the result.
 *
 * Returns: a new reference to a connection manager proxy, or %NULL if @error
 *          is set.
 */
TpConnectionManager *
tp_connection_manager_new (TpDBusDaemon *dbus,
                           const gchar *name,
                           const gchar *manager_filename,
                           GError **error)
{
  TpConnectionManager *cm;
  gchar *object_path, *bus_name;

  g_return_val_if_fail (dbus != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (!tp_connection_manager_check_valid_name (name, error))
    return NULL;

  object_path = g_strdup_printf ("%s%s", TP_CM_OBJECT_PATH_BASE, name);
  bus_name = g_strdup_printf ("%s%s", TP_CM_BUS_NAME_BASE, name);

  cm = TP_CONNECTION_MANAGER (g_object_new (TP_TYPE_CONNECTION_MANAGER,
        "dbus-daemon", dbus,
        "dbus-connection", ((TpProxy *) dbus)->dbus_connection,
        "bus-name", bus_name,
        "object-path", object_path,
        "manager-file", manager_filename,
        NULL));

  g_free (object_path);
  g_free (bus_name);

  return cm;
}

/**
 * tp_connection_manager_activate:
 * @self: a connection manager proxy
 *
 * Attempt to run and introspect the connection manager, asynchronously.
 * Since 0.7.26 this function is not generally very useful, since
 * the connection manager will now be activated automatically if necessary.
 *
 * If the CM was already running, do nothing and return %FALSE.
 *
 * On success, emit #TpConnectionManager::activated when the CM appears
 * on the bus, and #TpConnectionManager::got-info when its capabilities
 * have been (re-)discovered.
 *
 * On failure, emit #TpConnectionManager::exited without first emitting
 * activated.
 *
 * Returns: %TRUE if activation was needed and is now in progress, %FALSE
 *  if the connection manager was already running and no additional signals
 *  will be emitted.
 *
 * Since: 0.7.1
 */
gboolean
tp_connection_manager_activate (TpConnectionManager *self)
{
  if (self->priv->name_known)
    {
      if (self->running)
        {
          DEBUG ("already running");
          return FALSE;
        }

      if (self->priv->introspect_idle_id == 0)
        self->priv->introspect_idle_id = g_idle_add (
            tp_connection_manager_idle_introspect, self);
    }
  else
    {
      /* we'll activate later, when we know properly whether we're running */
      DEBUG ("queueing activation for when we know what's going on");
      self->priv->want_activation = TRUE;
    }

  return TRUE;
}

static gboolean
steal_into_ptr_array (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
  if (value != NULL)
    g_ptr_array_add (user_data, value);

  g_free (key);

  return TRUE;
}

typedef struct
{
  GHashTable *table;
  GPtrArray *arr;
  TpConnectionManagerListCb callback;
  gpointer user_data;
  GDestroyNotify destroy;
  TpProxyPendingCall *pending_call;
  size_t base_len;
  gsize refcount;
  gsize cms_to_ready;
  unsigned getting_names:1;
} _ListContext;

static void
list_context_unref (_ListContext *list_context)
{
  guint i;

  if (--list_context->refcount > 0)
    return;

  if (list_context->destroy != NULL)
    list_context->destroy (list_context->user_data);

  if (list_context->arr != NULL)
    {
      for (i = 0; i < list_context->arr->len; i++)
        {
          TpConnectionManager *cm = g_ptr_array_index (list_context->arr, i);

          if (cm != NULL)
            g_object_unref (cm);
        }

      g_ptr_array_free (list_context->arr, TRUE);
    }

  g_hash_table_destroy (list_context->table);
  g_slice_free (_ListContext, list_context);
}

static void
tp_list_connection_managers_cm_ready (TpConnectionManager *cm,
                                      const GError *error,
                                      gpointer user_data,
                                      GObject *weak_object)
{
  _ListContext *list_context = user_data;

  /* ignore errors here - all we guarantee is that the CM is ready
   * *if possible* */

  if ((--list_context->cms_to_ready) == 0)
    {
      TpConnectionManager **cms;
      guint n_cms = list_context->arr->len;

      g_assert (list_context->callback != NULL);

      g_ptr_array_add (list_context->arr, NULL);
      cms = (TpConnectionManager **) list_context->arr->pdata;

      list_context->callback (cms, n_cms, NULL, list_context->user_data,
          weak_object);
      list_context->callback = NULL;
    }
}

static void
tp_list_connection_managers_got_names (TpDBusDaemon *bus_daemon,
                                       const gchar * const *names,
                                       const GError *error,
                                       gpointer user_data,
                                       GObject *weak_object)
{
  _ListContext *list_context = user_data;
  const gchar * const *name_iter;

  if (error != NULL)
    {
      list_context->callback (NULL, 0, error, list_context->user_data,
          weak_object);
      return;
    }

  for (name_iter = names; name_iter != NULL && *name_iter != NULL; name_iter++)
    {
      const gchar *name;
      TpConnectionManager *cm;

      if (strncmp (TP_CM_BUS_NAME_BASE, *name_iter, list_context->base_len)
          != 0)
        continue;

      name = *name_iter + list_context->base_len;

      if (g_hash_table_lookup (list_context->table, name) == NULL)
        {
          /* just ignore connection managers with bad names */
          cm = tp_connection_manager_new (bus_daemon, name, NULL, NULL);
          if (cm != NULL)
            g_hash_table_insert (list_context->table, g_strdup (name), cm);
        }
    }

  if (list_context->getting_names)
    {
      /* now that we have all the CMs, wait for them all to be ready */
      guint i;

      list_context->arr = g_ptr_array_sized_new (g_hash_table_size
              (list_context->table));

      g_hash_table_foreach_steal (list_context->table, steal_into_ptr_array,
          list_context->arr);

      list_context->cms_to_ready = list_context->arr->len;
      list_context->refcount += list_context->cms_to_ready;

      for (i = 0; i < list_context->cms_to_ready; i++)
        {
          TpConnectionManager *cm = g_ptr_array_index (list_context->arr, i);

          tp_connection_manager_call_when_ready (cm,
              tp_list_connection_managers_cm_ready, list_context,
              (GDestroyNotify) list_context_unref, weak_object);
        }
    }
  else
    {
      list_context->getting_names = TRUE;
      list_context->refcount++;
      tp_dbus_daemon_list_names (bus_daemon, 2000,
          tp_list_connection_managers_got_names, list_context,
          (GDestroyNotify) list_context_unref, weak_object);
    }
}

/**
 * tp_list_connection_managers:
 * @bus_daemon: proxy for the D-Bus daemon
 * @callback: callback to be called when listing the CMs succeeds or fails;
 *   not called if the @weak_object goes away
 * @user_data: user-supplied data for the callback
 * @destroy: callback to destroy the user-supplied data, called after
 *   @callback, but also if the @weak_object goes away
 * @weak_object: if not %NULL, will be weakly referenced; the callback will
 *   not be called, and the call will be cancelled, if the object has vanished
 *
 * List the available (running or installed) connection managers. Call the
 * callback when done.
 *
 * Since 0.7.26, this function will wait for each #TpConnectionManager
 * to be ready, so all connection managers passed to @callback will be ready
 * (tp_connection_manager_is_ready() will return %TRUE) unless an error
 * occurred while launching that connection manager.
 *
 * Since: 0.7.1
 */
void
tp_list_connection_managers (TpDBusDaemon *bus_daemon,
                             TpConnectionManagerListCb callback,
                             gpointer user_data,
                             GDestroyNotify destroy,
                             GObject *weak_object)
{
  _ListContext *list_context = g_slice_new0 (_ListContext);

  list_context->base_len = strlen (TP_CM_BUS_NAME_BASE);
  list_context->callback = callback;
  list_context->user_data = user_data;
  list_context->destroy = destroy;

  list_context->getting_names = FALSE;
  list_context->refcount = 1;
  list_context->table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
  list_context->arr = NULL;
  list_context->cms_to_ready = 0;

  tp_dbus_daemon_list_activatable_names (bus_daemon, 2000,
      tp_list_connection_managers_got_names, list_context,
      (GDestroyNotify) list_context_unref, weak_object);
}

/**
 * tp_connection_manager_check_valid_name:
 * @name: a possible connection manager name
 * @error: used to raise %TP_ERROR_INVALID_ARGUMENT if %FALSE is returned
 *
 * Check that the given string is a valid connection manager name, i.e. that
 * it consists entirely of ASCII letters, digits and underscores, and starts
 * with a letter.
 *
 * Returns: %TRUE if @name is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_connection_manager_check_valid_name (const gchar *name,
                                        GError **error)
{
  const gchar *name_char;

  if (tp_str_empty (name))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "The empty string is not a valid connection manager name");
      return FALSE;
    }

  if (!g_ascii_isalpha (name[0]))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Not a valid connection manager name because first character "
          "is not an ASCII letter: %s", name);
      return FALSE;
    }

  for (name_char = name; *name_char != '\0'; name_char++)
    {
      if (!g_ascii_isalnum (*name_char) && *name_char != '_')
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Not a valid connection manager name because character '%c' "
              "is not an ASCII letter, digit or underscore: %s",
              *name_char, name);
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * tp_connection_manager_check_valid_protocol_name:
 * @name: a possible protocol name
 * @error: used to raise %TP_ERROR_INVALID_ARGUMENT if %FALSE is returned
 *
 * Check that the given string is a valid protocol name, i.e. that
 * it consists entirely of ASCII letters, digits and hyphen/minus, and starts
 * with a letter.
 *
 * Returns: %TRUE if @name is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_connection_manager_check_valid_protocol_name (const gchar *name,
                                                 GError **error)
{
  const gchar *name_char;

  if (name == NULL || name[0] == '\0')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "The empty string is not a valid protocol name");
      return FALSE;
    }

  if (!g_ascii_isalpha (name[0]))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Not a valid protocol name because first character "
          "is not an ASCII letter: %s", name);
      return FALSE;
    }

  for (name_char = name; *name_char != '\0'; name_char++)
    {
      if (!g_ascii_isalnum (*name_char) && *name_char != '-')
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Not a valid protocol name because character '%c' "
              "is not an ASCII letter, digit or hyphen/minus: %s",
              *name_char, name);
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * tp_connection_manager_get_name:
 * @self: a connection manager
 *
 * Return the internal name of this connection manager in the Telepathy
 * D-Bus API, e.g. "gabble" or "haze". This is often the name of the binary
 * without the "telepathy-" prefix.
 *
 * The returned string is valid as long as @self is. Copy it with g_strdup()
 * if a longer lifetime is required.
 *
 * Returns: the #TpConnectionManager:connection-manager property
 * Since: 0.7.26
 */
const gchar *
tp_connection_manager_get_name (TpConnectionManager *self)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), NULL);
  return self->name;
}

/**
 * tp_connection_manager_is_ready:
 * @self: a connection manager
 *
 * If protocol and parameter information has been obtained from the connection
 * manager or the cache in the .manager file, return %TRUE. Otherwise,
 * return %FALSE.
 *
 * This may change from %FALSE to %TRUE at any time that the main loop is
 * running; the #GObject::notify signal is emitted for the
 * #TpConnectionManager:info-source property.
 *
 * Returns: %TRUE, unless the #TpConnectionManager:info-source property is
 *          %TP_CM_INFO_SOURCE_NONE
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_is_ready (TpConnectionManager *self)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), FALSE);
  return self->info_source != TP_CM_INFO_SOURCE_NONE;
}

/**
 * tp_connection_manager_is_running:
 * @self: a connection manager
 *
 * Return %TRUE if this connection manager currently appears to be running.
 * This may change at any time that the main loop is running; the
 * #TpConnectionManager::activated and #TpConnectionManager::exited signals
 * are emitted.
 *
 * Returns: whether the connection manager is currently running
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_is_running (TpConnectionManager *self)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), FALSE);
  return self->running;
}

/**
 * tp_connection_manager_get_info_source:
 * @self: a connection manager
 *
 * If protocol and parameter information has been obtained from the connection
 * manager, return %TP_CM_INFO_SOURCE_LIVE; if it has been obtained from the
 * cache in the .manager file, return %TP_CM_INFO_SOURCE_FILE. If this
 * information has not yet been obtained, or obtaining it failed, return
 * %TP_CM_INFO_SOURCE_NONE.
 *
 * This may increase at any time that the main loop is running; the
 * #GObject::notify signal is emitted.
 *
 * Returns: the value of the #TpConnectionManager:info-source property
 * Since: 0.7.26
 */
TpCMInfoSource
tp_connection_manager_get_info_source (TpConnectionManager *self)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self),
      TP_CM_INFO_SOURCE_NONE);
  return self->info_source;
}

/**
 * tp_connection_manager_dup_protocol_names:
 * @self: a connection manager
 *
 * Returns a list of protocol names supported by this connection manager.
 * These are the internal protocol names used by the Telepathy specification
 * (e.g. "jabber" and "msn"), rather than user-visible names in any particular
 * locale.
 *
 * If this function is called before the connection manager information has
 * been obtained, the result is always %NULL. Use
 * tp_connection_manager_call_when_ready() to wait for this.
 *
 * The result is copied and must be freed by the caller, but it is not
 * necessarily still true after the main loop is re-entered.
 *
 * Returns: a #GStrv of protocol names
 * Since: 0.7.26
 */
gchar **
tp_connection_manager_dup_protocol_names (TpConnectionManager *self)
{
  GPtrArray *ret;
  guint i;

  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), NULL);

  if (self->info_source == TP_CM_INFO_SOURCE_NONE)
    return NULL;

  g_assert (self->priv->protocols != NULL);

  ret = g_ptr_array_sized_new (self->priv->protocols->len);

  for (i = 0; i < self->priv->protocols->len; i++)
    {
      TpConnectionManagerProtocol *proto = g_ptr_array_index (
          self->priv->protocols, i);

      if (proto != NULL)
        g_ptr_array_add (ret, g_strdup (proto->name));
    }

  g_ptr_array_add (ret, NULL);

  return (gchar **) g_ptr_array_free (ret, FALSE);
}

/**
 * tp_connection_manager_get_protocol:
 * @self: a connection manager
 * @protocol: the name of a protocol as defined in the Telepathy D-Bus API,
 *            e.g. "jabber" or "msn"
 *
 * Returns a structure representing a protocol, or %NULL if this connection
 * manager does not support the specified protocol.
 *
 * If this function is called before the connection manager information has
 * been obtained, the result is always %NULL. Use
 * tp_connection_manager_call_when_ready() to wait for this.
 *
 * The result is not necessarily valid after the main loop is re-entered.
 *
 * Returns: a structure representing the protocol
 * Since: 0.7.26
 */
const TpConnectionManagerProtocol *
tp_connection_manager_get_protocol (TpConnectionManager *self,
                                    const gchar *protocol)
{
  guint i;

  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), NULL);

  if (self->info_source == TP_CM_INFO_SOURCE_NONE)
    return NULL;

  g_assert (self->priv->protocols != NULL);

  for (i = 0; i < self->priv->protocols->len; i++)
    {
      TpConnectionManagerProtocol *proto = g_ptr_array_index (
          self->priv->protocols, i);

      if (proto != NULL && !tp_strdiff (proto->name, protocol))
        return proto;
    }

  return NULL;
}

/**
 * tp_connection_manager_has_protocol:
 * @self: a connection manager
 * @protocol: the name of a protocol as defined in the Telepathy D-Bus API,
 *            e.g. "jabber" or "msn"
 *
 * Return whether @protocol is supported by this connection manager.
 *
 * If this function is called before the connection manager information has
 * been obtained, the result is always %FALSE. Use
 * tp_connection_manager_call_when_ready() to wait for this.
 *
 * Returns: %TRUE if this connection manager supports @protocol
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_has_protocol (TpConnectionManager *self,
                                    const gchar *protocol)
{
  return (tp_connection_manager_get_protocol (self, protocol) != NULL);
}

/**
 * tp_connection_manager_protocol_has_param:
 * @protocol: structure representing a supported protocol
 * @param: a parameter name
 *
 * <!-- no more to say -->
 *
 * Returns: %TRUE if @protocol supports the parameter @param.
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_protocol_has_param (
    const TpConnectionManagerProtocol *protocol,
    const gchar *param)
{
  return (tp_connection_manager_protocol_get_param (protocol, param) != NULL);
}

/**
 * tp_connection_manager_protocol_get_param:
 * @protocol: structure representing a supported protocol
 * @param: a parameter name
 *
 * <!-- no more to say -->
 *
 * Returns: a structure representing the parameter @param, or %NULL if not
 *          supported
 * Since: 0.7.26
 */
const TpConnectionManagerParam *
tp_connection_manager_protocol_get_param (
    const TpConnectionManagerProtocol *protocol,
    const gchar *param)
{
  const TpConnectionManagerParam *ret = NULL;
  guint i;

  g_return_val_if_fail (protocol != NULL, NULL);

  for (i = 0; protocol->params[i].name != NULL; i++)
    {
      if (!tp_strdiff (param, protocol->params[i].name))
        {
          ret = &protocol->params[i];
          break;
        }
    }

  return ret;
}

/**
 * tp_connection_manager_protocol_can_register:
 * @protocol: structure representing a supported protocol
 *
 * Return whether a new account can be registered on this protocol, by setting
 * the special "register" parameter to %TRUE.
 *
 * Returns: %TRUE if @protocol supports the parameter "register"
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_protocol_can_register (
    const TpConnectionManagerProtocol *protocol)
{
  return tp_connection_manager_protocol_has_param (protocol, "register");
}

/**
 * tp_connection_manager_protocol_dup_param_names:
 * @protocol: a protocol supported by a #TpConnectionManager
 *
 * Returns a list of parameter names supported by this connection manager
 * for this protocol.
 *
 * The result is copied and must be freed by the caller with g_strfreev().
 *
 * Returns: a #GStrv of protocol names
 * Since: 0.7.26
 */
gchar **
tp_connection_manager_protocol_dup_param_names (
    const TpConnectionManagerProtocol *protocol)
{
  GPtrArray *ret;
  guint i;

  g_return_val_if_fail (protocol != NULL, NULL);

  ret = g_ptr_array_new ();

  for (i = 0; protocol->params[i].name != NULL; i++)
    g_ptr_array_add (ret, g_strdup (protocol->params[i].name));

  g_ptr_array_add (ret, NULL);
  return (gchar **) g_ptr_array_free (ret, FALSE);
}

/**
 * tp_connection_manager_param_get_name:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: the name of the parameter
 * Since: 0.7.26
 */
const gchar *
tp_connection_manager_param_get_name (const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, NULL);

  return param->name;
}

/**
 * tp_connection_manager_param_get_dbus_signature:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: the D-Bus signature of the parameter
 * Since: 0.7.26
 */
const gchar *
tp_connection_manager_param_get_dbus_signature (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, NULL);

  return param->dbus_signature;
}

/**
 * tp_connection_manager_param_is_required:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter is normally required
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_required (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED) != 0;
}

/**
 * tp_connection_manager_param_is_required_for_registration:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter is required when registering a new account
 *          (by setting the special "register" parameter to %TRUE)
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_required_for_registration (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_REGISTER) != 0;
}

/**
 * tp_connection_manager_param_is_secret:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter's value is a password or other secret
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_secret (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_SECRET) != 0;
}

/**
 * tp_connection_manager_param_is_dbus_property:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter represents a D-Bus property of the same name
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_dbus_property (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_DBUS_PROPERTY) != 0;
}

/**
 * tp_connection_manager_param_get_default:
 * @param: a parameter supported by a #TpConnectionManager
 * @value: pointer to an unset (all zeroes) #GValue into which the default's
 *         type and value are written
 *
 * Get the default value for this parameter, if there is one. If %FALSE is
 * returned, @value is left uninitialized.
 *
 * Returns: %TRUE if there is a default value
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_get_default (
    const TpConnectionManagerParam *param,
    GValue *value)
{
  g_return_val_if_fail (param != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (!G_IS_VALUE (value), FALSE);

  if ((param->flags & TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT) == 0
      || !G_IS_VALUE (&param->default_value))
    return FALSE;

  g_value_init (value, G_VALUE_TYPE (&param->default_value));
  g_value_copy (&param->default_value, value);

  return TRUE;
}
