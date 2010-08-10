/*
 * object used to request a channel from a TpAccount
 *
 * Copyright © 2010 Collabora Ltd.
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
 * SECTION: account-channel-request
 * @title: TpAccountChannelRequest
 * @short_description: Object used to request a channel from a #TpAccount
 *
 * A #TpAccountChannelRequest object is used to request a channel using the
 * ChannelDispatcher. Once created, use one of the create or ensure async
 * method to actually request the channel.
 *
 * Note that each #TpAccountChannelRequest object can only be used to create
 * one channel. You can't call a create or ensure method more than once on the
 * same #TpAccountChannelRequest.
 *
 * Once the channel has been created you can use the
 * TpAccountChannelRequest::re-handled: signal to be notified when the channel
 * has to be re-handled. This can be useful for example to move its window
 * to the foreground, if applicable.
 *
 * Since: 0.11.UNRELEASED
 */

/**
 * TpAccountChannelRequest:
 *
 * Data structure representing a #TpAccountChannelRequest object.
 *
 * Since: 0.11.UNRELEASED
 */

/**
 * TpAccountChannelRequestClass:
 *
 * The class of a #TpAccountChannelRequest.
 *
 * Since: 0.11.UNRELEASED
 */

#include "telepathy-glib/account-channel-request.h"
#include "telepathy-glib/account-channel-request-internal.h"

#include <telepathy-glib/channel-dispatcher.h>
#include <telepathy-glib/channel-request.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/simple-handler.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG TP_DEBUG_CLIENT
#include "telepathy-glib/debug-internal.h"
#include "telepathy-glib/_gen/signals-marshal.h"

struct _TpAccountChannelRequestClass {
    /*<private>*/
    GObjectClass parent_class;
};

G_DEFINE_TYPE(TpAccountChannelRequest,
    tp_account_channel_request, G_TYPE_OBJECT)

enum {
    PROP_ACCOUNT = 1,
    PROP_REQUEST,
    PROP_USER_ACTION_TIME,
    N_PROPS
};

enum {
  SIGNAL_RE_HANDLED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

struct _TpAccountChannelRequestPrivate
{
  TpAccount *account;
  GHashTable *request;
  gint64 user_action_time;

  TpBaseClient *handler;
  gboolean ensure;
  GCancellable *cancellable;
  GSimpleAsyncResult *result;
  TpChannelRequest *chan_request;
  gulong invalidated_sig;
  gulong cancel_id;
  TpChannel *channel;
  TpHandleChannelsContext *handle_context;

  /* TRUE if the channel has been requested (an _async function has been called
   * on the TpAccountChannelRequest) */
  gboolean requested;
};

static void
tp_account_channel_request_init (TpAccountChannelRequest *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TP_TYPE_ACCOUNT_CHANNEL_REQUEST,
      TpAccountChannelRequestPrivate);
}

static void
request_disconnect (TpAccountChannelRequest *self)
{
  if (self->priv->invalidated_sig == 0)
    return;

  g_assert (self->priv->chan_request != NULL);

  g_signal_handler_disconnect (self->priv->chan_request,
      self->priv->invalidated_sig);
  self->priv->invalidated_sig = 0;
}

static void
tp_account_channel_request_dispose (GObject *object)
{
  TpAccountChannelRequest *self = TP_ACCOUNT_CHANNEL_REQUEST (
      object);
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (tp_account_channel_request_parent_class)->dispose;

  request_disconnect (self);

  if (self->priv->cancel_id != 0)
    g_cancellable_disconnect (self->priv->cancellable, self->priv->cancel_id);

  tp_clear_object (&self->priv->account);
  tp_clear_pointer (&self->priv->request, g_hash_table_unref);
  tp_clear_object (&self->priv->handler);
  tp_clear_object (&self->priv->cancellable);
  tp_clear_object (&self->priv->result);
  tp_clear_object (&self->priv->chan_request);
  tp_clear_object (&self->priv->channel);
  tp_clear_object (&self->priv->handle_context);

  if (dispose != NULL)
    dispose (object);
}

