#ifndef __TP_DEBUG_H__
#define __TP_DEBUG_H__

#include <glib.h>
#include <telepathy-glib/defs.h>

G_BEGIN_DECLS

void tp_debug_set_flags (const gchar *flags_string);

void tp_debug_set_persistent (gboolean persistent);

void tp_debug_divert_messages (const gchar *filename);

void tp_debug_timestamped_log_handler (const gchar *log_domain,
    GLogLevelFlags log_level, const gchar *message, gpointer ignored);

#ifndef TP_DISABLE_DEPRECATED
void tp_debug_set_flags_from_string (const gchar *flags_string)
  _TP_GNUC_DEPRECATED;
void tp_debug_set_flags_from_env (const gchar *var)
  _TP_GNUC_DEPRECATED;
void tp_debug_set_all_flags (void) _TP_GNUC_DEPRECATED;
#endif

G_END_DECLS

#endif
