#ifndef __TF_CHANNEL_H__
#define __TF_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/channel.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define TF_TYPE_CHANNEL tf_channel_get_type()

#define TF_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  TF_TYPE_CHANNEL, TfChannel))

#define TF_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  TF_TYPE_CHANNEL, TfChannelClass))

#define TF_IS_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TF_TYPE_CHANNEL))

#define TF_IS_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TF_TYPE_CHANNEL))

#define TF_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  TF_TYPE_CHANNEL, TfChannelClass))

typedef struct _TfChannelPrivate TfChannelPrivate;

/**
 * TfChannel:
 *
 * All members of the object are private
 */

typedef struct _TfChannel TfChannel;

/**
 * TfChannelClass:
 * @parent_class: the parent #GObjectClass
 *
 * There are no overridable functions
 */

typedef struct _TfChannelClass TfChannelClass;

GType tf_channel_get_type (void);

void tf_channel_new_async (TpChannel *channel_proxy,
    GAsyncReadyCallback callback,
    gpointer user_data);


void tf_channel_error (TfChannel *chan,
  TpMediaStreamError error,
  const gchar *message);

gboolean tf_channel_bus_message (TfChannel *channel,
    GstMessage *message);

G_END_DECLS

#endif /* __TF_CHANNEL_H__ */
