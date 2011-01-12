/*
 * call-stream.c - Source for TfCallStream
 * Copyright (C) 2010 Collabora Ltd.
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
 * SECTION:tfcallstream
 */


#include "call-stream.h"

#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>
#include <gst/farsight/fs-conference-iface.h>

#include <stdarg.h>
#include <string.h>

#include <telepathy-glib/proxy-subclass.h>

#include "extensions/extensions.h"

#include "tf-signals-marshal.h"
#include "utils.h"


G_DEFINE_TYPE (TfCallStream, tf_call_stream, G_TYPE_OBJECT);

static void tf_call_stream_dispose (GObject *object);

static void
tf_call_stream_class_init (TfCallStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tf_call_stream_dispose;
}

static void
tf_call_stream_init (TfCallStream *self)
{
}

static void
tf_call_stream_dispose (GObject *object)
{
  TfCallStream *self = TF_CALL_STREAM (object);

  g_debug (G_STRFUNC);

  if (self->proxy)
    g_object_unref (self->proxy);
  self->proxy = NULL;

  if (self->stun_servers)
    g_boxed_free (TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST, self->stun_servers);
  self->stun_servers = NULL;

  if (self->relay_info)
    g_boxed_free (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST, self->relay_info);
  self->relay_info = NULL;

  if (self->fsstream)
    _tf_call_content_put_fsstream (self->call_content, self->fsstream);
  self->fsstream = NULL;

  if (self->endpoint)
    g_object_unref (self->endpoint);
  self->endpoint = NULL;

  g_free (self->creds_username);
  self->creds_username = NULL;
  g_free (self->creds_password);
  self->creds_password = NULL;

  fs_candidate_list_destroy (self->stored_remote_candidates);
  self->stored_remote_candidates = NULL;

  if (G_OBJECT_CLASS (tf_call_stream_parent_class)->dispose)
    G_OBJECT_CLASS (tf_call_stream_parent_class)->dispose (object);
}

static void
local_sending_state_changed (TfFutureCallStream *proxy,
    guint arg_State,
    gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);

  self->local_sending_state = arg_State;

  if (!self->fsstream)
    return;

  if (arg_State == TF_FUTURE_SENDING_STATE_PENDING_SEND ||
      arg_State == TF_FUTURE_SENDING_STATE_SENDING)
    {
      if (!self->has_send_resource)
        {
          if (_tf_content_start_sending (TF_CONTENT (self->call_content)))
            {
              self->has_send_resource = TRUE;
            }
          else
            {
              tf_future_cli_call_stream_call_set_sending (self->proxy,
                  -1, TF_FUTURE_SENDING_STATE_NONE,
                  TF_FUTURE_STREAM_SENDING_CHANGE_REASON_RESOURCE_UNAVAILABLE,
                  "", "Could not open send resource",
                  NULL, NULL, NULL, NULL);

              return;
            }
        }
    }

  switch (arg_State)
    {
    case TF_FUTURE_SENDING_STATE_PENDING_SEND:
      tf_future_cli_call_stream_call_set_sending (
          self->proxy, -1, TF_FUTURE_SENDING_STATE_SENDING, 0, "", "",
          NULL, NULL, NULL, NULL);
      /* fallthrough */
    case TF_FUTURE_SENDING_STATE_SENDING:
      g_object_set (self->fsstream, "direction", FS_DIRECTION_BOTH, NULL);
      break;
    case TF_FUTURE_SENDING_STATE_PENDING_STOP_SENDING:
      tf_future_cli_call_stream_call_set_sending (self->proxy,
          -1, TF_FUTURE_SENDING_STATE_NONE, 0, "", "",
              NULL, NULL, NULL, NULL);
      /* fallthrough */
    case TF_FUTURE_SENDING_STATE_NONE:
      if (self->has_send_resource)
        {
          _tf_content_stop_sending (TF_CONTENT (self->call_content));

          self->has_send_resource = FALSE;
        }
      g_object_set (self->fsstream, "direction", FS_DIRECTION_RECV, NULL);
      break;
    }
}


