/* tp-intset.h - Headers for a Glib-link set of integers
 *
 * Copyright (C) 2005, 2006 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2005, 2006 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __TP_INTSET_H__
#define __TP_INTSET_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * TpIntSet:
 *
 * Opaque type representing a set of unsigned integers.
 */
typedef struct _TpIntSet TpIntSet;

/**
 * TpIntFunc:
 * @i: The relevant integer
 * @userdata: Opaque user data
 *
 * A callback function acting on unsigned integers.
 */
typedef void (*TpIntFunc) (guint i, gpointer userdata);

TpIntSet *tp_intset_new (void) G_GNUC_WARN_UNUSED_RESULT;
TpIntSet *tp_intset_sized_new (guint size) G_GNUC_WARN_UNUSED_RESULT;
TpIntSet *tp_intset_new_containing (guint element) G_GNUC_WARN_UNUSED_RESULT;
void tp_intset_destroy (TpIntSet *set);
void tp_intset_clear (TpIntSet *set);

void tp_intset_add (TpIntSet *set, guint element);
gboolean tp_intset_remove (TpIntSet *set, guint element);
gboolean tp_intset_is_member (const TpIntSet *set, guint element)
  G_GNUC_WARN_UNUSED_RESULT;

void tp_intset_foreach (const TpIntSet *set, TpIntFunc func,
    gpointer userdata);
GArray *tp_intset_to_array (const TpIntSet *set) G_GNUC_WARN_UNUSED_RESULT;
TpIntSet *tp_intset_from_array (const GArray *array) G_GNUC_WARN_UNUSED_RESULT;

guint tp_intset_size (const TpIntSet *set) G_GNUC_WARN_UNUSED_RESULT;

gboolean tp_intset_is_equal (const TpIntSet *left, const TpIntSet *right)
  G_GNUC_WARN_UNUSED_RESULT;

TpIntSet *tp_intset_copy (const TpIntSet *orig) G_GNUC_WARN_UNUSED_RESULT;
TpIntSet *tp_intset_intersection (const TpIntSet *left, const TpIntSet *right)
  G_GNUC_WARN_UNUSED_RESULT;
TpIntSet *tp_intset_union (const TpIntSet *left, const TpIntSet *right)
  G_GNUC_WARN_UNUSED_RESULT;
TpIntSet *tp_intset_difference (const TpIntSet *left, const TpIntSet *right)
  G_GNUC_WARN_UNUSED_RESULT;
TpIntSet *tp_intset_symmetric_difference (const TpIntSet *left,
    const TpIntSet *right) G_GNUC_WARN_UNUSED_RESULT;

gchar *tp_intset_dump (const TpIntSet *set) G_GNUC_WARN_UNUSED_RESULT;

typedef struct _TpIntSetIter TpIntSetIter;

/**
 * TpIntSetIter:
 * @set: The set iterated over.
 * @element: Must be (guint)(-1) before iteration starts. Set to the next
 *  element in the set by tp_intset_iter_next(); undefined after
 *  tp_intset_iter_next() returns %FALSE.
 *
 * A structure representing iteration over a set of integers. Must be
 * initialized with either TP_INTSET_ITER_INIT() or tp_intset_iter_init().
 */
struct _TpIntSetIter
{
    const TpIntSet *set;
    guint element;
};

/**
 * TP_INTSET_ITER_INIT:
 * @set: A set of integers
 *
 * A suitable static initializer for a #TpIntSetIter, to be used as follows:
 *
 * <informalexample><programlisting>
 * void
 * do_something (const TpIntSet *intset)
 * {
 *   TpIntSetIter iter = TP_INTSET_ITER_INIT (intset);
 *   /<!-- -->* ... do something with iter ... *<!-- -->/
 * }
 * </programlisting></informalexample>
 */
#define TP_INTSET_ITER_INIT(set) { (set), (guint)(-1) }

/**
 * tp_intset_iter_init:
 * @iter: An integer set iterator to be initialized.
 * @set: An integer set to be used by that iterator
 *
 * Reset the iterator @iter to the beginning and make it iterate over @set.
 */
#define tp_intset_iter_init(iter, set) tp_intset_iter_init_inline (iter, set)
static inline void
tp_intset_iter_init_inline (TpIntSetIter *iter, const TpIntSet *set)
{
  g_return_if_fail (iter != NULL);
  iter->set = set;
  iter->element = (guint)(-1);
}

/**
 * tp_intset_iter_reset:
 * @iter: An integer set iterator to be reset.
 *
 * Reset the iterator @iter to the beginning. It must already be associated
 * with a set.
 */
#define tp_intset_iter_reset(iter) tp_intset_iter_reset_inline (iter)
static inline void
tp_intset_iter_reset_inline (TpIntSetIter *iter)
{
  g_return_if_fail (iter != NULL);
  g_return_if_fail (iter->set != NULL);
  iter->element = (guint)(-1);
}

gboolean tp_intset_iter_next (TpIntSetIter *iter);

G_END_DECLS

#endif /*__TP_INTSET_H__*/
