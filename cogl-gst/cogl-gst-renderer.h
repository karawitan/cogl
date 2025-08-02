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

#ifndef __COGL_GST_RENDERER_H__
#define __COGL_GST_RENDERER_H__

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "cogl-gst-video-sink.h"

G_BEGIN_DECLS

typedef enum
{
  COGL_GST_NOFORMAT,
  COGL_GST_RGB32,
  COGL_GST_RGB24,
  COGL_GST_AYUV,
  COGL_GST_YV12,
  COGL_GST_I420,
  COGL_GST_NV12
} CoglGstVideoFormat;

typedef enum
{
  COGL_GST_RENDERER_NEEDS_GLSL = (1 << 0),
  COGL_GST_RENDERER_NEEDS_TEXTURE_RG = (1 << 1)
} CoglGstRendererFlag;

typedef void (CoglGstRendererSetupPipeline) (CoglGstVideoSink *sink,
                                            GstVideoPipeline *pipeline);
typedef gboolean (CoglGstRendererUpload) (CoglGstVideoSink *sink,
                                         GstBuffer *buffer);

typedef struct _CoglGstRenderer CoglGstRenderer;

struct _CoglGstRenderer
{
  const char *name;
  CoglGstVideoFormat format;
  int flags;
  GstStaticCaps caps;
  int n_layers;
  CoglGstRendererSetupPipeline *setup_pipeline;
  CoglGstRendererUpload *upload;
};

G_END_DECLS

#endif /* __COGL_GST_RENDERER_H__ */
