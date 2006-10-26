/*
 * gabble-media-session.c - Source for GabbleMediaSession
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "ansi.h"
#include "debug.h"
#include "handles.h"
#include "namespaces.h"
#include "util.h"

#include "telepathy-errors.h"
#include "telepathy-helpers.h"

#include "gabble-connection.h"
#include "gabble-media-channel.h"
#include "gabble-media-stream.h"
#include "gabble-presence-cache.h"
#include "gabble-presence.h"

#include "gabble-media-session.h"
#include "gabble-media-session-signals-marshal.h"
#include "gabble-media-session-glue.h"

G_DEFINE_TYPE(GabbleMediaSession, gabble_media_session, G_TYPE_OBJECT)

#define DEFAULT_SESSION_TIMEOUT 50000

#define GTALK_STREAM_NAME "gtalk"

/* 99 streams gives us a max name length of 8 (videoXX\0 or audioXX\0) */
#define MAX_STREAMS 99
#define MAX_STREAM_NAME_LEN 8

/* signal enum */
enum
{
    NEW_STREAM_HANDLER,
    STREAM_ADDED,
    TERMINATED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_MEDIA_CHANNEL,
  PROP_OBJECT_PATH,
  PROP_SESSION_ID,
  PROP_INITIATOR,
  PROP_PEER,
  PROP_PEER_RESOURCE,
  PROP_STATE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaSessionPrivate GabbleMediaSessionPrivate;

struct _GabbleMediaSessionPrivate
{
  GabbleConnection *conn;
  GabbleMediaChannel *channel;
  GabbleMediaSessionMode mode;
  gchar *object_path;

  GHashTable *streams;

  gchar *id;
  JingleInitiator initiator;
  GabbleHandle peer;
  gchar *peer_resource;

  JingleSessionState state;
  gboolean ready;
  gboolean locally_accepted;
  gboolean terminated;

  guint timer_id;

  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_SESSION_GET_PRIVATE(obj) \
    ((GabbleMediaSessionPrivate *)obj->priv)

typedef struct {
    gchar *name;
    gchar *attributes;
} SessionStateDescription;

static const SessionStateDescription session_states[] =
{
    { "JS_STATE_PENDING_CREATED",       ANSI_BOLD_ON ANSI_FG_BLACK ANSI_BG_WHITE   },
    { "JS_STATE_PENDING_INITIATE_SENT", ANSI_BOLD_ON               ANSI_BG_CYAN    },
    { "JS_STATE_PENDING_INITIATED",     ANSI_BOLD_ON               ANSI_BG_MAGENTA },
    { "JS_STATE_PENDING_ACCEPT_SENT",   ANSI_BOLD_ON               ANSI_BG_CYAN    },
    { "JS_STATE_ACTIVE",                ANSI_BOLD_ON               ANSI_BG_BLUE    },
    { "JS_STATE_ENDED",                                            ANSI_BG_RED     }
};

static void
gabble_media_session_init (GabbleMediaSession *self)
{
  GabbleMediaSessionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MEDIA_SESSION, GabbleMediaSessionPrivate);

  self->priv = priv;

  priv->mode = MODE_JINGLE;
  priv->state = JS_STATE_PENDING_CREATED;
  priv->streams = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
}

static void stream_close_cb (GabbleMediaStream *stream,
                             GabbleMediaSession *session);
static void stream_connection_state_changed_cb (GabbleMediaStream *stream,
                                                GParamSpec *param,
                                                GabbleMediaSession *session);
static void stream_got_local_codecs_changed_cb (GabbleMediaStream *stream,
                                                GParamSpec *param,
                                                GabbleMediaSession *session);

static void
_emit_new_stream (const gchar *name,
                  GabbleMediaStream *stream,
                  GabbleMediaSession *session)
{
  gchar *object_path;
  guint id, media_type;

  g_object_get (stream,
                "object-path", &object_path,
                "id", &id,
                "media-type", &media_type,
                NULL);

  /* all of the streams are bidirectional from farsight's point of view, it's
   * just in the signalling they change */
  g_signal_emit (session, signals[NEW_STREAM_HANDLER], 0,
                 object_path, id, media_type,
                 TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);
}

static GabbleMediaStream *
create_media_stream (GabbleMediaSession *session,
                     const gchar *name,
                     JingleInitiator initiator,
                     guint media_type)
{
  GabbleMediaSessionPrivate *priv;
  gchar *object_path;
  GabbleMediaStream *stream;
  guint id;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));
  g_assert (name != NULL);

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  /* assert that if we're in google mode:
   *  - we only try to make one stream
   *  - it's an audio stream
   *  - it's called GTALK_STREAM_NAME */
  if (priv->mode == MODE_GOOGLE)
    {
      g_assert (g_hash_table_size (priv->streams) == 0);
      g_assert (media_type == TP_MEDIA_STREAM_TYPE_AUDIO);
      g_assert (!g_strdiff (name, GTALK_STREAM_NAME));
    }

  g_assert (g_hash_table_size (priv->streams) < MAX_STREAMS);
  g_assert (g_hash_table_lookup (priv->streams, name) == NULL);

  id = _gabble_media_channel_get_stream_id (priv->channel);

  GMS_DEBUG_INFO (session,
      "creating new %s %s stream called \"%s\" with id %u",
      priv->mode == MODE_GOOGLE ? "google" : "jingle",
      media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
      name, id);

  object_path = g_strdup_printf ("%s/MediaStream%u", priv->object_path, id);

  stream = g_object_new (GABBLE_TYPE_MEDIA_STREAM,
                         "connection", priv->conn,
                         "media-session", session,
                         "object-path", object_path,
                         "mode", priv->mode,
                         "name", name,
                         "id", id,
                         "initiator", initiator,
                         "media-type", media_type,
                         NULL);

  /* for incoming jingle streams, set the direction to none, so that the
   * direction handler adds the right flags */
  if (priv->mode == MODE_JINGLE && initiator == INITIATOR_REMOTE)
    g_object_set (stream,
        "combined-direction", TP_MEDIA_STREAM_DIRECTION_NONE,
        NULL);

  /* for incoming streams, set the signalling state to ACKNOWLEDGED */
  if (initiator == INITIATOR_REMOTE)
    g_object_set (stream,
        "signalling-state", STREAM_SIG_STATE_ACKNOWLEDGED,
        NULL);

  g_signal_connect (stream, "close",
                    (GCallback) stream_close_cb,
                    session);
  g_signal_connect (stream, "notify::connection-state",
                    (GCallback) stream_connection_state_changed_cb,
                    session);
  g_signal_connect (stream, "notify::got-local-codecs",
                    (GCallback) stream_got_local_codecs_changed_cb,
                    session);

  g_hash_table_insert (priv->streams, g_strdup (name), stream);

  g_free (object_path);

  if (priv->ready)
    _emit_new_stream (name, stream, session);

  g_signal_emit (session, signals[STREAM_ADDED], 0, stream);

  return stream;
}