static void
tp_account_channel_request_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpAccountChannelRequest *self = TP_ACCOUNT_CHANNEL_REQUEST (
      object);

  switch (property_id)
    {
      case PROP_ACCOUNT:
        g_value_set_object (value, self->priv->account);
        break;

      case PROP_REQUEST:
        g_value_set_object (value, self->priv->request);
        break;

      case PROP_USER_ACTION_TIME:
        g_value_set_int64 (value, self->priv->user_action_time);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
tp_account_channel_request_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpAccountChannelRequest *self = TP_ACCOUNT_CHANNEL_REQUEST (
      object);

  switch (property_id)
    {
      case PROP_ACCOUNT:
        self->priv->account = g_value_dup_object (value);
        break;

      case PROP_REQUEST:
        self->priv->request = g_value_dup_boxed (value);
        break;

      case PROP_USER_ACTION_TIME:
        self->priv->user_action_time = g_value_get_int64 (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
tp_account_channel_request_constructed (GObject *object)
{
  TpAccountChannelRequest *self = TP_ACCOUNT_CHANNEL_REQUEST (
      object);
  void (*chain_up) (GObject *) =
    ((GObjectClass *)
      tp_account_channel_request_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  g_assert (self->priv->account != NULL);
  g_assert (self->priv->request != NULL);
}

static void
tp_account_channel_request_class_init (
    TpAccountChannelRequestClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (TpAccountChannelRequestPrivate));

  object_class->get_property = tp_account_channel_request_get_property;
  object_class->set_property = tp_account_channel_request_set_property;
  object_class->constructed = tp_account_channel_request_constructed;
  object_class->dispose = tp_account_channel_request_dispose;

  /**
   * TpAccountChannelRequest:account:
   *
   * The #TpAccount used to request the channel.
   * Read-only except during construction.
   *
   * This property can't be %NULL.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_object ("account", "TpAccount",
      "The TpAccount used to request the channel",
      TP_TYPE_ACCOUNT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCOUNT,
      param_spec);

  /**
   * TpAccountChannelRequest:request:
   *
   * The #TpAccount used to request the channel.
   * Read-only except during construction.
   *
   * This property can't be %NULL.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("request", "GHashTable",
      "A dictionary containing desirable properties for the channel",
      TP_HASH_TYPE_STRING_VARIANT_MAP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUEST,
      param_spec);

  /**
   * TpAccountChannelRequest:user-action-time:
   *
   * The user action time that will be passed to mission-control when
   * requesting the channel.
   *
   * This property can't be %NULL.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_int64 ("user-action-time", "user action time",
      "UserActionTime",
      G_MININT64, G_MAXINT64, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_USER_ACTION_TIME,
      param_spec);

 /**
   * TpAccountChannelRequest::re-handled:
   * @self: a #TpAccountChannelRequest
   * @user_action_time: the time at which user action occurred, or 0 if this
   * channel is to be handled for some reason not involving user action.
   * @context: a #TpHandleChannelsContext representing the context of
   * the HandleChannels() call.
   *
   * Emitted when channel which has been created using @self has be re-handled.
   *
   * Since: 0.11.UNRELEASED
   */
  signals[SIGNAL_RE_HANDLED] = g_signal_new (
      "re-handled", G_OBJECT_CLASS_TYPE (cls),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      _tp_marshal_VOID__INT64_OBJECT,
      G_TYPE_NONE, 2, G_TYPE_INT64, TP_TYPE_HANDLE_CHANNELS_CONTEXT);
}

/**
 * tp_account_channel_request_new:
 * @account: a #TpAccount
 * @request: (transfer none) (element-type utf8 GObject.Value): the requested
 * properties of the channel
 * @user_action_time: the user action time to pass to the channel dispatcher
 * when requesting the channel
 *
 * Convenience function to create a new #TpAccountChannelRequest object.
 *
 * Returns: a new #TpAccountChannelRequest object
 *
 * Since: 0.11.UNRELEASED
 */
TpAccountChannelRequest *
tp_account_channel_request_new (
    TpAccount *account,
    GHashTable *request,
    gint64 user_action_time)
{
  g_return_val_if_fail (TP_IS_ACCOUNT (account), NULL);
  g_return_val_if_fail (request != NULL, NULL);

  return g_object_new (TP_TYPE_ACCOUNT_CHANNEL_REQUEST,
      "account", account,
      "request", request,
      "user-action-time", user_action_time,
      NULL);
}

/**
 * tp_account_channel_request_get_account:
 * @self: a #TpAccountChannelRequest
 *
 * Return the #TpAccountChannelRequest:account construct-only property
 *
 * Returns: the value of #TpAccountChannelRequest:account
 *
 * Since: 0.11.UNRELEASED
 */
TpAccount *
tp_account_channel_request_get_account (
    TpAccountChannelRequest *self)
{
  return self->priv->account;
}

/**
 * tp_account_channel_request_get_request:
 * @self: a #TpAccountChannelRequest
 *
 * Return the #TpAccountChannelRequest:request construct-only property
 *
 * Returns: the value of #TpAccountChannelRequest:request
 *
 * Since: 0.11.UNRELEASED
 */
GHashTable *
tp_account_channel_request_get_request (
    TpAccountChannelRequest *self)
{
  return self->priv->request;
}

/**
 * tp_account_channel_request_get_user_action_time:
 * @self: a #TpAccountChannelRequest
 *
 * Return the #TpAccountChannelRequest:user-action-time construct-only property
 *
 * Returns: the value of #TpAccountChannelRequest:user-action-time
 *
 * Since: 0.11.UNRELEASED
 */
gint64
tp_account_channel_request_get_user_action_time (
    TpAccountChannelRequest *self)
{
  return self->priv->user_action_time;
}

static void
request_fail (TpAccountChannelRequest *self,
    const GError *error)
{
  request_disconnect (self);
  g_simple_async_result_set_from_error (self->priv->result, error);
  g_simple_async_result_complete (self->priv->result);
}

static void
request_complete (TpAccountChannelRequest *self,
    TpChannel *channel,
    TpHandleChannelsContext *handle_context)
{
  g_assert (self->priv->result != NULL);

  self->priv->channel = g_object_ref (channel);
  self->priv->handle_context = g_object_ref (handle_context);

  g_simple_async_result_complete (self->priv->result);

  /* We just need to keep the Handler around. */
  request_disconnect (self);
  tp_clear_object (&self->priv->result);
  tp_clear_object (&self->priv->chan_request);
}

static void
channel_invalidated_cb (TpProxy *chan,
    guint domain,
    gint code,
    gchar *message,
    TpAccountChannelRequest *self)
{
  /* Channel has been destroyed, we can remove the Handler */
  DEBUG ("Channel has been invalidated (%s), unref ourself", message);
  g_object_unref (self);
}

static void
handle_channels (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  TpAccountChannelRequest *self = user_data;
  TpChannel *channel;

  if (G_UNLIKELY (g_list_length (channels) != 1))
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "We are supposed to handle only one channel" };

      tp_handle_channels_context_fail (context, &error);

      request_fail (self, &error);
      return;
    }

  if (self->priv->result == NULL)
    {
      /* We are re-handling the channel, no async request to complete */
      g_signal_emit (self, signals[SIGNAL_RE_HANDLED], 0, user_action_time,
          context);

      goto out;
    }

  /* Request succeeded */
  channel = channels->data;

  if (tp_proxy_get_invalidated (channel) == NULL)
    {
      /* Keep the handler alive while the channel is valid so keep a ref on
       * ourself until the channel is invalidated */
      g_object_ref (self);

      g_signal_connect (channel, "invalidated",
          G_CALLBACK (channel_invalidated_cb), self);
    }

  request_complete (self, channel, context);

out:
  tp_handle_channels_context_accept (context);
}

static void
channel_request_succeeded (TpAccountChannelRequest *self)
{
  GError err = { TP_ERRORS, TP_ERROR_NOT_YOURS,
      "Another Handler is handling this channel" };

  if (self->priv->result == NULL)
    /* Our handler has been called, all good */
    return;

  /* Our handler hasn't be called but the channel request is complete.
   * That means another handler handled the channels so we don't own it. */
  request_fail (self, &err);
}

static void
channel_request_proceed_cb (TpChannelRequest *request,
  const GError *error,
  gpointer user_data,
  GObject *weak_object)
{
  TpAccountChannelRequest *self = user_data;

  if (error != NULL)
    {
      DEBUG ("Proceed failed: %s", error->message);

      request_fail (self, error);
      return;
    }

  DEBUG ("Proceed succeeded; waiting for the channel to be handled");
}

static void
channel_request_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    TpAccountChannelRequest *self)
{
  GError error = { domain, code, message };

  if (g_error_matches (&error, TP_DBUS_ERRORS, TP_DBUS_ERROR_OBJECT_REMOVED))
    {
      /* Object has been removed without error, so ChannelRequest succeeded */
      channel_request_succeeded (self);
      return;
    }

  DEBUG ("ChannelRequest has been invalidated: %s", message);

  request_fail (self, &error);
}

