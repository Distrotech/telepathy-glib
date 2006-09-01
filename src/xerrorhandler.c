
#include "xerrorhandler.h"

#include <X11/Xlib.h>

G_DEFINE_TYPE (TpStreamEngineXErrorHandler, tp_stream_engine_x_error_handler, G_TYPE_OBJECT);

#define X_ERROR_HANDLER_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TP_STREAM_ENGINE_TYPE_X_ERROR_HANDLER, TpStreamEngineXErrorHandlerPrivate))

static TpStreamEngineXErrorHandler *singleton;

typedef struct _TpStreamEngineXErrorHandlerPrivate TpStreamEngineXErrorHandlerPrivate;

struct _TpStreamEngineXErrorHandlerPrivate
{
  int (*old_error_handler)(Display *, XErrorEvent *);
};

enum {
  SIGNAL_BAD_WINDOW,
  SIGNAL_COUNT
};

static guint signals[SIGNAL_COUNT];

static int
error_handler (Display *display, XErrorEvent *event)
{
  TpStreamEngineXErrorHandler *handler =
    tp_stream_engine_x_error_handler_get ();

  g_signal_emit (handler, signals[SIGNAL_BAD_WINDOW], 0, event->resourceid);

  return 0;
}

static void
tp_stream_engine_x_error_handler_dispose (GObject *object)
{
  TpStreamEngineXErrorHandlerPrivate *priv = X_ERROR_HANDLER_PRIVATE (object);

  if (priv->old_error_handler)
    XSetErrorHandler (priv->old_error_handler);

  if (G_OBJECT_CLASS (tp_stream_engine_x_error_handler_parent_class)->dispose)
    G_OBJECT_CLASS (tp_stream_engine_x_error_handler_parent_class)->dispose (object);
}

static void
tp_stream_engine_x_error_handler_class_init (TpStreamEngineXErrorHandlerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TpStreamEngineXErrorHandlerPrivate));

  object_class->dispose = tp_stream_engine_x_error_handler_dispose;

  signals[SIGNAL_BAD_WINDOW] =
    g_signal_new ("bad-window",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
tp_stream_engine_x_error_handler_init (TpStreamEngineXErrorHandler *self)
{
  TpStreamEngineXErrorHandlerPrivate *priv = X_ERROR_HANDLER_PRIVATE (self);

  priv->old_error_handler = XSetErrorHandler (error_handler);
}

TpStreamEngineXErrorHandler*
tp_stream_engine_x_error_handler_get (void)
{
  if (NULL == singleton)
    singleton = g_object_new (TP_STREAM_ENGINE_TYPE_X_ERROR_HANDLER, NULL);

  return singleton;
}

void
tp_stream_engine_x_error_handler_cleanup (void)
{
  if (NULL != singleton)
    {
      g_object_unref (singleton);
      singleton = NULL;
    }
}