static GObject *
gabble_media_session_constructor (GType type, guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaSessionPrivate *priv;
  DBusGConnection *bus;

  obj = G_OBJECT_CLASS (gabble_media_session_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (GABBLE_MEDIA_SESSION (obj));

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_media_session_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_MEDIA_CHANNEL:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_SESSION_ID:
      g_value_set_string (value, priv->id);
      break;
    case PROP_INITIATOR:
      g_value_set_uint (value, priv->initiator);
      break;
    case PROP_PEER:
      g_value_set_uint (value, priv->peer);
      break;
    case PROP_PEER_RESOURCE:
      g_value_set_string (value, priv->peer_resource);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void session_state_changed (GabbleMediaSession *session,
                                   JingleSessionState prev_state,
                                   JingleSessionState new_state);

static void
gabble_media_session_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  JingleSessionState prev_state;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_MEDIA_CHANNEL:
      priv->channel = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_SESSION_ID:
      g_free (priv->id);
      priv->id = g_value_dup_string (value);
      break;
    case PROP_INITIATOR:
      priv->initiator = g_value_get_uint (value);
      break;
    case PROP_PEER:
      priv->peer = g_value_get_uint (value);
      break;
    case PROP_PEER_RESOURCE:
      g_free (priv->peer_resource);
      priv->peer_resource = g_value_dup_string (value);
      break;
    case PROP_STATE:
      prev_state = priv->state;
      priv->state = g_value_get_uint (value);

      if (priv->state == JS_STATE_ENDED)
        g_assert (priv->terminated);

      if (priv->state != prev_state)
        session_state_changed (session, prev_state, priv->state);

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_media_session_dispose (GObject *object);
static void gabble_media_session_finalize (GObject *object);

static void
gabble_media_session_class_init (GabbleMediaSessionClass *gabble_media_session_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_session_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_media_session_class, sizeof (GabbleMediaSessionPrivate));

  object_class->constructor = gabble_media_session_constructor;

  object_class->get_property = gabble_media_session_get_property;
  object_class->set_property = gabble_media_session_set_property;

  object_class->dispose = gabble_media_session_dispose;
  object_class->finalize = gabble_media_session_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "media session's channel.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("media-channel", "GabbleMediaChannel object",
                                    "Gabble media channel object that owns this "
                                    "media session object.",
                                    GABBLE_TYPE_MEDIA_CHANNEL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_CHANNEL, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("session-id", "Session ID",
                                    "A unique session identifier used "
                                    "throughout all communication.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION_ID, param_spec);

  param_spec = g_param_spec_uint ("initiator", "Session initiator",
                                  "An enum signifying which end initiated "
                                  "the session.",
                                  INITIATOR_LOCAL,
                                  INITIATOR_REMOTE,
                                  INITIATOR_LOCAL,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_uint ("peer", "Session peer",
                                  "The GabbleHandle representing the contact "
                                  "with whom this session communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_string ("peer-resource",
                                    "Session peer's resource",
                                    "The resource of the contact "
                                    "with whom this session communicates, "
                                    "if applicable",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_WRITABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
                                   param_spec);

  param_spec = g_param_spec_uint ("state", "Session state",
                                  "The current state that the session is in.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[NEW_STREAM_HANDLER] =
    g_signal_new ("new-stream-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_session_marshal_VOID__STRING_UINT_UINT_UINT,
                  G_TYPE_NONE, 4, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  signals[STREAM_ADDED] =
    g_signal_new ("stream-added",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[TERMINATED] =
    g_signal_new ("terminated",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_session_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_media_session_class), &dbus_glib_gabble_media_session_object_info);
}

static void
gabble_media_session_dispose (GObject *object)
{
  GabbleMediaSession *self = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  DEBUG ("called");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _gabble_media_session_terminate (self, INITIATOR_LOCAL, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  if (priv->timer_id != 0)
    g_source_remove (priv->timer_id);

  g_hash_table_destroy (priv->streams);
  priv->streams = NULL;

  if (G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose (object);
}

static void
gabble_media_session_finalize (GObject *object)
{
  GabbleMediaSession *self = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  g_free (priv->id);
  g_free (priv->object_path);
  g_free (priv->peer_resource);
  G_OBJECT_CLASS (gabble_media_session_parent_class)->finalize (object);
}


static void
_steal_one_stream (const gchar *name,
                   GabbleMediaStream *stream,
                   GPtrArray **arr)
{
  g_ptr_array_add (*arr, stream);
}

/**
 * gabble_media_session_error
 *
 * Implements D-Bus method Error
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_session_error (GabbleMediaSession *self,
                            guint errno,
                            const gchar *message,
                            GError **error)
{
  GabbleMediaSessionPrivate *priv;
  GPtrArray *streams;
  guint i;

  g_assert (GABBLE_IS_MEDIA_SESSION (self));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  GMS_DEBUG_INFO (self, "Media.SessionHandler::Error called, error %u (%s) -- "
      "emitting error on each stream", errno, message);

  if (priv->state == JS_STATE_ENDED)
    {
      return TRUE;
    }
  else if (priv->state == JS_STATE_PENDING_CREATED)
    {
      /* shortcut to prevent sending remove actions if we haven't sent an
       * initiate yet */
      g_object_set (self, "state", JS_STATE_ENDED, NULL);
      return TRUE;
    }

  g_assert (priv->streams != NULL);

  streams = g_ptr_array_sized_new (g_hash_table_size (priv->streams));
  g_hash_table_foreach (priv->streams, (GHFunc) _steal_one_stream, &streams);

  for (i = 0; i < streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (streams, i);

      if (!gabble_media_stream_error (stream, errno, message, error))
        {
          g_ptr_array_free (streams, TRUE);
          return FALSE;
        }
    }

  g_ptr_array_free (streams, TRUE);

  return TRUE;
}


/**
 * gabble_media_session_ready
 *
 * Implements D-Bus method Ready
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_session_ready (GabbleMediaSession *self,
                            GError **error)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (self));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  priv->ready = TRUE;

  g_hash_table_foreach (priv->streams, (GHFunc) _emit_new_stream, self);

  return TRUE;
}

static GabbleMediaStream *
_lookup_stream_by_name (GabbleMediaSession *session,
                        const gchar *stream_name)
{
  GabbleMediaSessionPrivate *priv;
  GabbleMediaStream *stream;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  stream = g_hash_table_lookup (priv->streams, stream_name);

  return stream;
}


static gboolean
_handle_create (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  GabbleMediaSessionMode session_mode;
  TpMediaStreamType stream_type;

  if (stream != NULL)
    {
      GMS_DEBUG_WARNING (session, "can't create new stream called \"%s\", it "
          "already exists; rejecting", stream_name);
      return FALSE;
    }

  if (desc_node == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to create stream without a "
          "content description");
      return FALSE;
    }

  if (lm_message_node_has_namespace (desc_node,
        NS_GOOGLE_SESSION_PHONE, NULL))
    {
      session_mode = MODE_GOOGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_AUDIO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_AUDIO, NULL))
    {
      session_mode = MODE_JINGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_AUDIO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_VIDEO, NULL))
    {
      session_mode = MODE_JINGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_VIDEO;
    }
  else
    {
      GMS_DEBUG_WARNING (session, "refusing to create stream for "
          "unsupported content description");
      return FALSE;
    }

  /* MODE_GOOGLE is allowed to have a null transport node */
  if (session_mode == MODE_JINGLE && trans_node == NULL)
    {
      GMS_DEBUG_WARNING (session, "refusing to create stream for "
          "unsupported transport");
      return FALSE;
    }

  if (session_mode != priv->mode)
    {
      if (g_hash_table_size (priv->streams) > 0)
        {
          GMS_DEBUG_WARNING (session, "refusing to change mode because "
              "streams already exist");
          return FALSE;
        }
      else
        {
          GMS_DEBUG_INFO (session, "setting session mode to %s",
              session_mode == MODE_GOOGLE ? "google" : "jingle");
          priv->mode = session_mode;
        }
    }

  if (g_hash_table_size (priv->streams) == MAX_STREAMS)
    {
      GMS_DEBUG_WARNING (session, "refusing to create more than "
          G_STRINGIFY (MAX_STREAMS) " streams");
      return FALSE;
    }

  stream = create_media_stream (session, stream_name, INITIATOR_REMOTE,
                                stream_type);

  return TRUE;
}


static TpMediaStreamDirection
_senders_to_direction (GabbleMediaSession *session,
                       const gchar *senders)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  TpMediaStreamDirection ret = TP_MEDIA_STREAM_DIRECTION_NONE;

  if (!g_strdiff (senders, "initiator"))
    {
      if (priv->initiator == INITIATOR_LOCAL)
        ret = TP_MEDIA_STREAM_DIRECTION_SEND;
      else
        ret = TP_MEDIA_STREAM_DIRECTION_RECEIVE;
    }
  else if (!g_strdiff (senders, "responder"))
    {
      if (priv->initiator == INITIATOR_REMOTE)
        ret = TP_MEDIA_STREAM_DIRECTION_SEND;
      else
        ret = TP_MEDIA_STREAM_DIRECTION_RECEIVE;
    }
  else if (!g_strdiff (senders, "both"))
    {
      ret = TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;
    }

  return ret;
}

static gboolean send_direction_change (GabbleMediaSession *session,
    GabbleMediaStream *stream, TpMediaStreamDirection dir, GError **error);

static gboolean
_handle_direction (GabbleMediaSession *session,
                   LmMessage *message,
                   LmMessageNode *content_node,
                   const gchar *stream_name,
                   GabbleMediaStream *stream,
                   LmMessageNode *desc_node,
                   LmMessageNode *trans_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  const gchar *senders;
  CombinedStreamDirection combined_dir, new_combined_dir;
  TpMediaStreamDirection requested_dir, current_dir;
  TpMediaStreamPendingSend pending_send;

  if (priv->mode == MODE_GOOGLE)
    return TRUE;

  if (stream == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle direction for unknown "
          "stream \"%s\"", stream_name);
      return FALSE;
    }

  requested_dir = TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;

  senders = lm_message_node_get_attribute (content_node, "senders");
  if (senders != NULL)
    requested_dir = _senders_to_direction (session, senders);

  if (requested_dir == TP_MEDIA_STREAM_DIRECTION_NONE)
    {
      GMS_DEBUG_WARNING (session, "received invalid content senders value "
          "\"%s\" on stream \"%s\"; rejecting", senders, stream_name);
      return FALSE;
    }

  g_object_get (stream, "combined-direction", &combined_dir, NULL);

  current_dir = COMBINED_DIRECTION_GET_DIRECTION (combined_dir);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND (combined_dir);

  GMS_DEBUG_INFO (session, "received request for senders \"%s\" on stream "
      "\"%s\"", senders, stream_name);

  /* if local sending has been added, remove it,
   * and set the pending local send flag */
  if (((current_dir & TP_MEDIA_STREAM_DIRECTION_SEND) == 0) &&
    ((requested_dir & TP_MEDIA_STREAM_DIRECTION_SEND) != 0))
    {
      GMS_DEBUG_INFO (session, "setting pending local send flag");
      requested_dir &= ~TP_MEDIA_STREAM_DIRECTION_SEND;
      pending_send |= TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
    }

#if 0
  /* clear any pending remote send */
  if ((pending_send & TP_MEDIA_STREAM_PENDING_REMOTE_SEND) != 0)
    {
      GMS_DEBUG_INFO (session, "setting pending local send flag");
      pending_send &= ~TP_MEDIA_STREAM_PENDING_REMOTE_SEND;
    }
#endif

  /* make any necessary changes */
  new_combined_dir = MAKE_COMBINED_DIRECTION (requested_dir, pending_send);
  if (new_combined_dir != combined_dir)
    g_object_set (stream, "combined-direction", new_combined_dir, NULL);

  return TRUE;
}


static gboolean
_handle_accept (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node)
{
  if (stream == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle accept for unknown stream "
          "\"%s\"", stream_name);
      return FALSE;
    }

  g_object_set (stream, "playing", TRUE, NULL);

  return TRUE;
}


static gboolean
_handle_codecs (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node)
{
  if (stream == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle codecs for unknown stream "
          "\"%s\"", stream_name);
      return FALSE;
    }

  if (desc_node == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle codecs without a content "
          "description node");
      return FALSE;
    }

  if (!_gabble_media_stream_post_remote_codecs (stream, message, desc_node))
    {
      GMS_DEBUG_INFO (session, "_gabble_media_stream_post_remote_codecs "
          "failed");
      return FALSE;
    }

  return TRUE;
}