static void
tf_call_stream_try_adding_fsstream (TfCallStream *self)
{
  gchar *transmitter;
  GError *error = NULL;
  guint n_params = 0;
  GParameter params[6];
  GList *preferred_local_candidates = NULL;
  guint i;

  if (!self->server_info_retrieved ||
      !self->has_contact ||
      !self->has_media_properties)
    return;

  switch (self->transport_type)
    {
    case TF_FUTURE_STREAM_TRANSPORT_TYPE_RAW_UDP:
      transmitter = "rawudp";

      switch (tf_call_content_get_fs_media_type (self->call_content))
        {
        case TP_MEDIA_STREAM_TYPE_VIDEO:
          preferred_local_candidates = g_list_prepend (NULL,
              fs_candidate_new (NULL, FS_COMPONENT_RTP, FS_CANDIDATE_TYPE_HOST,
                  FS_NETWORK_PROTOCOL_UDP, NULL, 9078));
          break;
        case TP_MEDIA_STREAM_TYPE_AUDIO:
          preferred_local_candidates = g_list_prepend (NULL,
              fs_candidate_new (NULL, FS_COMPONENT_RTP, FS_CANDIDATE_TYPE_HOST,
                  FS_NETWORK_PROTOCOL_UDP, NULL, 7078));
        default:
          break;
        }

      if (preferred_local_candidates)
        {
          params[n_params].name = "preferred-local-candidates";
          g_value_init (&params[n_params].value, FS_TYPE_CANDIDATE_LIST);
          g_value_take_boxed (&params[n_params].value,
              preferred_local_candidates);
          n_params++;
        }
      break;
    case TF_FUTURE_STREAM_TRANSPORT_TYPE_ICE:
    case TF_FUTURE_STREAM_TRANSPORT_TYPE_GTALK_P2P:
    case TF_FUTURE_STREAM_TRANSPORT_TYPE_WLM_2009:
      transmitter = "nice";

      /* MISSING: Set Controlling mode property here */

      params[n_params].name = "compatibility-mode";
      g_value_init (&params[n_params].value, G_TYPE_UINT);
      switch (self->transport_type)
        {
        case TF_FUTURE_STREAM_TRANSPORT_TYPE_ICE:
          g_value_set_uint (&params[n_params].value, 0);
          break;
        case TF_FUTURE_STREAM_TRANSPORT_TYPE_GTALK_P2P:
          g_value_set_uint (&params[n_params].value, 1);
          self->multiple_usernames = TRUE;
          break;
        case TF_FUTURE_STREAM_TRANSPORT_TYPE_WLM_2009:
          g_value_set_uint (&params[n_params].value, 3);
          break;
        default:
          break;
        }
      n_params++;
      break;
    case TF_FUTURE_STREAM_TRANSPORT_TYPE_SHM:
      transmitter = "shm";
      break;
    default:
       tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
           "org.freedesktop.Telepathy.Error.NotImplemented",
           "Unknown transport type %d", self->transport_type);
    }

  if (self->stun_servers->len)
    {
      GValueArray *gva = g_ptr_array_index (self->stun_servers, 0);
      gchar *ip;
      guint16 port;
      gchar *conn_timeout_str;

      /* We only use the first STUN server if there are many */

      tp_value_array_unpack (gva, 2, &ip, &port);

      params[n_params].name = "stun-ip";
      g_value_init (&params[n_params].value, G_TYPE_STRING);
      g_value_set_string (&params[n_params].value, ip);
      n_params++;

      params[n_params].name = "stun-port";
      g_value_init (&params[n_params].value, G_TYPE_UINT);
      g_value_set_uint (&params[n_params].value, port);
      n_params++;

      conn_timeout_str = getenv ("FS_CONN_TIMEOUT");
      if (conn_timeout_str)
        {
          gint conn_timeout = strtol (conn_timeout_str, NULL, 10);

          params[n_params].name = "stun-timeout";
          g_value_init (&params[n_params].value, G_TYPE_UINT);
          g_value_set_uint (&params[n_params].value, conn_timeout);
          n_params++;
        }
    }

  if (self->relay_info->len)
    {
      GValueArray *fs_relay_info = g_value_array_new (0);
      GValue val = {0};
      g_value_init (&val, GST_TYPE_STRUCTURE);

      for (i = 0; i < self->relay_info->len; i++)
        {
          GHashTable *one_relay = g_ptr_array_index(self->relay_info, i);
          const gchar *type = NULL;
          const gchar *ip;
          guint32 port;
          const gchar *username;
          const gchar *password;
          guint component;
          GstStructure *s;

          ip = tp_asv_get_string (one_relay, "ip");
          port = tp_asv_get_uint32 (one_relay, "port", NULL);
          type = tp_asv_get_string (one_relay, "type");
          username = tp_asv_get_string (one_relay, "username");
          password = tp_asv_get_string (one_relay, "password");
          component = tp_asv_get_uint32 (one_relay, "component", NULL);

          if (!ip || !port || !username || !password)
              continue;

          if (!type)
            type = "udp";

          s = gst_structure_new ("relay-info",
              "ip", G_TYPE_STRING, ip,
              "port", G_TYPE_UINT, port,
              "username", G_TYPE_STRING, username,
              "password", G_TYPE_STRING, password,
              "type", G_TYPE_STRING, type,
              NULL);

          if (component)
            gst_structure_set (s, "component", G_TYPE_UINT, component, NULL);


          g_value_take_boxed (&val, s);

          g_value_array_append (fs_relay_info, &val);
          g_value_reset (&val);
        }

      if (fs_relay_info->n_values)
        {
          params[n_params].name = "relay-info";
          g_value_init (&params[n_params].value, G_TYPE_VALUE_ARRAY);
          g_value_set_boxed (&params[n_params].value, fs_relay_info);
          n_params++;
        }

      g_value_array_free (fs_relay_info);
    }

  self->fsstream = _tf_call_content_get_fsstream_by_handle (self->call_content,
      self->contact_handle,
      transmitter,
      n_params,
      params,
      &error);

  for (i = 0; i < n_params; i++)
    g_value_unset (&params[i].value);

  if (!self->fsstream)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "",
          "Could not create FsStream: %s", error->message);
      return;
    }

  if (self->local_sending_state == TF_FUTURE_SENDING_STATE_PENDING_SEND ||
      self->local_sending_state == TF_FUTURE_SENDING_STATE_SENDING)
    local_sending_state_changed (self->proxy, self->local_sending_state,
        NULL, (GObject *) self);
}