static void
channel_request_cancel_cb (TpChannelRequest *request,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  /* Don't do anything, we rely on the invalidation of the channel request to
   * complete the operation */
  if (error != NULL)
    {
      DEBUG ("ChannelRequest.Cancel() failed: %s", error->message);
      return;
    }

  DEBUG ("ChannelRequest.Cancel() succeeded");
}

static void
operation_cancelled_cb (GCancellable *cancellable,
    TpAccountChannelRequest *self)
{
  if (self->priv->chan_request == NULL)
    {
      DEBUG ("ChannelRequest has been invalidated, we can't cancel any more");
      return;
    }

  DEBUG ("Operation has been cancelled, cancel the channel request");

  tp_cli_channel_request_call_cancel (self->priv->chan_request, -1,
      channel_request_cancel_cb, self, NULL, G_OBJECT (self));
}

static void
request_and_handle_channel_cb (TpChannelDispatcher *cd,
    const gchar *channel_request_path,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpAccountChannelRequest *self = user_data;
  GError *err = NULL;

  if (error != NULL)
    {
      DEBUG ("%s failed: %s",
          self->priv->ensure ? "EnsureChannel" : "CreateChannel",
          error->message);

      request_fail (self, error);
      return;
    }

  DEBUG ("Got ChannelRequest: %s", channel_request_path);

  self->priv->chan_request = tp_channel_request_new (
      tp_base_client_get_dbus_daemon (self->priv->handler),
      channel_request_path, NULL, &err);

  if (self->priv->chan_request == NULL)
    {
      DEBUG ("Failed to create ChannelRequest: %s", err->message);
      goto fail;
    }

  self->priv->invalidated_sig = g_signal_connect (self->priv->chan_request,
      "invalidated", G_CALLBACK (channel_request_invalidated_cb), self);

  if (self->priv->cancellable != NULL)
    {
      self->priv->cancel_id = g_cancellable_connect (self->priv->cancellable,
          G_CALLBACK (operation_cancelled_cb), self, NULL);

      /* We just aborted the operation so we're done */
      if (g_cancellable_is_cancelled (self->priv->cancellable))
        return;
    }

  DEBUG ("Calling ChannelRequest.Proceed()");

  tp_cli_channel_request_call_proceed (self->priv->chan_request, -1,
      channel_request_proceed_cb, self, NULL, G_OBJECT (self));

  return;

fail:
  request_fail (self, err);
  g_error_free (err);
}

