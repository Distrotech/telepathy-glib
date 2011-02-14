/*
 * call-handler.c
 * Copyright (C) 2011 Collabora Ltd.
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

#include <gst/gst.h>
#include <telepathy-glib/telepathy-glib.h>
#include <extensions/extensions.h>
#include <gst/farsight/fs-element-added-notifier.h>
#include <telepathy-farstream/telepathy-farstream.h>

typedef struct {
  GstElement *pipeline;
  guint buswatch;
  TpChannel *proxy;
  TfChannel *channel;
  GList *notifiers;
} ChannelContext;

GMainLoop *loop;

static gboolean
bus_watch_cb (GstBus *bus,
    GstMessage *message,
    gpointer user_data)
{
  ChannelContext *context = user_data;

  if (context->channel != NULL)
    tf_channel_bus_message (context->channel, message);

  return TRUE;
}

static void
src_pad_added_cb (TfContent *content,
    TpHandle handle,
    FsStream *stream,
    GstPad *pad,
    FsCodec *codec,
    gpointer user_data)
{
  ChannelContext *context = user_data;
  gchar *cstr = fs_codec_to_string (codec);
  FsMediaType mtype;
  GstPad *sinkpad;
  GstElement *element;

  g_debug ("New src pad: %s", cstr);
  g_object_get (content, "media-type", &mtype, NULL);

  switch (mtype)
    {
      case FS_MEDIA_TYPE_AUDIO:
        element = gst_parse_bin_from_description (
          "audioconvert ! audioresample ! audioconvert ! autoaudiosink",
            TRUE, NULL);
        break;
      case FS_MEDIA_TYPE_VIDEO:
        element = gst_parse_bin_from_description (
          "ffmpegcolorspace ! videoscale ! autovideosink",
          TRUE, NULL);
        break;
      default:
        g_warning ("Unknown media type");
        return;
    }

  gst_bin_add (GST_BIN (context->pipeline), element);
  sinkpad = gst_element_get_pad (element, "sink");
  gst_element_set_state (element, GST_STATE_PLAYING);
  gst_pad_link (pad, sinkpad);

  g_object_unref (sinkpad);
}


static void
content_added_cb (TfChannel *channel,
    TfContent *content,
    gpointer user_data)
{
  GstPad *srcpad, *sinkpad;
  FsMediaType mtype;
  GstElement *element;
  GList *codecs;
  ChannelContext *context = user_data;

  g_debug ("Content added");

  codecs = fs_codec_list_from_keyfile ("codec-preferences", NULL);

  tf_content_set_codec_preferences (content, codecs, NULL);

  g_object_get (content,
    "sink-pad", &sinkpad,
    "media-type", &mtype,
    NULL);

  switch (mtype)
    {
      case FS_MEDIA_TYPE_AUDIO:
        element = gst_parse_bin_from_description (
          "audiotestsrc is-live=1 ! audio/x-raw-int,rate=8000 ! queue",
            TRUE, NULL);
        break;
      case FS_MEDIA_TYPE_VIDEO:
        element = gst_parse_bin_from_description (
          "videotestsrc is-live=1 ! " \
            "video/x-raw-yuv,width=640, height=480 ! queue",
          TRUE, NULL);
        break;
      default:
        g_warning ("Unknown media type");
        goto out;
    }

  g_signal_connect (content, "src-pad-added",
    G_CALLBACK (src_pad_added_cb), context);

  gst_bin_add (GST_BIN (context->pipeline), element);
  srcpad = gst_element_get_pad (element, "src");
  gst_pad_link (srcpad, sinkpad);

  gst_element_set_state (element, GST_STATE_PLAYING);

  g_object_unref (srcpad);
out:
  g_object_unref (sinkpad);
}

static void
conference_added_cb (TfChannel *channel,
  GstElement *conference,
  gpointer user_data)
{
  ChannelContext *context = user_data;
  FsElementAddedNotifier *notifier;

  g_debug ("Conference added");

  /* Add notifier to set the various element properties as needed */
  notifier = fs_element_added_notifier_new ();
  fs_element_added_notifier_set_properties_from_keyfile (notifier,
    fs_utils_get_default_element_properties (conference));
  fs_element_added_notifier_add (notifier, GST_BIN (context->pipeline));

  context->notifiers = g_list_prepend (context->notifiers, notifier);

  gst_bin_add (GST_BIN (context->pipeline), conference);
  gst_element_set_state (conference, GST_STATE_PLAYING);
}