static void
server_info_retrieved (TfFutureCallStream *proxy,
    gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);

  self->server_info_retrieved = TRUE;

  tf_call_stream_try_adding_fsstream (self);
}

static void
relay_info_changed (TfFutureCallStream *proxy,
    const GPtrArray *arg_Relay_Info,
    gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);

  if (self->server_info_retrieved)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "org.freedesktop.Telepathy.Error.NotImplemented",
          "Changing relay servers after ServerInfoRetrived is not implemented");
      return;
    }

  /* Ignore signals that come before the basic info has been retrived */
  if (!self->relay_info)
    return;

  g_boxed_free (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST, self->relay_info);
  self->relay_info = g_boxed_copy (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST,
      arg_Relay_Info);
}

static void
stun_servers_changed (TfFutureCallStream *proxy,
    const GPtrArray *arg_Servers,
    gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);

  if (self->server_info_retrieved)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "org.freedesktop.Telepathy.Error.NotImplemented",
          "Changing STUN servers after ServerInfoRetrived is not implemented");
      return;
    }

  /* Ignore signals that come before the basic info has been retrived */
  if (!self->stun_servers)
    return;

  g_boxed_free (TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST, self->stun_servers);
  self->stun_servers = g_boxed_copy (TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST,
      arg_Servers);
}