static gboolean
_handle_candidates (GabbleMediaSession *session,
                    LmMessage *message,
                    LmMessageNode *content_node,
                    const gchar *stream_name,
                    GabbleMediaStream *stream,
                    LmMessageNode *desc_node,
                    LmMessageNode *trans_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (stream == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle candidates for unknown "
          "stream \"%s\"", stream_name);
      return FALSE;
    }

  if (trans_node == NULL)
    {
      if (priv->mode == MODE_GOOGLE)
        {
          trans_node = content_node;
        }
      else
        {
          GMS_DEBUG_WARNING (session, "unable to handle candidates without a "
              "transport node");
          return FALSE;
        }
    }

  if (!_gabble_media_stream_post_remote_candidates (stream, message,
        trans_node))
    {
      GMS_DEBUG_INFO (session, "_gabble_media_stream_post_remote_candidates "
          "failed");
      return FALSE;
    }

  return TRUE;
}


static gboolean
_handle_remove (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (stream == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle content-remove for "
          "unknown stream \"%s\"", stream_name);
      return FALSE;
    }

  /* reducing a session to contain 0 streams is invalid; instead the peer
   * should terminate the session. I guess we'll do it for them... */
  if (g_hash_table_size (priv->streams) == 1)
    {
      return FALSE;
    }

  /* close the stream */
  _gabble_media_stream_close (stream);

  return TRUE;
}


