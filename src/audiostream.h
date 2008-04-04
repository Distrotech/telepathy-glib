#ifndef __TP_STREAM_ENGINE_AUDIO_STREAM_H__
#define __TP_STREAM_ENGINE_AUDIO_STREAM_H__

#include <glib-object.h>
#include <farsight/farsight-session.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/media-interfaces.h>

#include "stream.h"

G_BEGIN_DECLS

#define TP_STREAM_ENGINE_TYPE_AUDIO_STREAM tp_stream_engine_audio_stream_get_type()

#define TP_STREAM_ENGINE_AUDIO_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  TP_STREAM_ENGINE_TYPE_STREAM, TpStreamEngineAudioStream))

#define TP_STREAM_ENGINE_AUDIO_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  TP_STREAM_ENGINE_TYPE_AUDIO_STREAM, TpStreamEngineAudioStreamClass))

#define TP_STREAM_ENGINE_IS_AUDIO_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  TP_STREAM_ENGINE_TYPE_AUDIO_STREAM))

#define TP_STREAM_ENGINE_IS_AUDIO_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  TP_STREAM_ENGINE_TYPE_AUDIO_STREAM))

#define TP_STREAM_ENGINE_AUDIO_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  TP_STREAM_ENGINE_TYPE_AUDIO_STREAM, TpStreamEngineAudioStreamClass))

typedef struct _TpStreamEngineAudioStreamPrivate
          TpStreamEngineAudioStreamPrivate;

typedef struct {
  TpStreamEngineStream parent;

  TpStreamEngineAudioStreamPrivate *priv;
} TpStreamEngineAudioStream;

typedef struct {
  TpStreamEngineStreamClass parent_class;
} TpStreamEngineAudioStreamClass;

GType tp_stream_engine_audio_stream_get_type (void);


gboolean tp_stream_engine_audio_stream_mute_output (
  TpStreamEngineAudioStream *audiostream,
  gboolean mute_state,
  GError **error);

gboolean tp_stream_engine_audio_stream_set_output_volume (
  TpStreamEngineAudioStream *stream,
  guint volume,
  GError **error);

gboolean tp_stream_engine_audio_stream_mute_input (
  TpStreamEngineAudioStream *audiostream,
  gboolean mute_state,
  GError **error);

G_END_DECLS

#endif /* __TP_STREAM_ENGINE_AUDIO_STREAM_H__ */