static void
tf_call_stream_add_remote_candidate (TfCallStream *self,
    const GPtrArray *candidates)
{
  GList *fscandidates = NULL;
  guint i;
  GError *error = NULL;

  for (i = 0; i < candidates->len; i++)
    {
      GValueArray *tpcandidate = g_ptr_array_index (candidates, i);
      guint component;
      gchar *ip;
      guint16 port;
      GHashTable *extra_info;
      const gchar *foundation;
      guint priority;
      const gchar *username;
      const gchar *password;
      gboolean valid;
      FsCandidate *cand;

      tp_value_array_unpack (tpcandidate, 4, &component, &ip, &port,
          &extra_info);

      foundation = tp_asv_get_string (extra_info, "Foundation");
      if (!foundation)
        foundation = "";
      priority = tp_asv_get_uint32 (extra_info, "Priority", &valid);
      if (!valid)
        priority = 0;

      username = tp_asv_get_string (extra_info, "Username");
      if (!username)
        username = self->creds_username;

      password = tp_asv_get_string (extra_info, "Password");
      if (!password)
        password = self->creds_password;

      cand = fs_candidate_new (foundation, component, FS_CANDIDATE_TYPE_HOST,
          FS_NETWORK_PROTOCOL_UDP, ip, port);
      cand->priority = priority;
      cand->username = g_strdup (username);
      cand->password = g_strdup (password);

      fscandidates = g_list_append (fscandidates, cand);
    }

  if (self->fsstream)
    {
      if (!fs_stream_set_remote_candidates (self->fsstream, fscandidates,
              &error))
        {
          tf_call_content_error (self->call_content,
              TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
              "Error setting the remote candidates: %s", error->message);
          g_clear_error (&error);
        }
      fs_candidate_list_destroy (fscandidates);
    }
  else
    {
      self->stored_remote_candidates =
          g_list_concat (self->stored_remote_candidates, fscandidates);
    }
}

static void
remote_candidates_added (TpProxy *proxy,
    const GPtrArray *arg_Candidates,
    gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);

  tf_call_stream_add_remote_candidate (self, arg_Candidates);
}

static void
remote_credentials_set (TpProxy *proxy,
    const gchar *arg_Username,
    const gchar *arg_Password,
    gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);

  if ((self->creds_username && strcmp (self->creds_username, arg_Username)) ||
      (self->creds_password && strcmp (self->creds_password, arg_Password)))
    {
      /* Remote credentials changed, this will perform a ICE restart, so
       * clear old remote candidates */
      fs_candidate_list_destroy (self->stored_remote_candidates);
      self->stored_remote_candidates = NULL;
    }

  g_free (self->creds_username);
  g_free (self->creds_password);
  self->creds_username = g_strdup (arg_Username);
  self->creds_password = g_strdup (arg_Password);
}


static void
got_endpoint_properties (TpProxy *proxy, GHashTable *out_Properties,
    const GError *error, gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);
  GValueArray *credentials;
  gchar *username, *password;
  GPtrArray *candidates;

  if (error)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "", "Error getting the Streams's media properties: %s",
          error->message);
      return;
    }

  if (!out_Properties)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "", "Error getting the Stream's media properties: there are none");
      return;
    }


  credentials = tp_asv_get_boxed (out_Properties, "RemoteCredentials",
      TF_FUTURE_STRUCT_TYPE_STREAM_CREDENTIALS);
  if (!credentials)
    goto invalid_property;
  tp_value_array_unpack (credentials, 2, &username, &password);
  self->creds_username = g_strdup (username);
  self->creds_password = g_strdup (password);

  candidates = tp_asv_get_boxed (out_Properties, "RemoteCandidates",
      TF_FUTURE_ARRAY_TYPE_CANDIDATE_LIST);
  if (!candidates)
    goto invalid_property;

  tf_call_stream_add_remote_candidate (self, candidates);


  return;

 invalid_property:
  tf_call_content_error (self->call_content,
      TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
      "Error getting the Endpoint's properties: invalid type");
}