static gboolean
dump_pipeline_cb (gpointer data)
{
  ChannelContext *context = data;

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (context->pipeline),
    GST_DEBUG_GRAPH_SHOW_ALL,
    "call-handler");

  return TRUE;
}

static void
new_tf_channel_cb (GObject *source,
  GAsyncResult *result,
  gpointer user_data)
{
  ChannelContext *context = user_data;

  g_debug ("New TfChannel");

  context->channel = TF_CHANNEL (g_async_initable_new_finish (
      G_ASYNC_INITABLE (source), result, NULL));


  if (context->channel == NULL)
    {
      g_warning ("Failed to create channel");
      return;
    }

  g_debug ("Adding timeout");
  g_timeout_add_seconds (5, dump_pipeline_cb, context);

  g_signal_connect (context->channel, "fs-conference-added",
    G_CALLBACK (conference_added_cb), context);

  g_signal_connect (context->channel, "content-added",
    G_CALLBACK (content_added_cb), context);
}

static void
proxy_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    gpointer user_data)
{
  ChannelContext *context = user_data;

  g_debug ("Channel closed");
  if (context->pipeline != NULL)
    {
      gst_element_set_state (context->pipeline, GST_STATE_NULL);
      g_object_unref (context->pipeline);
    }

  if (context->channel != NULL)
    g_object_unref (context->channel);

  g_list_free_full (context->notifiers, g_object_unref);

  g_object_unref (context->proxy);

  g_slice_free (ChannelContext, context);

  g_main_loop_quit (loop);
}

static void
new_call_channel_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *handler_context,
    gpointer user_data)
{
  ChannelContext *context = g_slice_new0 (ChannelContext);
  TpChannel *proxy;

  g_debug ("New channel");

  proxy = channels->data;

  context->pipeline = gst_pipeline_new (NULL);
  context->buswatch = gst_bus_add_watch (
    gst_pipeline_get_bus (GST_PIPELINE (context->pipeline)),
    bus_watch_cb,
    context);

  gst_element_set_state (context->pipeline, GST_STATE_PLAYING);

  tf_channel_new_async (proxy, new_tf_channel_cb, context);

  tp_handle_channels_context_accept (handler_context);

  tf_future_cli_channel_type_call_call_accept (proxy, -1,
      NULL, NULL, NULL, NULL);

  context->proxy = g_object_ref (proxy);
  g_signal_connect (proxy, "invalidated",
    G_CALLBACK (proxy_invalidated_cb),
    context);
}

int
main (int argc, char **argv)
{
  TpBaseClient *client;
  TpDBusDaemon *bus;

  g_type_init ();
  tf_init ();
  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  bus = tp_dbus_daemon_dup (NULL);

  client = tp_simple_handler_new (bus,
    FALSE,
    FALSE,
    "TpFsCallHandlerDemo",
    TRUE,
    new_call_channel_cb,
    NULL,
    NULL);

  tp_base_client_take_handler_filter (client,
    tp_asv_new (
       TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TF_FUTURE_IFACE_CHANNEL_TYPE_CALL,
       TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        TF_FUTURE_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, G_TYPE_BOOLEAN,
          TRUE,
       NULL));

  tp_base_client_take_handler_filter (client,
    tp_asv_new (
       TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TF_FUTURE_IFACE_CHANNEL_TYPE_CALL,
       TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        TF_FUTURE_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, G_TYPE_BOOLEAN,
          TRUE,
       NULL));

  tp_base_client_register (client, NULL);

  g_main_loop_run (loop);

  g_object_unref (bus);
  g_object_unref (client);
  g_main_loop_unref (loop);

  return 0;
}