static gboolean
_handle_terminate (GabbleMediaSession *session,
                   LmMessage *message,
                   LmMessageNode *content_node,
                   const gchar *stream_name,
                   GabbleMediaStream *stream,
                   LmMessageNode *desc_node,
                   LmMessageNode *trans_node)
{
  DEBUG ("called for %s", stream_name);

  _gabble_media_session_terminate (session, INITIATOR_REMOTE, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  return TRUE;
}


typedef gboolean (*StreamHandlerFunc)(GabbleMediaSession *session,
                                      LmMessage *message,
                                      LmMessageNode *content_node,
                                      const gchar *stream_name,
                                      GabbleMediaStream *stream,
                                      LmMessageNode *desc_node,
                                      LmMessageNode *trans_node);

typedef struct _Handler Handler;

struct _Handler {
  const gchar *actions[3];
  JingleSessionState min_allowed_state;
  JingleSessionState max_allowed_state;
  StreamHandlerFunc stream_handlers[4];
  JingleSessionState new_state;
};

static Handler handlers[] = {
  {
    { "initiate", "session-initiate", NULL },
    JS_STATE_PENDING_CREATED,
    JS_STATE_PENDING_CREATED,
    { _handle_create, _handle_direction, _handle_codecs, NULL },
    JS_STATE_PENDING_INITIATED
  },
  {
    { "accept", "session-accept", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_PENDING_INITIATED,
    { _handle_direction, _handle_codecs, _handle_accept, NULL },
    JS_STATE_ACTIVE
  },
  {
    { "reject", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_PENDING_INITIATED,
    { _handle_terminate, NULL },
    JS_STATE_INVALID
  },
  {
    { "terminate", "session-terminate", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ENDED,
    { _handle_terminate, NULL },
    JS_STATE_INVALID
  },
  {
    { "candidates", "transport-info", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_candidates, NULL },
    JS_STATE_INVALID
  },
  {
    { "content-add", NULL },
    JS_STATE_ACTIVE,
    JS_STATE_ACTIVE,
    { _handle_create, _handle_direction, _handle_codecs, NULL },
    JS_STATE_INVALID,
  },
  {
    { "content-modify", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_direction, NULL },
    JS_STATE_INVALID
  },
  {
    { "content-accept", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_direction, _handle_codecs, _handle_accept, NULL },
    JS_STATE_INVALID
  },
  {
    { "content-remove", "content-decline", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_remove, NULL },
    JS_STATE_INVALID
  },
  {
    { NULL },
    JS_STATE_INVALID,
    JS_STATE_INVALID,
    { NULL },
    JS_STATE_INVALID
  }
};


static gboolean
_call_handlers_on_stream (GabbleMediaSession *session,
                          LmMessage *message,
                          LmMessageNode *content_node,
                          const gchar *stream_name,
                          StreamHandlerFunc *func)
{
  GabbleMediaStream *stream = NULL;
  LmMessageNode *desc_node = NULL, *trans_node = NULL;
  StreamHandlerFunc *tmp;

  if (content_node != NULL)
    {
      desc_node = lm_message_node_get_child (content_node, "description");

      trans_node = lm_message_node_get_child_with_namespace (content_node,
          "transport", NS_GOOGLE_TRANSPORT_P2P);
    }

  for (tmp = func; *tmp != NULL; tmp++)
    {
       /* handlers may create the stream */
       if (stream == NULL && stream_name != NULL)
         stream = _lookup_stream_by_name (session, stream_name);

       if (!(*tmp) (session, message, content_node, stream_name, stream,
             desc_node, trans_node))
         return FALSE;
    }

  return TRUE;
}


static gboolean
_call_handlers_on_streams (GabbleMediaSession *session,
                           LmMessage *message,
                           LmMessageNode *session_node,
                           StreamHandlerFunc *func)
{
  if (lm_message_node_has_namespace (session_node, NS_GOOGLE_SESSION, NULL))
    {
      if (!_call_handlers_on_stream (session, message, session_node,
            GTALK_STREAM_NAME, func))
        return FALSE;
    }
  else
    {
      LmMessageNode *content_node;

      if (session_node->children == NULL)
        return _call_handlers_on_stream (session, message, NULL, NULL, func);

      for (content_node = session_node->children;
           NULL != content_node;
           content_node = content_node->next)
        {
          const gchar *stream_name;

          if (g_strdiff (content_node->name, "content"))
            continue;

          stream_name = lm_message_node_get_attribute (content_node, "name");

          if (stream_name == NULL)
            {
              GMS_DEBUG_WARNING (session, "rejecting content node with no "
                  "name");
              return FALSE;
            }

          if (!_call_handlers_on_stream (session, message, content_node,
                stream_name, func))
            return FALSE;
        }
    }

  return TRUE;
}


void
_gabble_media_session_handle_action (GabbleMediaSession *session,
                                     LmMessage *message,
                                     LmMessageNode *session_node,
                                     const gchar *action)
{
  GabbleMediaSessionPrivate *priv;
  StreamHandlerFunc *funcs = NULL;
  JingleSessionState new_state = JS_STATE_INVALID;
  Handler *i;
  const gchar **tmp;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  GMS_DEBUG_INFO (session, "got jingle session action \"%s\" from peer",
                  action);

  /* do the state machine dance */

  /* search the table of handlers for the action */
  for (i = handlers; NULL != i->actions[0]; i++)
    {
      for (tmp = i->actions; NULL != *tmp; tmp++)
        if (0 == strcmp (*tmp, action))
          break;

      if (NULL == *tmp)
        continue;

      /* if we're outside the allowable states for this action, return an error
       * immediately */
      if (priv->state < i->min_allowed_state ||
          priv->state > i->max_allowed_state)
        {
          GMS_DEBUG_ERROR (session, "action \"%s\" not allowed in current "
              "state; terminating session", action);
          goto ACK_FAILURE;
        }

      funcs = i->stream_handlers;
      new_state = i->new_state;

      break;
    }

  /* pointer is not NULL if we found a matching action */
  if (NULL == funcs)
    {
      GMS_DEBUG_ERROR (session, "received unrecognised action \"%s\"; "
          "terminating session", action);
      goto ACK_FAILURE;
    }

  /* call handlers if there are any (NULL-terminated array) */
  if (NULL != *funcs)
    {
      if (!_call_handlers_on_streams (session, message, session_node, funcs))
        goto FUNC_ERROR;
    }

  /* if the action specified a new state to go to, set it */
  if (JS_STATE_INVALID != new_state)
    g_object_set (session, "state", new_state, NULL);

  _gabble_connection_acknowledge_set_iq (priv->conn, message);

  return;

FUNC_ERROR:
  GMS_DEBUG_ERROR (session, "error encountered with action \"%s\" in current "
      "state; terminating session", action);

ACK_FAILURE:
  _gabble_connection_send_iq_error (priv->conn, message,
                                    XMPP_ERROR_NOT_ALLOWED, NULL);
}

static gboolean
timeout_session (gpointer data)
{
  GabbleMediaSession *session = data;

  DEBUG ("session timed out");

  _gabble_media_session_terminate (session, INITIATOR_LOCAL, TP_CHANNEL_GROUP_CHANGE_REASON_ERROR);

  return FALSE;
}

static void
session_state_changed (GabbleMediaSession *session,
                       JingleSessionState prev_state,
                       JingleSessionState new_state)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  GMS_DEBUG_EVENT (session, "state changed from %s to %s",
                   session_states[prev_state].name,
                   session_states[new_state].name);

  /*
   * If the state goes from CREATED to INITIATED (which means the remote
   * end initiated), set the timer. If, OTOH, we're the end which just sent an
   * initiate, set the timer.
   */
  if ((prev_state == JS_STATE_PENDING_CREATED &&
       new_state == JS_STATE_PENDING_INITIATED) ||
      (new_state == JS_STATE_PENDING_INITIATE_SENT))
    {
      priv->timer_id =
        g_timeout_add (DEFAULT_SESSION_TIMEOUT, timeout_session, session);
    }
  else if (new_state == JS_STATE_ACTIVE)
    {
      g_source_remove (priv->timer_id);
      priv->timer_id = 0;
    }
}

static void
_mark_local_streams_sent_one (const gchar *name,
                              GabbleMediaStream *stream,
                              GabbleMediaSession *session)
{
  JingleInitiator initiator;

  g_object_get (stream, "initiator", &initiator, NULL);

  if (initiator == INITIATOR_REMOTE)
    return;

  GMS_DEBUG_INFO (session, "marking local stream %s as signalled", name);

  g_object_set (stream, "signalling-state", STREAM_SIG_STATE_SENT, NULL);
}

void
_mark_local_streams_sent (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  g_hash_table_foreach (priv->streams, (GHFunc) _mark_local_streams_sent_one,
      session);
}

static void
_set_remote_streams_playing_one (const gchar *name,
                                 GabbleMediaStream *stream,
                                 GabbleMediaSession *session)
{
  JingleInitiator initiator;

  g_object_get (stream, "initiator", &initiator, NULL);

  if (initiator == INITIATOR_LOCAL)
    return;

  GMS_DEBUG_INFO (session, "setting remote stream %s as playing", name);

  g_object_set (stream, "playing", TRUE, NULL);
}

void
_set_remote_streams_playing (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  g_hash_table_foreach (priv->streams,
      (GHFunc) _set_remote_streams_playing_one, session);
}

typedef struct _AddDescriptionsData AddDescriptionsData;

struct _AddDescriptionsData {
  GabbleMediaSession *session;
  LmMessageNode *session_node;
  JingleInitiator initiator;
};

static void
_add_content_descriptions_one (const gchar *name,
                               GabbleMediaStream *stream,
                               AddDescriptionsData *data)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (data->session);
  LmMessageNode *content_node;
  JingleInitiator initiator;

  g_object_get (stream, "initiator", &initiator, NULL);

  if (initiator != data->initiator)
    {
      GMS_DEBUG_INFO (data->session, "not adding content description for %s "
          "stream %s", initiator == INITIATOR_LOCAL ? "local" : "remote",
          name);
      return;
    }

  if (priv->mode == MODE_GOOGLE)
    {
      content_node = data->session_node;
    }
  else
    {
      content_node = lm_message_node_add_child (data->session_node, "content", NULL);
      lm_message_node_set_attribute (content_node, "name", name);
    }

  _gabble_media_stream_content_node_add_description (stream, content_node);

  _gabble_media_stream_content_node_add_transport (stream, content_node);
}

static void
_add_content_descriptions (GabbleMediaSession *session,
                           LmMessageNode *session_node,
                           JingleInitiator stream_initiator)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  AddDescriptionsData data;

  data.session = session;
  data.session_node = session_node;
  data.initiator = stream_initiator;

  g_hash_table_foreach (priv->streams, (GHFunc) _add_content_descriptions_one, &data);
}

static LmHandlerResult
accept_msg_reply_cb (GabbleConnection *conn,
                     LmMessage *sent_msg,
                     LmMessage *reply_msg,
                     GObject *object,
                     gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session, "accept failed");

  g_object_set (session, "state", JS_STATE_ACTIVE, NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
_stream_not_ready_for_accept (const gchar *name,
                              GabbleMediaStream *stream,
                              GabbleMediaSession *session)
{
  TpMediaStreamState connection_state;
  JingleInitiator stream_initiator;
  gboolean got_local_codecs;

  g_object_get (stream,
                "got-local-codecs", &got_local_codecs,
                "connection-state", &connection_state,
                "initiator", &stream_initiator,
                NULL);

  /* locally initiated streams shouldn't delay acceptance */
  if (stream_initiator == INITIATOR_LOCAL)
    return FALSE;

  if (!got_local_codecs)
    {
      GMS_DEBUG_INFO (session, "stream %s does not yet have local codecs",
          name);

      return TRUE;
    }

  if (connection_state != TP_MEDIA_STREAM_STATE_CONNECTED)
    {
      GMS_DEBUG_INFO (session, "stream %s is not yet connected", name);

      return TRUE;
    }

  return FALSE;
}

static void
try_session_accept (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;
  const gchar *action;

  if (priv->state < JS_STATE_ACTIVE && !priv->locally_accepted)
    {
      GMS_DEBUG_INFO (session, "not sending accept yet, waiting for local "
          "user to accept call");
      return;
    }

  if (g_hash_table_find (priv->streams, (GHRFunc) _stream_not_ready_for_accept,
        session) != NULL)
    {
      GMS_DEBUG_INFO (session, "not sending accept yet, found a stream which "
          "was not yet connected or was missing local codecs");
      return;
    }

  if (priv->mode == MODE_GOOGLE)
    action = "accept";
  else
    action = "session-accept";

  /* construct a session acceptance message */
  msg = _gabble_media_session_message_new (session, action, &session_node);

  /* only accept REMOTE streams; any LOCAL streams were added by the local
   * user before accepting and should be signalled after the accept */
  _add_content_descriptions (session, session_node, INITIATOR_REMOTE);

  GMS_DEBUG_INFO (session, "sending jingle session action \"%s\" to peer",
      action);

  /* send the final acceptance message */
  _gabble_connection_send_with_reply (priv->conn, msg, accept_msg_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);

  /* set remote streams playing */
  _set_remote_streams_playing (session);

  g_object_set (session, "state", JS_STATE_PENDING_ACCEPT_SENT, NULL);
}

static LmHandlerResult
content_accept_msg_reply_cb (GabbleConnection *conn,
                             LmMessage *sent_msg,
                             LmMessage *reply_msg,
                             GObject *object,
                             gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (user_data);
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      GPtrArray *streams;

      GMS_DEBUG_ERROR (session, "content-accept failed; removing stream");
      NODE_DEBUG (sent_msg->node, "message sent");
      NODE_DEBUG (reply_msg->node, "message reply");

      streams = g_ptr_array_sized_new (1);
      g_ptr_array_add (streams, stream);
      _gabble_media_session_remove_streams (session, streams);
      g_ptr_array_free (streams, TRUE);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
try_content_accept (GabbleMediaSession *session,
                    GabbleMediaStream *stream)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  gchar *name;
  LmMessage *msg;
  LmMessageNode *session_node;
  AddDescriptionsData data;

  g_assert (priv->state == JS_STATE_ACTIVE);
  g_assert (priv->mode == MODE_JINGLE);

  g_object_get (stream, "name", &name, NULL);

  if (_stream_not_ready_for_accept (name, stream, session))
    {
      GMS_DEBUG_INFO (session, "not sending content-accept yet, stream %s "
          "is disconnected or missing local codecs", name);
      g_free (name);
      return;
    }

  /* send a content acceptance message */
  msg = _gabble_media_session_message_new (session, "content-accept",
      &session_node);

  data.session = session;
  data.session_node = session_node;
  data.initiator = INITIATOR_REMOTE;

  _add_content_descriptions_one (name, stream, &data);

  GMS_DEBUG_INFO (session, "sending jingle session action \"content-accept\" "
      "to peer for stream %s", name);

  g_free (name);

  _gabble_connection_send_with_reply (priv->conn, msg,
      content_accept_msg_reply_cb, G_OBJECT (stream), session, NULL);

  lm_message_unref (msg);

  /* set stream playing */
  g_object_set (stream, "playing", TRUE, NULL);
}

static LmHandlerResult
initiate_msg_reply_cb (GabbleConnection *conn,
                       LmMessage *sent_msg,
                       LmMessage *reply_msg,
                       GObject *object,
                       gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session, "initiate failed");

  g_object_set (session, "state", JS_STATE_PENDING_INITIATED, NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
_stream_not_ready_for_initiate (const gchar *name,
                                GabbleMediaStream *stream,
                                GabbleMediaSession *session)
{
  gboolean got_local_codecs;

  g_object_get (stream,
                "got-local-codecs", &got_local_codecs,
                NULL);

  if (!got_local_codecs)
    {
      GMS_DEBUG_INFO (session, "stream %s does not yet have local codecs",
          name);

      return TRUE;
    }

  return FALSE;
}

static void
try_session_initiate (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;
  const gchar *action;

  if (g_hash_table_find (priv->streams,
        (GHRFunc) _stream_not_ready_for_initiate, session) != NULL)
    {
      GMS_DEBUG_INFO (session, "not sending initiate yet, found a stream "
          "which was missing local codecs");
      return;
    }

  if (priv->mode == MODE_GOOGLE)
      action = "initiate";
  else
      action = "session-initiate";

  msg = _gabble_media_session_message_new (session, action, &session_node);

  _add_content_descriptions (session, session_node, INITIATOR_LOCAL);

  GMS_DEBUG_INFO (session, "sending jingle action \"%s\" to peer", action);

  _gabble_connection_send_with_reply (priv->conn, msg, initiate_msg_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);

  /* mark local streams as sent (so that eg candidates will be sent) */
  _mark_local_streams_sent (session);

  g_object_set (session, "state", JS_STATE_PENDING_INITIATE_SENT, NULL);
}

static LmHandlerResult
content_add_msg_reply_cb (GabbleConnection *conn,
                          LmMessage *sent_msg,
                          LmMessage *reply_msg,
                          GObject *object,
                          gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (user_data);
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      GPtrArray *streams;

      GMS_DEBUG_ERROR (session, "content-add failed; removing stream");
      NODE_DEBUG (sent_msg->node, "message sent");
      NODE_DEBUG (reply_msg->node, "message reply");

      streams = g_ptr_array_sized_new (1);
      g_ptr_array_add (streams, stream);
      _gabble_media_session_remove_streams (session, streams);
      g_ptr_array_free (streams, TRUE);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
do_content_add (GabbleMediaSession *session,
                GabbleMediaStream *stream)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  gchar *name;
  LmMessage *msg;
  LmMessageNode *session_node;
  AddDescriptionsData data;

  g_assert (priv->state == JS_STATE_ACTIVE);
  g_assert (priv->mode == MODE_JINGLE);

  g_object_get (stream, "name", &name, NULL);

  if (_stream_not_ready_for_initiate (name, stream, session))
    {
      GMS_DEBUG_ERROR (session, "trying to send content-add for stream %s "
          "but we have no local codecs. what?!", name);
      g_free (name);
      g_assert_not_reached ();
      return;
    }

  msg = _gabble_media_session_message_new (session, "content-add",
      &session_node);

  data.session = session;
  data.session_node = session_node;
  data.initiator = INITIATOR_LOCAL;

  _add_content_descriptions_one (name, stream, &data);

  GMS_DEBUG_INFO (session, "sending jingle action \"content-add\" to peer for "
      "stream %s", name);

  g_free (name);

  _gabble_connection_send_with_reply (priv->conn, msg,
      content_add_msg_reply_cb, G_OBJECT (stream), session, NULL);

  lm_message_unref (msg);

  /* mark stream as sent */
  g_object_set (stream, "signalling-state", STREAM_SIG_STATE_SENT, NULL);
}

static void
stream_close_cb (GabbleMediaStream *stream,
                 GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;
  gchar *name;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  g_object_get (stream, "name", &name, NULL);

  g_hash_table_remove (priv->streams, name);

  g_free (name);
}

static void
stream_connection_state_changed_cb (GabbleMediaStream *stream,
                                    GParamSpec *param,
                                    GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;
  TpMediaStreamState connection_state;
  JingleInitiator stream_initiator;
  gchar *name;
  gboolean playing;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  g_object_get (stream,
                "connection-state", &connection_state,
                "initiator", &stream_initiator,
                "name", &name,
                "playing", &playing,
                NULL);

  if (connection_state != TP_MEDIA_STREAM_STATE_CONNECTED)
    return;

  GMS_DEBUG_INFO (session, "stream %s has gone connected", name);
  g_free (name);

  if (playing)
    {
      GMS_DEBUG_INFO (session, "doing nothing, stream is already playing");
      return;
    }

  /* after session is active, we do things per-stream with content-* actions */
  if (priv->state < JS_STATE_ACTIVE)
    {
      /* send a session accept if the session was initiated by the peer */
      if (priv->initiator == INITIATOR_REMOTE)
        {
          try_session_accept (session);
        }
      else
        {
          GMS_DEBUG_INFO (session, "session initiated by us, so we're not "
              "going to consider sending an accept");
        }
    }
  else
    {
      /* send a content accept if the stream was added by the peer */
      if (stream_initiator == INITIATOR_REMOTE)
        {
          try_content_accept (session, stream);
        }
      else
        {
          GMS_DEBUG_INFO (session, "stream added by us, so we're not going "
              "to send an accept");
        }
    }
}

static void
stream_got_local_codecs_changed_cb (GabbleMediaStream *stream,
                                    GParamSpec *param,
                                    GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;
  gboolean got_local_codecs, playing;
  JingleInitiator stream_initiator;
  gchar *name;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  g_object_get (stream,
                "got-local-codecs", &got_local_codecs,
                "initiator", &stream_initiator,
                "name", &name,
                "playing", &playing,
                NULL);

  if (!got_local_codecs)
    return;

  GMS_DEBUG_INFO (session, "stream %s has got local codecs", name);
  g_free (name);

  if (playing)
    {
      GMS_DEBUG_ERROR (session, "stream was already playing and we got local "
          "codecs. what?!");
      g_assert_not_reached ();
      return;
    }

  /* after session is active, we do things per-stream with content-* actions */
  if (priv->state < JS_STATE_ACTIVE)
    {
      if (priv->initiator == INITIATOR_REMOTE)
        {
          if (priv->state < JS_STATE_PENDING_ACCEPT_SENT)
            {
              try_session_accept (session);
            }
          else
            {
              GMS_DEBUG_INFO (session, "stream added after sending accept; "
                  "not doing content-add until remote end acknowledges");
            }
        }
      else
        {
          if (priv->state < JS_STATE_PENDING_INITIATE_SENT)
            {
              try_session_initiate (session);
            }
          else
            {
              GMS_DEBUG_INFO (session, "stream added after sending initiate; "
                  "not doing content-add until remote end accepts");
            }
        }
    }
  else
    {
      if (stream_initiator == INITIATOR_REMOTE)
        {
          try_content_accept (session, stream);
        }
      else
        {
          do_content_add (session, stream);
        }
    }
}

static gchar *
get_jid_for_contact (GabbleMediaSession *session,
                     GabbleHandle handle)
{
  GabbleMediaSessionPrivate *priv;
  const gchar *base_jid;
  GabbleHandle self;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  self = priv->conn->self_handle;

  base_jid = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (base_jid != NULL);

  if (handle == self)
    {
      gchar *resource, *ret;
      g_object_get (priv->conn, "resource", &resource, NULL);
      g_assert (resource != NULL);
      ret = g_strdup_printf ("%s/%s", base_jid, resource);
      g_free (resource);
      return ret;
    }
  else
    {
      g_assert (priv->peer_resource != NULL);
      return g_strdup_printf ("%s/%s", base_jid, priv->peer_resource);
    }
}

LmMessage *
_gabble_media_session_message_new (GabbleMediaSession *session,
                                   const gchar *action,
                                   LmMessageNode **session_node)
{
  GabbleMediaSessionPrivate *priv;
  LmMessage *msg;
  LmMessageNode *iq_node, *node;
  gchar *peer_jid, *initiator_jid;
  GabbleHandle initiator_handle;
  const gchar *element, *xmlns;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  peer_jid = get_jid_for_contact (session, priv->peer);

  msg = lm_message_new_with_sub_type (
      peer_jid,
      LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_SET);

  g_free (peer_jid);

  iq_node = lm_message_get_node (msg);

  if (priv->mode == MODE_GOOGLE)
    element = "session";
  else
    element = "jingle";

  if (priv->initiator == INITIATOR_LOCAL)
    initiator_handle = GABBLE_GROUP_MIXIN (session)->self_handle;
  else
    initiator_handle = priv->peer;

  node = lm_message_node_add_child (iq_node, element, NULL);
  initiator_jid = get_jid_for_contact (session, initiator_handle);

  lm_message_node_set_attributes (node,
      (priv->mode == MODE_GOOGLE) ? "id" : "sid", priv->id,
      (priv->mode == MODE_GOOGLE) ? "type" : "action", action,
      "initiator", initiator_jid,
      NULL);

  if (priv->mode == MODE_GOOGLE)
    xmlns = NS_GOOGLE_SESSION;
  else
    xmlns = NS_JINGLE;

  lm_message_node_set_attribute (node, "xmlns", xmlns);
  g_free (initiator_jid);

  if (session_node)
    *session_node = node;

  return msg;
}

static void
_accept_local_pending_send (const gchar *name,
                            GabbleMediaStream *stream,
                            GabbleMediaSession *session)
{
  CombinedStreamDirection combined_dir;
  TpMediaStreamDirection current_dir;
  TpMediaStreamPendingSend pending_send;

  g_object_get (stream, "combined-direction", &combined_dir, NULL);

  current_dir = COMBINED_DIRECTION_GET_DIRECTION (combined_dir);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND (combined_dir);

  if ((pending_send & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
    {
      GMS_DEBUG_INFO (session, "accepting pending local send on stream %s",
          name);

      current_dir |= TP_MEDIA_STREAM_DIRECTION_SEND;
      pending_send &= ~TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
      combined_dir = MAKE_COMBINED_DIRECTION (current_dir, pending_send);
      g_object_set (stream, "combined-direction", combined_dir, NULL);
    }
}

void
_gabble_media_session_accept (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  priv->locally_accepted = TRUE;

  g_hash_table_foreach (priv->streams, (GHFunc) _accept_local_pending_send,
      session);

  try_session_accept (session);
}

/* for when you want the reply to be removed from
 * the handler chain, but don't care what it is */
static LmHandlerResult
ignore_reply_cb (GabbleConnection *conn,
                 LmMessage *sent_msg,
                 LmMessage *reply_msg,
                 GObject *object,
                 gpointer user_data)
{
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

void
_gabble_media_session_remove_streams (GabbleMediaSession *session,
                                      const GPtrArray *streams)
{
  GabbleMediaSessionPrivate *priv;
  LmMessage *msg;
  LmMessageNode *session_node;
  guint i;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  /* end the session if there'd be no streams left after reducing it */
  if (streams->len == g_hash_table_size (priv->streams))
    {
      _gabble_media_session_terminate (session, INITIATOR_LOCAL, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
      return;
    }

  /* construct a remove message if we're in a state greater than CREATED (ie
   * something has been sent/received about this session) */
  if (priv->state > JS_STATE_PENDING_CREATED)
    {
      msg = _gabble_media_session_message_new (session, "content-remove",
                                               &session_node);

      GMS_DEBUG_INFO (session, "sending jingle session action "
          "\"content-remove\" to peer");
    }
  else
    {
      msg = NULL;

      GMS_DEBUG_INFO (session, "not sending jingle session action "
          "\"content-remove\" to peer, no initiates have been sent");
    }

  /* right, remove it */
  for (i = 0; i < streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (streams, i);

      if (msg != NULL)
        {
          gchar *name;
          LmMessageNode *content_node;

          g_object_get (stream, "name", &name, NULL);

          content_node = lm_message_node_add_child (session_node, "content",
              NULL);
          lm_message_node_set_attribute (content_node, "name", name);

          g_free (name);
        }

      /* close the stream */
      _gabble_media_stream_close (stream);
    }

  /* send the remove message */
  if (msg != NULL)
    {
      _gabble_connection_send_with_reply (priv->conn, msg, ignore_reply_cb,
                                          G_OBJECT (session), NULL, NULL);

      lm_message_unref (msg);
    }
}

static void
send_reject_message (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;

  /* this should only happen in google mode, and we should only arrive in that
   * mode when we've ended up talking to a resource that doesn't support
   * jingle */
  g_assert (priv->mode == MODE_GOOGLE);
  g_assert (priv->peer_resource != NULL);

  /* construct a session terminate message */
  msg = _gabble_media_session_message_new (session, "reject", &session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"reject\" to peer");

  /* send it */
  _gabble_connection_send_with_reply (priv->conn, msg, ignore_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);
}

static void
send_terminate_message (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  const gchar *action;
  LmMessage *msg;
  LmMessageNode *session_node;

  /* construct a session terminate message */
  if (priv->mode == MODE_GOOGLE)
    action = "terminate";
  else
    action = "session-terminate";

  msg = _gabble_media_session_message_new (session, action, &session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"%s\" to peer", action);

  /* send it */
  _gabble_connection_send_with_reply (priv->conn, msg, ignore_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);
}

static void
_close_one_stream (const gchar *name,
                   GabbleMediaStream *stream,
                   GabbleMediaSession *session)
{
  _gabble_media_stream_close (stream);
}

void
_gabble_media_session_terminate (GabbleMediaSession *session,
                                 JingleInitiator who,
                                 TpChannelGroupChangeReason why)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  GabbleHandle actor;

  if (priv->state == JS_STATE_ENDED)
    return;

  if (who == INITIATOR_REMOTE)
    {
      actor = priv->peer;
    }
  else
    {
      actor = priv->conn->self_handle;

      /* Need to tell them that it's all over. */

      /* Jingle doesn't have a "reject" action; a termination before an
       * acceptance indicates that the call has been declined */

      if (priv->initiator == INITIATOR_REMOTE &&
          priv->state == JS_STATE_PENDING_INITIATED &&
          priv->mode == MODE_GOOGLE)
        {
          send_reject_message (session);
        }

      /* if we're still in CREATED, then we've not sent or received any
       * messages about this session yet, so no terminate is necessary */
      else if (priv->state > JS_STATE_PENDING_CREATED)
        {
          send_terminate_message (session);
        }

      g_hash_table_foreach (priv->streams, (GHFunc) _close_one_stream, session);
    }

  priv->terminated = TRUE;
  g_object_set (session, "state", JS_STATE_ENDED, NULL);
  g_signal_emit (session, signals[TERMINATED], 0, actor, why);
}

#if _GMS_DEBUG_LEVEL
void
_gabble_media_session_debug (GabbleMediaSession *session,
                             DebugMessageType type,
                             const gchar *format, ...)
{
  if (DEBUGGING)
    {
      va_list list;
      gchar buf[512];
      GabbleMediaSessionPrivate *priv;
      time_t curtime;
      struct tm *loctime;
      gchar stamp[10];
      const gchar *type_str;

      g_assert (GABBLE_IS_MEDIA_SESSION (session));

      priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

      curtime = time (NULL);
      loctime = localtime (&curtime);

      strftime (stamp, sizeof (stamp), "%T", loctime);

      va_start (list, format);

      vsnprintf (buf, sizeof (buf), format, list);

      va_end (list);

      switch (type) {
        case DEBUG_MSG_INFO:
          type_str = ANSI_BOLD_ON ANSI_FG_WHITE;
          break;
        case DEBUG_MSG_DUMP:
          type_str = ANSI_BOLD_ON ANSI_FG_GREEN;
          break;
        case DEBUG_MSG_WARNING:
          type_str = ANSI_BOLD_ON ANSI_FG_YELLOW;
          break;
        case DEBUG_MSG_ERROR:
          type_str = ANSI_BOLD_ON ANSI_FG_WHITE ANSI_BG_RED;
          break;
        case DEBUG_MSG_EVENT:
          type_str = ANSI_BOLD_ON ANSI_FG_CYAN;
          break;
        default:
          g_assert_not_reached ();
          return;
      }

      printf ("[%s%s%s] %s%-26s%s %s%s%s\n",
          ANSI_BOLD_ON ANSI_FG_WHITE,
          stamp,
          ANSI_RESET,
          session_states[priv->state].attributes,
          session_states[priv->state].name,
          ANSI_RESET,
          type_str,
          buf,
          ANSI_RESET);

      fflush (stdout);
    }
}

#endif /* _GMS_DEBUG_LEVEL */

static const gchar *
_name_stream (GabbleMediaSession *session,
              TpMediaStreamType media_type)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  static gchar ret[MAX_STREAM_NAME_LEN] = GTALK_STREAM_NAME;

  if (priv->mode != MODE_GOOGLE)
    {
      guint i = 1;

      do {
          g_snprintf (ret, MAX_STREAM_NAME_LEN, "%s%u",
              media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
              i++);

          if (g_hash_table_lookup (priv->streams, ret) != NULL)
            {
              ret[0] = '\0';
            }
      } while (ret[0] == '\0');
    }

  return ret;
}


gboolean
_gabble_media_session_request_streams (GabbleMediaSession *session,
                                       const GArray *media_types,
                                       GPtrArray **ret,
                                       GError **error)
{
  static GabblePresenceCapabilities google_audio_caps =
    PRESENCE_CAP_GOOGLE_VOICE;
  static GabblePresenceCapabilities jingle_audio_caps =
    PRESENCE_CAP_JINGLE | PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO |
    PRESENCE_CAP_GOOGLE_TRANSPORT_P2P;
  static GabblePresenceCapabilities jingle_video_caps =
    PRESENCE_CAP_JINGLE | PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO |
    PRESENCE_CAP_GOOGLE_TRANSPORT_P2P;

  GabbleMediaSessionPrivate *priv;
  GabblePresence *presence;
  gboolean want_audio, want_video;
  GabblePresenceCapabilities jingle_desired_caps;
  guint idx;
  gchar *dump;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->peer);

  if (presence == NULL)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "member has no "
          "audio/video capabilities");

      return FALSE;
    }

  dump = gabble_presence_dump (presence);
  GMS_DEBUG_DUMP (session, "presence for peer %d:\n%s", priv->peer, dump);
  g_free (dump);

  want_audio = want_video = FALSE;

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);

      if (media_type == TP_MEDIA_STREAM_TYPE_AUDIO)
        {
          want_audio = TRUE;
        }
      else if (media_type == TP_MEDIA_STREAM_TYPE_VIDEO)
        {
          want_video = TRUE;
        }
      else
        {
          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, "given "
            "media type %u is invalid", media_type);
          return FALSE;
        }
    }

  /* work out what we'd need to do these streams with jingle */
  jingle_desired_caps = 0;

  if (want_audio)
    jingle_desired_caps |= jingle_audio_caps;

  if (want_video)
    jingle_desired_caps |= jingle_video_caps;

  GMS_DEBUG_INFO (session, "want audio: %s; want video: %s",
    want_audio ? "yes" : "no", want_video ? "yes" : "no");

  /* existing call; the recipient and the mode has already been decided */
  if (priv->peer_resource)
    {
      /* is a google call... we have no other option */
      if (priv->mode == MODE_GOOGLE)
        {
          GMS_DEBUG_INFO (session, "already in Google mode; can't add new stream");

          g_assert (g_hash_table_size (priv->streams) == 1);

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "google talk "
              "calls may only contain one stream");

          return FALSE;
        }

      if (!gabble_presence_resource_has_caps (presence, priv->peer_resource,
            jingle_desired_caps))
        {
          GMS_DEBUG_INFO (session,
            "in Jingle mode but have insufficient caps for requested streams");

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "existing "
              "call member doesn't support all requested media types");

          return FALSE;
        }

      GMS_DEBUG_INFO (session,
        "in Jingle mode, and have necessary caps");
    }

  /* no existing call; we should choose a recipient and a mode */
  else
    {
      const gchar *resource;

      g_assert (g_hash_table_size (priv->streams) == 0);

      /* see if we have a fully-capable jingle resource; regardless of the
       * desired media type it's best if we can add/remove the others later */
      resource = gabble_presence_pick_resource_by_caps (presence,
          jingle_audio_caps | jingle_video_caps);

      if (resource == NULL)
        {
          GMS_DEBUG_INFO (session, "contact is not fully jingle-capable");

          /* ok, no problem. see if we can do just what's wanted with jingle */
          resource = gabble_presence_pick_resource_by_caps (presence,
              jingle_desired_caps);

          if (resource == NULL && want_audio && !want_video)
            {
              GMS_DEBUG_INFO (session,
                "contact doesn't have desired Jingle capabilities");

              /* last ditch... if we want only audio and not video, we can make
               * do with google talk */
              resource = gabble_presence_pick_resource_by_caps (presence,
                  google_audio_caps);

              if (resource != NULL)
                {
                  /* only one stream possible with google */
                  if (media_types->len == 1)
                    {
                      GMS_DEBUG_INFO (session,
                        "contact has no Jingle capabilities; "
                        "falling back to Google audio call");
                      priv->mode = MODE_GOOGLE;
                    }
                  else
                    {
                      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                          "Google Talk calls may only contain one stream");

                      return FALSE;
                    }
                }
              else
                {
                  GMS_DEBUG_INFO (session,
                    "contact doesn't have desired Google capabilities");
                }
            }
        }

      if (resource == NULL)
        {
          GMS_DEBUG_INFO (session,
            "contact doesn't have a resource with suitable capabilities");

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "member does "
              "not have the desired audio/video capabilities");

          return FALSE;
        }

      priv->peer_resource = g_strdup (resource);
    }

  /* check it's not a ridiculous number of streams */
  if ((g_hash_table_size (priv->streams) + media_types->len) > MAX_STREAMS)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "I think that's "
          "quite enough streams already");
      return FALSE;
    }

  /* if we've got here, we're good to make the streams */

  *ret = g_ptr_array_sized_new (media_types->len);

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);
      GabbleMediaStream *stream;
      const gchar *stream_name;

      if (priv->mode == MODE_GOOGLE)
        stream_name = GTALK_STREAM_NAME;
      else
        stream_name = _name_stream (session, media_type);

      stream = create_media_stream (session, stream_name, INITIATOR_LOCAL,
                                    media_type);

      g_ptr_array_add (*ret, stream);
    }

  return TRUE;
}