static void
tf_call_stream_add_endpoint (TfCallStream *self)
{
  GError *error = NULL;

  self->endpoint = g_object_new (TP_TYPE_PROXY,
      "dbus-daemon", tp_proxy_get_dbus_daemon (self->proxy),
      "bus-name", tp_proxy_get_bus_name (self->proxy),
      "object-path", self->endpoint_objpath,
      NULL);
  tp_proxy_add_interface_by_id (TP_PROXY (self->endpoint),
      TF_FUTURE_IFACE_QUARK_CALL_STREAM_ENDPOINT);

  tf_future_cli_call_stream_endpoint_connect_to_remote_credentials_set (
      TP_PROXY (self->endpoint), remote_credentials_set, NULL, NULL,
      G_OBJECT (self), &error);
  if (error)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
          "Error connectiong to RemoteCredentialsSet signal: %s",
          error->message);
      g_clear_error (&error);
      return;
    }

  tf_future_cli_call_stream_endpoint_connect_to_remote_candidates_added (
      TP_PROXY (self->endpoint), remote_candidates_added, NULL, NULL,
      G_OBJECT (self), &error);
  if (error)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
          "Error connectiong to RemoteCandidatesAdded signal: %s",
          error->message);
      g_clear_error (&error);
      return;
    }

  tp_cli_dbus_properties_call_get_all (self->endpoint, -1,
      TF_FUTURE_IFACE_CALL_STREAM_ENDPOINT,
      got_endpoint_properties, NULL, NULL, G_OBJECT (self));
}


static void
endpoints_changed (TfFutureCallStream *proxy,
    const GPtrArray *arg_Endpoints_Added,
    const GPtrArray *arg_Endpoints_Removed,
    gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);

  /* Ignore signals before getting the properties to avoid races */
  if (!self->has_media_properties)
    return;

  if (arg_Endpoints_Removed->len != 0)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "org.freedesktop.Telepathy.Error.NotImplemented",
          "Removing Endpoints is not implemented");
      return;
    }

  if (arg_Endpoints_Added->len != 1)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "org.freedesktop.Telepathy.Error.NotImplemented",
          "Having more than one endpoint is not implemented");
      return;
    }

  if (self->endpoint_objpath)
    {
      if (strcmp (g_ptr_array_index (arg_Endpoints_Added, 0),
              self->endpoint_objpath))
        tf_call_content_error (self->call_content,
            TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
            "",
            "Trying to give a different endpoint, CM bug");
      return;
    }

  self->endpoint_objpath = g_strdup (
      g_ptr_array_index (arg_Endpoints_Added, 0));
  tf_call_stream_add_endpoint (self);
}


static void
got_stream_media_properties (TpProxy *proxy, GHashTable *out_Properties,
    const GError *error, gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);
  GPtrArray *stun_servers;
  GPtrArray *relay_info;
  GPtrArray *endpoints;
  gboolean valid;

  if (error)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "", "Error getting the Streams's media properties: %s",
          error->message);
      return;
    }

  if (!out_Properties)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "", "Error getting the Stream's media properties: there are none");
      return;
    }

  self->transport_type =
      tp_asv_get_uint32 (out_Properties, "Transport", &valid);
  if (!valid)
    goto invalid_property;

  stun_servers = tp_asv_get_boxed (out_Properties, "STUNServers",
      TP_STRUCT_TYPE_SOCKET_ADDRESS_IP);
  if (!stun_servers)
    goto invalid_property;

  relay_info = tp_asv_get_boxed (out_Properties, "STUNServers",
      TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST);
  if (!relay_info)
    goto invalid_property;

  self->server_info_retrieved = tp_asv_get_boolean (out_Properties,
      "HasServerInfo", &valid);
  if (!valid)
    goto invalid_property;

  self->stun_servers = g_boxed_copy (TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST,
      stun_servers);
  self->relay_info = g_boxed_copy (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST,
      relay_info);

  endpoints = tp_asv_get_boxed (out_Properties, "Endpoints",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST);

  if (endpoints->len > 1)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "org.freedesktop.Telepathy.Error.NotImplemented",
          "Having more than one endpoint is not implemented");
      return;
    }

  if (endpoints->len == 1)
    {
      self->endpoint_objpath = g_strdup (g_ptr_array_index (endpoints, 0));
      tf_call_stream_add_endpoint (self);
    }

  self->has_media_properties = TRUE;

  tf_call_stream_try_adding_fsstream (self);

  return;
 invalid_property:
  tf_call_content_error (self->call_content,
      TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
      "Error getting the Stream's properties: invalid type");
  return;
}


