#ifndef FUTURE_EXTENSIONS_H
#define FUTURE_EXTENSIONS_H

#include <glib-object.h>
#include <telepathy-glib/telepathy-glib.h>

#include "extensions/_gen/enums.h"
#include "extensions/_gen/cli-channel.h"
#include "extensions/_gen/cli-misc.h"

#include "extensions/call-content.h"
#include "extensions/call-stream.h"

G_BEGIN_DECLS

#include "extensions/_gen/gtypes.h"
#include "extensions/_gen/interfaces.h"

void tf_future_cli_init (void);

G_END_DECLS

#endif