static const gchar*
_direction_to_senders (GabbleMediaSession *session,
                       TpMediaStreamDirection dir)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  const gchar *ret = NULL;

  switch (dir)
    {
      case TP_MEDIA_STREAM_DIRECTION_NONE:
        g_assert_not_reached ();
        break;
      case TP_MEDIA_STREAM_DIRECTION_SEND:
        if (priv->initiator == INITIATOR_LOCAL)
          ret = "initiator";
        else
          ret = "responder";
        break;
      case TP_MEDIA_STREAM_DIRECTION_RECEIVE:
        if (priv->initiator == INITIATOR_REMOTE)
          ret = "initiator";
        else
          ret = "responder";
        break;
      case TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL:
        ret = "both";
        break;
    }

  g_assert (ret != NULL);

  return ret;
}

static LmHandlerResult
direction_msg_reply_cb (GabbleConnection *conn,
                        LmMessage *sent_msg,
                        LmMessage *reply_msg,
                        GObject *object,
                        gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session, "direction change failed");

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
send_direction_change (GabbleMediaSession *session,
                       GabbleMediaStream *stream,
                       TpMediaStreamDirection dir,
                       GError **error)
{
  GabbleMediaSessionPrivate *priv;
  const gchar *senders;
  gchar *name;
  LmMessage *msg;
  LmMessageNode *session_node, *content_node;
  gboolean ret;

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  senders = _direction_to_senders (session, dir);
  g_object_get (stream, "name", &name, NULL);

  GMS_DEBUG_INFO (session, "sending jingle session action \"content-modify\" "
      "to peer for stream %s (senders=%s)", name, senders);

  msg = _gabble_media_session_message_new (session, "content-modify",
      &session_node);
  content_node = lm_message_node_add_child (session_node, "content", NULL);

  lm_message_node_set_attributes (content_node,
      "name", name,
      "senders", senders,
      NULL);

  g_free (name);

  ret = _gabble_connection_send_with_reply (priv->conn, msg,
      direction_msg_reply_cb, G_OBJECT (session), NULL, error);

  lm_message_unref (msg);

  return ret;
}