static void
got_stream_properties (TpProxy *proxy, GHashTable *out_Properties,
    const GError *error, gpointer user_data, GObject *weak_object)
{
  TfCallStream *self = TF_CALL_STREAM (weak_object);
  gboolean valid;
  GError *myerror = NULL;
  guint i;
  const gchar * const * interfaces;
  gboolean got_media_interface = FALSE;
  gboolean local_sending_state;
  GHashTable *members;
  GHashTableIter iter;
  gpointer key, value;

  if (error)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "", "Error getting the Streams's properties: %s", error->message);
      return;
    }

  if (!out_Properties)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "", "Error getting the Content's properties: there are none");
      return;
    }

  interfaces = tp_asv_get_strv (out_Properties, "Interfaces");

  for (i = 0; interfaces[i]; i++)
    if (!strcmp (interfaces[i], TF_FUTURE_IFACE_CALL_STREAM_INTERFACE_MEDIA))
      {
        got_media_interface = TRUE;
        break;
      }

  if (!got_media_interface)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "", "Stream does not have the media interface,"
          " but HardwareStreaming was NOT true");
      return;
    }

  members = tp_asv_get_boxed (out_Properties, "RemoteMembers",
      TF_FUTURE_HASH_TYPE_CONTACT_SENDING_STATE_MAP);
  if (!members)
    goto invalid_property;

  local_sending_state = tp_asv_get_boolean (out_Properties, "LocalSendingState",
      &valid);
  if (!valid)
    goto invalid_property;

  self->local_sending_state = local_sending_state;

  if (g_hash_table_size (members) != 1)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR,
          "org.freedesktop.Telepathy.Error.NotImplemented",
          "Only one Member per Stream is supported, there are %d",
          g_hash_table_size (members));
      return;
    }

  g_hash_table_iter_init (&iter, members);

  if (g_hash_table_iter_next (&iter, &key, &value))
    {
      self->has_contact = TRUE;
      self->contact_handle = GPOINTER_TO_UINT (key);
    }

  tp_proxy_add_interface_by_id (TP_PROXY (self->proxy),
      TF_FUTURE_IFACE_QUARK_CALL_STREAM_INTERFACE_MEDIA);

  tf_future_cli_call_stream_interface_media_connect_to_server_info_retrieved (
      TF_FUTURE_CALL_STREAM (proxy), server_info_retrieved, NULL, NULL,
      G_OBJECT (self), &myerror);
  if (myerror)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
          "Error connectiong to ServerInfoRetrived signal: %s",
          myerror->message);
      g_clear_error (&myerror);
      return;
    }

  tf_future_cli_call_stream_interface_media_connect_to_stun_servers_changed (
      TF_FUTURE_CALL_STREAM (proxy), stun_servers_changed, NULL, NULL,
      G_OBJECT (self), &myerror);
  if (myerror)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
          "Error connectiong to ServerInfoRetrived signal: %s",
          myerror->message);
      g_clear_error (&myerror);
      return;
    }


  tf_future_cli_call_stream_interface_media_connect_to_relay_info_changed (
      TF_FUTURE_CALL_STREAM (proxy), relay_info_changed, NULL, NULL,
      G_OBJECT (self), &myerror);
  if (myerror)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
          "Error connectiong to ServerInfoRetrived signal: %s",
          myerror->message);
      g_clear_error (&myerror);
      return;
    }


  tf_future_cli_call_stream_interface_media_connect_to_endpoints_changed (
      TF_FUTURE_CALL_STREAM (proxy), endpoints_changed, NULL, NULL,
      G_OBJECT (self), &myerror);
  if (myerror)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
          "Error connectiong to EndpointsChanged signal: %s",
          myerror->message);
      g_clear_error (&myerror);
      return;
    }

  tp_cli_dbus_properties_call_get_all (proxy, -1,
      TF_FUTURE_IFACE_CALL_STREAM_INTERFACE_MEDIA,
      got_stream_media_properties, NULL, NULL, G_OBJECT (self));

  return;

 invalid_property:
  tf_call_content_error (self->call_content,
      TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
      "Error getting the Stream's properties: invalid type");
  return;
}

