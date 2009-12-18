/*
 * util-internal.h - Headers for non-public telepathy-glib utility functions
 *
 * Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
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

#ifndef __TP_UTIL_INTERNAL_H__
#define __TP_UTIL_INTERNAL_H__

#include <glib.h>

GArray *_tp_quark_array_copy (const GQuark *quarks) G_GNUC_WARN_UNUSED_RESULT;

#endif /* __TP_UTIL_INTERNAL_H__ */