static void
request_and_handle_channel_async (TpAccountChannelRequest *self,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data,
    gboolean ensure)
{
  GError *error = NULL;
  TpDBusDaemon *dbus;
  TpChannelDispatcher *cd;

  g_return_if_fail (!self->priv->requested);
  self->priv->requested = TRUE;

  if (g_cancellable_is_cancelled (cancellable))
    {
      g_simple_async_report_error_in_idle (G_OBJECT (self), callback,
          user_data, G_IO_ERROR, G_IO_ERROR_CANCELLED,
          "Operation has been cancelled");

      return;
    }

  dbus = tp_proxy_get_dbus_daemon (self->priv->account);

  if (cancellable != NULL)
    self->priv->cancellable = g_object_ref (cancellable);
  self->priv->ensure = ensure;

  /* Create a temp handler */
  self->priv->handler = tp_simple_handler_new (dbus, TRUE, FALSE,
      "TpGLibRequestAndHandle", TRUE, handle_channels, self, NULL);

  if (!tp_base_client_register (self->priv->handler, &error))
    {
      DEBUG ("Failed to register temp handler: %s", error->message);

      g_simple_async_report_gerror_in_idle (G_OBJECT (self), callback,
          user_data, error);

      g_error_free (error);
      return;
    }

  cd = tp_channel_dispatcher_new (dbus);

  if (ensure)
    {
      self->priv->result = g_simple_async_result_new (G_OBJECT (self), callback,
          user_data,
          tp_account_channel_request_ensure_and_handle_channel_async);

      tp_cli_channel_dispatcher_call_ensure_channel (cd, -1,
          tp_proxy_get_object_path (self->priv->account), self->priv->request,
          self->priv->user_action_time,
          tp_base_client_get_bus_name (self->priv->handler),
          request_and_handle_channel_cb, self, NULL, G_OBJECT (self));
    }
  else
    {
      self->priv->result = g_simple_async_result_new (G_OBJECT (self), callback,
          user_data,
          tp_account_channel_request_create_and_handle_channel_async);

      tp_cli_channel_dispatcher_call_create_channel (cd, -1,
          tp_proxy_get_object_path (self->priv->account), self->priv->request,
          self->priv->user_action_time,
          tp_base_client_get_bus_name (self->priv->handler),
          request_and_handle_channel_cb, self, NULL, G_OBJECT (self));
    }

  g_object_unref (cd);
}

