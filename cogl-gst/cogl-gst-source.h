/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007, 2008 OpenedHand
 * Copyright (C) 2009, 2010, 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __COGL_GST_SOURCE_H__
#define __COGL_GST_SOURCE_H__

#include <gst/gst.h>
#include <glib-object.h>
#include "cogl-gst-video-sink.h"

G_BEGIN_DECLS

#define COGL_GST_TYPE_SOURCE (cogl_gst_source_get_type ())
#define COGL_GST_SOURCE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_GST_TYPE_SOURCE, CoglGstSource))
#define COGL_GST_SOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), COGL_GST_TYPE_SOURCE, CoglGstSourceClass))
#define COGL_GST_IS_SOURCE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COGL_GST_TYPE_SOURCE))
#define COGL_GST_IS_SOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COGL_GST_TYPE_SOURCE))
#define COGL_GST_SOURCE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), COGL_GST_TYPE_SOURCE, CoglGstSourceClass))

typedef struct _CoglGstSource CoglGstSource;
typedef struct _CoglGstSourceClass CoglGstSourceClass;

typedef enum
{
  COGL_GST_SOURCE_NONE = 0,
  COGL_GST_SOURCE_NEW_BUFFER = (1 << 0),
  COGL_GST_SOURCE_NEW_CAPS = (1 << 1)
} CoglGstSourceFlags;

struct _CoglGstSource
{
  GSource parent_instance;
  GstVideoSink *sink;
  GMutex buffer_lock;
  GstBuffer *buffer;
  gboolean has_new_caps;
  CoglGstSourceFlags flags;
};

struct _CoglGstSourceClass
{
  GSourceClass parent_class;
};

GType cogl_gst_source_get_type (void);

CoglGstSource *cogl_gst_source_new (GstVideoSink *sink);

G_END_DECLS

#endif /* __COGL_GST_SOURCE_H__ */