TfCallStream *
tf_call_stream_new (TfCallChannel *call_channel,
    TfCallContent *call_content,
    const gchar *object_path,
    GError **error)
{
  TfCallStream *self;
  TfFutureCallStream *proxy = tf_future_call_stream_new (call_channel->proxy,
      object_path, error);
  GError *myerror = NULL;

  if (!proxy)
    return NULL;

  self = g_object_new (TF_TYPE_STREAM, NULL);

  self->call_content = call_content;
  self->proxy = proxy;

  tf_future_cli_call_stream_connect_to_local_sending_state_changed (
      TF_FUTURE_CALL_STREAM (proxy), local_sending_state_changed, NULL, NULL,
      G_OBJECT (self), &myerror);
  if (myerror)
    {
      tf_call_content_error (self->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "",
          "Error connectiong to LocalSendingStateChanged signal: %s",
          myerror->message);
      g_object_unref (self);
      g_propagate_error (error, myerror);
      return NULL;
    }

  tp_cli_dbus_properties_call_get_all (proxy, -1, TF_FUTURE_IFACE_CALL_STREAM,
      got_stream_properties, NULL, NULL, G_OBJECT (self));

  return self;
}

static void
cb_fs_new_local_candidate (TfCallStream *stream, FsCandidate *candidate)
{
  GPtrArray *candidate_list = g_ptr_array_sized_new (1);
  GValueArray *gva;
  GHashTable *extra_info;

  extra_info = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  if (candidate->priority)
    tp_asv_set_uint32 (extra_info, "Priority", candidate->priority);

  if (candidate->foundation)
    tp_asv_set_string (extra_info, "Foundation", candidate->foundation);

  if (stream->multiple_usernames)
    {
      if (candidate->username)
        tp_asv_set_string (extra_info, "Username", candidate->username);
      if (candidate->password)
        tp_asv_set_string (extra_info, "Password", candidate->password);
    }
  else
    {
      if ((!stream->last_local_username && candidate->username) ||
          (!stream->last_local_password && candidate->password) ||
          (stream->last_local_username &&
              strcmp (candidate->username, stream->last_local_username)) ||
          (stream->last_local_password &&
              strcmp (candidate->password, stream->last_local_password)))
        {
          g_free (stream->last_local_username);
          g_free (stream->last_local_password);
          stream->last_local_username = g_strdup (candidate->username);
          stream->last_local_password = g_strdup (candidate->password);

          if (!stream->last_local_username)
            stream->last_local_username = g_strdup ("");
          if (!stream->last_local_password)
            stream->last_local_password = g_strdup ("");

          /* Add a callback to kill Call on errors */
          tf_future_cli_call_stream_interface_media_call_set_credentials (
              stream->proxy, -1, stream->last_local_username,
              stream->last_local_password, NULL, NULL, NULL, NULL);

        }
    }

  gva = tp_value_array_build (G_TYPE_UINT, candidate->component_id,
      G_TYPE_STRING, candidate->ip,
      G_TYPE_UINT, candidate->port,
      G_TYPE_HASH_TABLE, extra_info,
      G_TYPE_INVALID);

  g_ptr_array_add (candidate_list, gva);

  /* Should also check for errors */
  tf_future_cli_call_stream_interface_media_call_add_candidates (stream->proxy,
      -1, candidate_list, NULL, NULL, NULL, NULL);


  g_boxed_free (TF_FUTURE_ARRAY_TYPE_CANDIDATE_LIST, candidate_list);
}