gboolean
_gabble_media_session_request_stream_direction (GabbleMediaSession *session,
                                                GabbleMediaStream *stream,
                                                TpMediaStreamDirection requested_dir,
                                                GError **error)
{
  GabbleMediaSessionPrivate *priv;
  CombinedStreamDirection combined_dir, new_combined_dir;
  TpMediaStreamDirection current_dir; //, new_dir;
  TpMediaStreamPendingSend pending_send;

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  g_object_get (stream, "combined-direction", &combined_dir, NULL);

  current_dir = COMBINED_DIRECTION_GET_DIRECTION (combined_dir);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND (combined_dir);

  if (priv->mode == MODE_GOOGLE)
    {
      g_assert (current_dir == TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

      if (requested_dir == TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)
        return TRUE;

      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "google talk "
          "calls can only be bi-directional");
      return FALSE;
    }

  if (requested_dir == TP_MEDIA_STREAM_DIRECTION_NONE)
    {
      GPtrArray *streams;

      GMS_DEBUG_INFO (session, "request for NONE direction; removing stream");

      streams = g_ptr_array_sized_new (1);
      g_ptr_array_add (streams, stream);
      _gabble_media_session_remove_streams (session, streams);
      g_ptr_array_free (streams, TRUE);

      return TRUE;
    }

  /* if we're awaiting a local decision on sending... */
  if ((pending_send & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
    {
      /* clear the flag */
      pending_send &= ~TP_MEDIA_STREAM_PENDING_LOCAL_SEND;

      /* make our current_dir match what other end thinks (he thinks we're
       * bidirectional) so that we send the correct transitions */
      current_dir ^= TP_MEDIA_STREAM_DIRECTION_SEND;
    }

#if 0
  /* if we're asking the remote end to start sending, set the pending flag and
   * don't change our directionality just yet */
  new_dir = requested_dir;
  if (((current_dir & TP_MEDIA_STREAM_DIRECTION_RECEIVE) == 0) &&
      ((new_dir & TP_MEDIA_STREAM_DIRECTION_RECEIVE) != 0))
    {
      pending_send ^= TP_MEDIA_STREAM_PENDING_REMOTE_SEND;
      new_dir &= ~TP_MEDIA_STREAM_DIRECTION_RECEIVE;
    }
#endif

  /* make any necessary changes */
  new_combined_dir = MAKE_COMBINED_DIRECTION (requested_dir, pending_send);
  if (new_combined_dir != combined_dir)
    g_object_set (stream, "combined-direction", new_combined_dir, NULL);

  /* short-circuit sending a request if we're not asking for anything new */
  if (current_dir == requested_dir)
    return TRUE;

  /* send request */
  return send_direction_change (session, stream, requested_dir, error);
}