static TpChannel *
request_and_handle_channel_finish (TpAccountChannelRequest *self,
    GAsyncResult *result,
    TpHandleChannelsContext **context,
    gpointer source_tag,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (TP_IS_ACCOUNT_CHANNEL_REQUEST (self), FALSE);
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
          G_OBJECT (self), source_tag),
      FALSE);

  if (context != NULL)
    *context = g_object_ref (self->priv->handle_context);

  return g_object_ref (self->priv->channel);
}

/**
 * tp_account_channel_request_create_and_handle_channel_async:
 * @self: a #TpAccountChannelRequest
 * @cancellable: optional #GCancellable object, %NULL to ignore
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Asynchronously calls CreateChannel on the ChannelDispatcher to create a
 * channel with the properties defined in #TpAccountChannelRequest:request
 * that you are going to handle yourself.
 * When the operation is finished, @callback will be called. You can then call
 * tp_account_channel_request_create_and_handle_channel_finish() to get the
 * result of the operation.
 *
 * Since: 0.11.UNRELEASED
 */
void
tp_account_channel_request_create_and_handle_channel_async (
    TpAccountChannelRequest *self,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  request_and_handle_channel_async (self, cancellable, callback, user_data,
      FALSE);
}

/**
 * tp_account_channel_request_create_and_handle_channel_finish:
 * @self: a #TpAccountChannelRequest
 * @result: a #GAsyncResult
 * @context: (out): pointer used to return a reference on the context of the
 * HandleChannels() call, or %NULL
 * @error: a #GError to fill
 *
 * Finishes an async channel creation started using
 * tp_account_channel_request_create_and_handle_channel_async().
 *
 * Returns: a new reference on a #TpChannel if the channel was successfully
 * created and you are handling it, otherwise %NULL.
 *
 * Since: 0.11.UNRELEASED
 */
TpChannel *
tp_account_channel_request_create_and_handle_channel_finish (
    TpAccountChannelRequest *self,
    GAsyncResult *result,
    TpHandleChannelsContext **context,
    GError **error)
{
  return request_and_handle_channel_finish (self, result, context,
      tp_account_channel_request_create_and_handle_channel_async, error);
}

/**
 * tp_account_channel_request_ensure_and_handle_channel_async:
 * @self: a #TpAccountChannelRequest
 * @cancellable: optional #GCancellable object, %NULL to ignore
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Asynchronously calls EnsureChannel on the ChannelDispatcher to create a
 * channel with the properties defined in #TpAccountChannelRequest:request
 * that you are going to handle yourself.
 * When the operation is finished, @callback will be called. You can then call
 * tp_account_channel_request_ensure_and_handle_channel_finish() to get the
 * result of the operation.
 *
 * If the channel already exists and is already being handled, or if a
 * newly created channel is sent to a different handler, this operation
 * will fail with the error %TP_ERROR_NOT_YOURS. The other handler
 * will be notified that the channel was requested again, and can
 * move its window to the foreground, if applicable.
 *
 * Since: 0.11.UNRELEASED
 */
void
tp_account_channel_request_ensure_and_handle_channel_async (
    TpAccountChannelRequest *self,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  request_and_handle_channel_async (self, cancellable, callback, user_data,
      TRUE);
}

/**
 * tp_account_channel_request_ensure_and_handle_channel_finish:
 * @self: a #TpAccountChannelRequest
 * @result: a #GAsyncResult
 * @context: (out): pointer used to return a reference on the context of the
 * HandleChannels() call, or %NULL
 * @error: a #GError to fill
 *
 * Finishes an async channel creation started using
 * tp_account_channel_request_ensure_and_handle_channel_async().
 *
 * If the channel already exists and is already being handled, or if a
 * newly created channel is sent to a different handler, this operation
 * will fail with the error %TP_ERROR_NOT_YOURS.
 *
 * Returns: a new reference on a #TpChannel if the channel was successfully
 * created and you are handling it, otherwise %NULL.
 *
 * Since: 0.11.UNRELEASED
 */
TpChannel *
tp_account_channel_request_ensure_and_handle_channel_finish (
    TpAccountChannelRequest *self,
    GAsyncResult *result,
    TpHandleChannelsContext **context,
    GError **error)
{
  return request_and_handle_channel_finish (self, result, context,
      tp_account_channel_request_ensure_and_handle_channel_async, error);
}