static void
cb_fs_local_candidates_prepared (TfCallStream *stream)
{
  tf_future_cli_call_stream_interface_media_call_candidates_prepared (
      stream->proxy, -1, NULL, NULL, NULL, NULL);

}

static void
cb_fs_component_state_changed (TfCallStream *stream, guint component,
    FsStreamState fsstate)
{
  TpMediaStreamState state;

  if (!stream->endpoint)
    return;

  switch (fsstate)
  {
    case FS_STREAM_STATE_FAILED:
    case FS_STREAM_STATE_DISCONNECTED:
      state = TP_MEDIA_STREAM_STATE_DISCONNECTED;
      break;
    case FS_STREAM_STATE_GATHERING:
    case FS_STREAM_STATE_CONNECTING:
    case FS_STREAM_STATE_CONNECTED:
      state = TP_MEDIA_STREAM_STATE_CONNECTING;
      break;
    case FS_STREAM_STATE_READY:
    default:
      state = TP_MEDIA_STREAM_STATE_CONNECTED;
      break;
  }

  tf_future_cli_call_stream_endpoint_call_set_stream_state (stream->endpoint,
      -1, state, NULL, NULL, NULL, NULL);
}


gboolean
tf_call_stream_bus_message (TfCallStream *stream, GstMessage *message)
{
  const GstStructure *s;
  const GValue *val;

  if (!stream->fsstream)
    return FALSE;

  s = gst_message_get_structure (message);

  if (gst_structure_has_name (s, "farsight-error"))
    {
      GObject *object;
      const gchar *msg;
      FsError errorno;
      GEnumClass *enumclass;
      GEnumValue *enumvalue;
      const gchar *debug;

      val = gst_structure_get_value (s, "src-object");
      object = g_value_get_object (val);

      if (object != (GObject*) stream->fsstream)
        return FALSE;

      val = gst_structure_get_value (s, "error-no");
      errorno = g_value_get_enum (val);
      msg = gst_structure_get_string (s, "error-msg");
      debug = gst_structure_get_string (s, "debug-msg");

      enumclass = g_type_class_ref (FS_TYPE_ERROR);
      enumvalue = g_enum_get_value (enumclass, errorno);
      g_warning ("error (%s (%d)): %s : %s",
          enumvalue->value_nick, errorno, msg, debug);
      g_type_class_unref (enumclass);

      tf_call_content_error (stream->call_content,
          TF_FUTURE_CONTENT_REMOVAL_REASON_ERROR, "", msg);
      return TRUE;
    }

  val = gst_structure_get_value (s, "stream");
  if (!val)
    return FALSE;
  if (!G_VALUE_HOLDS_OBJECT (val))
    return FALSE;
  if (g_value_get_object (val) != stream->fsstream)
    return FALSE;

  if (gst_structure_has_name (s, "farsight-new-local-candidate"))
    {
      FsCandidate *candidate;

      val = gst_structure_get_value (s, "candidate");
      candidate = g_value_get_boxed (val);

      cb_fs_new_local_candidate (stream, candidate);
      return TRUE;
    }
  else if (gst_structure_has_name (s, "farsight-local-candidates-prepared"))
    {
      cb_fs_local_candidates_prepared (stream);
    }
  else if (gst_structure_has_name (s, "farsight-component-state-changed"))
    {
      guint component;
      FsStreamState fsstate;

      if (!gst_structure_get_uint (s, "component", &component) ||
          !gst_structure_get_enum (s, "state", FS_TYPE_STREAM_STATE,
              (gint*) &fsstate))
        return TRUE;

      cb_fs_component_state_changed (stream, component, fsstate);
    }
  else
    {
      return FALSE;
    }

  return TRUE;
}
