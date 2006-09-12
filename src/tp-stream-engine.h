/*
 * tp-stream-engine.h - Header for TpStreamEngine
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __TP_STREAM_ENGINE_H__
#define __TP_STREAM_ENGINE_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _TpStreamEngine TpStreamEngine;
typedef struct _TpStreamEngineClass TpStreamEngineClass;

struct _TpStreamEngineClass {
    GObjectClass parent_class;
};

struct _TpStreamEngine {
    GObject parent;
};

GType tp_stream_engine_get_type(void);

/* TYPE MACROS */
#define TP_TYPE_STREAM_ENGINE \
  (tp_stream_engine_get_type())
#define TP_STREAM_ENGINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_STREAM_ENGINE, TpStreamEngine))
#define TP_STREAM_ENGINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_STREAM_ENGINE, TpStreamEngineClass))
#define TP_IS_STREAM_ENGINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_STREAM_ENGINE))
#define TP_IS_STREAM_ENGINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_STREAM_ENGINE))
#define TP_STREAM_ENGINE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_STREAM_ENGINE, TpStreamEngineClass))


gboolean tp_stream_engine_add_preview_window (TpStreamEngine *obj, guint window, GError **error);
gboolean tp_stream_engine_handle_channel (TpStreamEngine *obj, const gchar * bus_name, const gchar * connection, const gchar * channel_type, const gchar * channel, guint handle_type, guint handle, GError **error);
gboolean tp_stream_engine_mute_input (TpStreamEngine *obj, const gchar * channel_path, guint stream_id, gboolean mute_state, GError **error);
gboolean tp_stream_engine_mute_output (TpStreamEngine *obj, const gchar * channel_path, guint stream_id, gboolean mute_state, GError **error);
gboolean tp_stream_engine_remove_preview_window (TpStreamEngine *obj, guint window, GError **error);
gboolean tp_stream_engine_set_output_volume (TpStreamEngine *obj, const gchar * channel_path, guint stream_id, guint volume, GError **error);
gboolean tp_stream_engine_set_output_window (TpStreamEngine *obj, const gchar * channel_path, guint stream_id, guint window, GError **error);


void tp_stream_engine_register (TpStreamEngine *self);
void tp_stream_engine_error (TpStreamEngine *self, int error, const char *debug);

TpStreamEngine *tp_stream_engine_get ();
GstElement *tp_stream_engine_get_pipeline (TpStreamEngine *obj);

gboolean
tp_stream_engine_add_output_window (TpStreamEngine *obj,
                                    GstElement *sink,
                                    guint window_id);

gboolean
tp_stream_engine_remove_output_window (TpStreamEngine *obj,
                                       guint window_id);

G_END_DECLS

#endif /* #ifndef __TP_STREAM_ENGINE_H__*/
