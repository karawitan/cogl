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

#include "config.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include "cogl-gst-video-sink.h"

#define COGL_GST_VIDEO_SINK_GET_PRIVATE(obj) (cogl_gst_video_sink_get_instance_private ((CoglGstVideoSink *)obj))

G_DEFINE_TYPE_WITH_PRIVATE (CoglGstVideoSink, cogl_gst_video_sink, GST_TYPE_VIDEO_SINK)

enum
{
  PROP_0,
  PROP_UPDATE_PRIORITY,
  N_PROPERTIES
};

enum
{
  PIPELINE_READY_SIGNAL,
  NEW_FRAME_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
  CoglContext *ctx;
  CoglPipeline *pipeline;
  CoglTexture *frame[3];
  CoglBool frame_dirty;
  CoglGstVideoFormat format;
  CoglBool bgr;
  CoglGstSource *source;
  GSList *renderers;
  GstCaps *caps;
  CoglGstRenderer *renderer;
  GstFlowReturn flow_return;
  int custom_start;
  int free_layer;
  CoglBool default_sample;
  GstVideoInfo info;
} CoglGstVideoSinkPrivate;

static gpointer
cogl_gst_rectangle_copy (gpointer src)
{
  if (G_LIKELY (src))
    {
      CoglGstRectangle *new = g_slice_new (CoglGstRectangle);
      memcpy (new, src, sizeof (CoglGstRectangle));
      return new;
    }
  else
    return NULL;
}

static void
cogl_gst_rectangle_free (gpointer ptr)
{
  g_slice_free (CoglGstRectangle, ptr);
}

COGL_GTYPE_DEFINE_BOXED (GstRectangle,
                         gst_rectangle,
                         cogl_gst_rectangle_copy,
                         cogl_gst_rectangle_free);

static void
cogl_gst_source_finalize (GSource *source)
{
  CoglGstSource *gst_source = (CoglGstSource *) source;

  g_mutex_lock (&gst_source->buffer_lock);
  if (gst_source->buffer)
    gst_buffer_unref (gst_source->buffer);
  gst_source->buffer = NULL;
  g_mutex_unlock (&gst_source->buffer_lock);
  g_mutex_clear (&gst_source->buffer_lock);
}

int
cogl_gst_video_sink_get_free_layer (CoglGstVideoSink *sink)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  return priv->free_layer;
}

void
cogl_gst_video_sink_attach_frame (CoglGstVideoSink *sink,
                                  CoglPipeline *pln)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  int i;

  for (i = 0; i < G_N_ELEMENTS (priv->frame); i++)
    if (priv->frame[i] != NULL)
      cogl_pipeline_set_layer_texture (pln, i + priv->custom_start,
                                       priv->frame[i]);
}

static CoglBool
cogl_gst_source_prepare (GSource *source,
                         int *timeout)
{
  CoglGstSource *gst_source = (CoglGstSource *) source;

  *timeout = -1;

  return gst_source->buffer != NULL;
}

static CoglBool
cogl_gst_source_check (GSource *source)
{
  CoglGstSource *gst_source = (CoglGstSource *) source;

  return gst_source->buffer != NULL;
}

static void
cogl_gst_video_sink_set_priority (CoglGstVideoSink *sink,
                                  int priority)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  if (priv->source)
    g_source_set_priority ((GSource *) priv->source, priority);
}

static void
dirty_default_pipeline (CoglGstVideoSink *sink)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  if (priv->pipeline)
    {
      cogl_object_unref (priv->pipeline);
      priv->pipeline = NULL;
    }
}

void
cogl_gst_video_sink_set_first_layer (CoglGstVideoSink *sink,
                                     int first_layer)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  g_return_if_fail (COGL_GST_IS_VIDEO_SINK (sink));

  if (first_layer != priv->custom_start)
    {
      priv->custom_start = first_layer;
      dirty_default_pipeline (sink);

      if (priv->renderer)
        priv->free_layer = (priv->custom_start +
                            priv->renderer->n_layers);
    }
}

void
cogl_gst_video_sink_set_default_sample (CoglGstVideoSink *sink,
                                        CoglBool default_sample)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  g_return_if_fail (COGL_GST_IS_VIDEO_SINK (sink));

  if (default_sample != priv->default_sample)
    {
      priv->default_sample = default_sample;
      dirty_default_pipeline (sink);
    }
}

void
cogl_gst_video_sink_setup_pipeline (CoglGstVideoSink *sink,
                                    CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  if (priv->renderer)
    priv->renderer->setup_pipeline (sink, pipeline);
}

static SnippetCacheEntry *
get_cache_entry (CoglGstVideoSink *sink,
                 SnippetCache *cache)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  GList *l;

  for (l = cache->entries.head; l; l = l->next)
    {
      SnippetCacheEntry *entry = l->data;

      if (entry->start_position == priv->custom_start)
        return entry;
    }

  return NULL;
}

static SnippetCacheEntry *
add_cache_entry (CoglGstVideoSink *sink,
                 SnippetCache *cache,
                 const char *decl)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  SnippetCacheEntry *entry = g_slice_new (SnippetCacheEntry);
  char *default_source;

  entry->start_position = priv->custom_start;

  entry->vertex_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
                      decl,
                      NULL /* post */);
  entry->fragment_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                      decl,
                      NULL /* post */);

  default_source =
    g_strdup_printf ("  cogl_layer *= cogl_gst_sample_video%i "
                     "(cogl_tex_coord%i_in.st);\n",
                     priv->custom_start,
                     priv->custom_start);
  entry->default_sample_snippet =
    cogl_snippet_new (COGL_SNIPPET_HOOK_LAYER_FRAGMENT,
                      NULL, /* declarations */
                      default_source);
  g_free (default_source);

  g_queue_push_head (&cache->entries, entry);

  return entry;
}

static void
setup_pipeline_from_cache_entry (CoglGstVideoSink *sink,
                                 CoglPipeline *pipeline,
                                 SnippetCacheEntry *cache_entry,
                                 int n_layers)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  if (cache_entry)
    {
      int i;

      /* The global sampling function gets added to both the fragment
       * and vertex stages. The hope is that the GLSL compiler will
       * easily remove the dead code if it's not actually used */
      cogl_pipeline_add_snippet (pipeline, cache_entry->vertex_snippet);
      cogl_pipeline_add_snippet (pipeline, cache_entry->fragment_snippet);

      /* Set all of the layers to just directly copy from the previous
       * layer so that it won't redundantly generate code to sample
       * the intermediate textures */
      for (i = 0; i < n_layers; i++)
        cogl_pipeline_set_layer_combine (pipeline,
                                         priv->custom_start + i,
                                         "RGBA=REPLACE(PREVIOUS)",
                                         NULL);

      if (priv->default_sample)
        cogl_pipeline_add_layer_snippet (pipeline,
                                         priv->custom_start + n_layers - 1,
                                         cache_entry->default_sample_snippet);
    }

  priv->frame_dirty = TRUE;
}

CoglPipeline *
cogl_gst_video_sink_get_pipeline (CoglGstVideoSink *vt)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (vt);

  g_return_val_if_fail (COGL_GST_IS_VIDEO_SINK (vt), NULL);

  if (priv->pipeline == NULL)
    {
      priv->pipeline = cogl_pipeline_new (priv->ctx);
      cogl_gst_video_sink_setup_pipeline (vt, priv->pipeline);
      cogl_gst_video_sink_attach_frame (vt, priv->pipeline);
      priv->frame_dirty = FALSE;
    }
  else if (priv->frame_dirty)
    {
      CoglPipeline *pipeline = cogl_pipeline_copy (priv->pipeline);
      cogl_object_unref (priv->pipeline);
      priv->pipeline = pipeline;

      cogl_gst_video_sink_attach_frame (vt, pipeline);
      priv->frame_dirty = FALSE;
    }

  return priv->pipeline;
}

static void
clear_frame_textures (CoglGstVideoSink *sink)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  int i;

  for (i = 0; i < G_N_ELEMENTS (priv->frame); i++)
    {
      if (priv->frame[i] == NULL)
        break;
      else
        cogl_object_unref (priv->frame[i]);
    }

  memset (priv->frame, 0, sizeof (priv->frame));

  priv->frame_dirty = TRUE;
}

static inline CoglBool
is_pot (unsigned int number)
{
  /* Make sure there is only one bit set */
  return (number & (number - 1)) == 0;
}

static CoglTexture *
video_texture_new_from_data (CoglContext *ctx,
                             int width,
                             int height,
                             CoglPixelFormat format,
                             int rowstride,
                             const uint8_t *data)
{
  CoglBitmap *bitmap;
  CoglTexture *tex;
  CoglError *internal_error = NULL;

  bitmap = cogl_bitmap_new_for_data (ctx,
                                     width, height,
                                     format,
                                     rowstride,
                                     (uint8_t *) data);

  if ((is_pot (cogl_bitmap_get_width (bitmap)) &&
       is_pot (cogl_bitmap_get_height (bitmap))) ||
      cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NPOT_BASIC))
    {
      tex = cogl_texture_2d_new_from_bitmap (bitmap);

      cogl_texture_set_premultiplied (tex, FALSE);

      if (!cogl_texture_allocate (tex, &internal_error))
        {
          cogl_error_free (internal_error);
          internal_error = NULL;
          cogl_object_unref (tex);
          tex = NULL;
        }
    }
  else
    tex = NULL;

  if (!tex)
    {
      tex = cogl_texture_2d_sliced_new_from_bitmap (bitmap,
                                                    -1); /* no maximum waste */

      cogl_texture_set_premultiplied (tex, FALSE);

      cogl_texture_allocate (tex, NULL);
    }

  cogl_object_unref (bitmap);

  return tex;
}

static void
cogl_gst_rgb24_glsl_setup_pipeline (CoglGstVideoSink *sink,
                                    CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry = get_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "cogl_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  return texture2D (cogl_sampler%i, UV);\n"
                         "}\n",
                         priv->custom_start,
                         priv->custom_start);

      entry = add_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 1);
}

static void
cogl_gst_rgb24_setup_pipeline (CoglGstVideoSink *sink,
                               CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  setup_pipeline_from_cache_entry (sink, pipeline, NULL, 1);
}

static CoglBool
cogl_gst_rgb24_upload (CoglGstVideoSink *sink,
                       GstBuffer *buffer)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  CoglPixelFormat format;
  GstVideoFrame frame;

  if (priv->bgr)
    format = COGL_PIXEL_FORMAT_BGR_888;
  else
    format = COGL_PIXEL_FORMAT_RGB_888;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] = video_texture_new_from_data (priv->ctx, priv->info.width,
                                                priv->info.height,
                                                format,
                                                priv->info.stride[0],
                                                frame.data[0]);

  gst_video_frame_unmap (&frame);

  return TRUE;

map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static CoglGstRenderer rgb24_glsl_renderer =
{
  "RGB 24",
  COGL_GST_RGB24,
  COGL_GST_RENDERER_NEEDS_GLSL,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR }")),
  1, /* n_layers */
  cogl_gst_rgb24_glsl_setup_pipeline,
  cogl_gst_rgb24_upload,
};

static CoglGstRenderer rgb24_renderer =
{
  "RGB 24",
  COGL_GST_RGB24,
  0,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR }")),
  1, /* n_layers */
  cogl_gst_rgb24_setup_pipeline,
  cogl_gst_rgb24_upload,
};

static void
cogl_gst_rgb32_glsl_setup_pipeline (CoglGstVideoSink *sink,
                                    CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry = get_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "cogl_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  vec4 color = texture2D (cogl_sampler%i, UV);\n"
                         /* Premultiply the color */
                         "  color.rgb *= color.a;\n"
                         "  return color;\n"
                         "}\n", priv->custom_start,
                         priv->custom_start);

      entry = add_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 1);
}

static void
cogl_gst_rgb32_setup_pipeline (CoglGstVideoSink *sink,
                               CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  char *layer_combine;

  setup_pipeline_from_cache_entry (sink, pipeline, NULL, 1);

  layer_combine = g_strdup_printf ("RGB=MODULATE(PREVIOUS, TEXTURE_%i[A])\n"
                                   "A=REPLACE(PREVIOUS[A])",
                                   priv->custom_start);
  cogl_pipeline_set_layer_combine (pipeline,
                                   priv->custom_start + 1,
                                   layer_combine,
                                   NULL);
  g_free(layer_combine);
}

static CoglBool
cogl_gst_rgb32_upload (CoglGstVideoSink *sink,
                       GstBuffer *buffer)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  CoglPixelFormat format;
  GstVideoFrame frame;

  if (priv->bgr)
    format = COGL_PIXEL_FORMAT_BGRA_8888;
  else
    format = COGL_PIXEL_FORMAT_RGBA_8888;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] = video_texture_new_from_data (priv->ctx, priv->info.width,
                                                priv->info.height,
                                                format,
                                                priv->info.stride[0],
                                                frame.data[0]);

  gst_video_frame_unmap (&frame);

  return TRUE;

map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static CoglGstRenderer rgb32_glsl_renderer =
{
  "RGB 32",
  COGL_GST_RGB32,
  COGL_GST_RENDERER_NEEDS_GLSL,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBA, BGRA }")),
  1, /* n_layers */
  cogl_gst_rgb32_glsl_setup_pipeline,
  cogl_gst_rgb32_upload,
};

static CoglGstRenderer rgb32_renderer =
{
  "RGB 32",
  COGL_GST_RGB32,
  0,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBA, BGRA }")),
  2, /* n_layers */
  cogl_gst_rgb32_setup_pipeline,
  cogl_gst_rgb32_upload,
};

static CoglBool
cogl_gst_yv12_upload (CoglGstVideoSink *sink,
                      GstBuffer *buffer)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  CoglPixelFormat format = COGL_PIXEL_FORMAT_A_8;
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 0),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 0),
                                 format,
                                 priv->info.stride[0], frame.data[0]);

  priv->frame[2] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 1),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 1),
                                 format,
                                 priv->info.stride[1], frame.data[1]);

  priv->frame[1] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 2),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 2),
                                 format,
                                 priv->info.stride[2], frame.data[2]);

  gst_video_frame_unmap (&frame);

  return TRUE;

map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static CoglBool
cogl_gst_i420_upload (CoglGstVideoSink *sink,
                      GstBuffer *buffer)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  CoglPixelFormat format = COGL_PIXEL_FORMAT_A_8;
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 0),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 0),
                                 format,
                                 priv->info.stride[0], frame.data[0]);

  priv->frame[1] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 1),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 1),
                                 format,
                                 priv->info.stride[1], frame.data[1]);

  priv->frame[2] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 2),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 2),
                                 format,
                                 priv->info.stride[2], frame.data[2]);

  gst_video_frame_unmap (&frame);

  return TRUE;

map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static void
cogl_gst_yv12_glsl_setup_pipeline (CoglGstVideoSink *sink,
                                   CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry;

  entry = get_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "cogl_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  float y = 1.1640625 * "
                         "(texture2D (cogl_sampler%i, UV).a - 0.0625);\n"
                         "  float u = texture2D (cogl_sampler%i, UV).a - 0.5;\n"
                         "  float v = texture2D (cogl_sampler%i, UV).a - 0.5;\n"
                         "  vec4 color;\n"
                         "  color.r = y + 1.59765625 * v;\n"
                         "  color.g = y - 0.390625 * u - 0.8125 * v;\n"
                         "  color.b = y + 2.015625 * u;\n"
                         "  color.a = 1.0;\n"
                         "  return color;\n"
                         "}\n",
                         priv->custom_start,
                         priv->custom_start,
                         priv->custom_start + 1,
                         priv->custom_start + 2);

      entry = add_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 3);
}

static CoglGstRenderer yv12_glsl_renderer =
{
  "YV12 glsl",
  COGL_GST_YV12,
  COGL_GST_RENDERER_NEEDS_GLSL,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("YV12")),
  3, /* n_layers */
  cogl_gst_yv12_glsl_setup_pipeline,
  cogl_gst_yv12_upload,
};

static CoglGstRenderer i420_glsl_renderer =
{
  "I420 glsl",
  COGL_GST_I420,
  COGL_GST_RENDERER_NEEDS_GLSL,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420")),
  3, /* n_layers */
  cogl_gst_yv12_glsl_setup_pipeline,
  cogl_gst_i420_upload,
};

static void
cogl_gst_ayuv_glsl_setup_pipeline (CoglGstVideoSink *sink,
                                   CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry;

  entry = get_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source
        = g_strdup_printf ("vec4\n"
                           "cogl_gst_sample_video%i (vec2 UV)\n"
                           "{\n"
                           "  vec4 color = texture2D (cogl_sampler%i, UV);\n"
                           "  float y = 1.1640625 * (color.g - 0.0625);\n"
                           "  float u = color.b - 0.5;\n"
                           "  float v = color.a - 0.5;\n"
                           "  color.a = color.r;\n"
                           "  color.r = y + 1.59765625 * v;\n"
                           "  color.g = y - 0.390625 * u - 0.8125 * v;\n"
                           "  color.b = y + 2.015625 * u;\n"
                           /* Premultiply the color */
                           "  color.rgb *= color.a;\n"
                           "  return color;\n"
                           "}\n", priv->custom_start,
                           priv->custom_start);

      entry = add_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 1);
}

static CoglBool
cogl_gst_ayuv_upload (CoglGstVideoSink *sink,
                      GstBuffer *buffer)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  CoglPixelFormat format = COGL_PIXEL_FORMAT_RGBA_8888;
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] = video_texture_new_from_data (priv->ctx, priv->info.width,
                                                priv->info.height,
                                                format,
                                                priv->info.stride[0],
                                                frame.data[0]);

  gst_video_frame_unmap (&frame);

  return TRUE;

map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static CoglGstRenderer ayuv_glsl_renderer =
{
  "AYUV glsl",
  COGL_GST_AYUV,
  COGL_GST_RENDERER_NEEDS_GLSL,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("AYUV")),
  1, /* n_layers */
  cogl_gst_ayuv_glsl_setup_pipeline,
  cogl_gst_ayuv_upload,
};

static void
cogl_gst_nv12_glsl_setup_pipeline (CoglGstVideoSink *sink,
                                   CoglPipeline *pipeline)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  static SnippetCache snippet_cache;
  SnippetCacheEntry *entry;

  entry = get_cache_entry (sink, &snippet_cache);

  if (entry == NULL)
    {
      char *source;

      source =
        g_strdup_printf ("vec4\n"
                         "cogl_gst_sample_video%i (vec2 UV)\n"
                         "{\n"
                         "  vec4 color;\n"
                         "  float y = 1.1640625 *\n"
                         "            (texture2D (cogl_sampler%i, UV).a -\n"
                         "             0.0625);\n"
                         "  vec2 uv = texture2D (cogl_sampler%i, UV).rg;\n"
                         "  uv -= 0.5;\n"
                         "  float u = uv.x;\n"
                         "  float v = uv.y;\n"
                         "  color.r = y + 1.59765625 * v;\n"
                         "  color.g = y - 0.390625 * u - 0.8125 * v;\n"
                         "  color.b = y + 2.015625 * u;\n"
                         "  color.a = 1.0;\n"
                         "  return color;\n"
                         "}\n",
                         priv->custom_start,
                         priv->custom_start,
                         priv->custom_start + 1);

      entry = add_cache_entry (sink, &snippet_cache, source);
      g_free (source);
    }

  setup_pipeline_from_cache_entry (sink, pipeline, entry, 2);
}

static CoglBool
cogl_gst_nv12_upload (CoglGstVideoSink *sink,
                      GstBuffer *buffer)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  GstVideoFrame frame;

  if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_READ))
    goto map_fail;

  clear_frame_textures (sink);

  priv->frame[0] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 0),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 0),
                                 COGL_PIXEL_FORMAT_A_8,
                                 priv->info.stride[0],
                                 frame.data[0]);

  priv->frame[1] =
    video_texture_new_from_data (priv->ctx,
                                 GST_VIDEO_INFO_COMP_WIDTH (&priv->info, 1),
                                 GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, 1),
                                 COGL_PIXEL_FORMAT_RG_88,
                                 priv->info.stride[1],
                                 frame.data[1]);

  gst_video_frame_unmap (&frame);

  return TRUE;

 map_fail:
  {
    GST_ERROR_OBJECT (sink, "Could not map incoming video frame");
    return FALSE;
  }
}

static CoglGstRenderer nv12_glsl_renderer =
{
  "NV12 glsl",
  COGL_GST_NV12,
  COGL_GST_RENDERER_NEEDS_GLSL | COGL_GST_RENDERER_NEEDS_TEXTURE_RG,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:SystemMemory",
                                                      "NV12")),
  2, /* n_layers */
  cogl_gst_nv12_glsl_setup_pipeline,
  cogl_gst_nv12_upload,
};

static GSList*
cogl_gst_build_renderers_list (CoglContext *ctx)
{
  GSList *list = NULL;
  CoglGstRendererFlag flags = 0;
  int i;
  static CoglGstRenderer *const renderers[] =
  {
    /* These are in increasing order of priority so that the
     * priv->renderers will be in decreasing order. That way the GLSL
     * renderers will be preferred if they are available */
    &rgb24_renderer,
    &rgb32_renderer,
    &rgb24_glsl_renderer,
    &rgb32_glsl_renderer,
    &yv12_glsl_renderer,
    &i420_glsl_renderer,
    &ayuv_glsl_renderer,
    &nv12_glsl_renderer,
    NULL
  };

  if (cogl_has_feature (ctx, COGL_FEATURE_ID_GLSL))
    flags |= COGL_GST_RENDERER_NEEDS_GLSL;

  if (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_RG))
    flags |= COGL_GST_RENDERER_NEEDS_TEXTURE_RG;

  for (i = 0; renderers[i]; i++)
    if ((renderers[i]->flags & flags) == renderers[i]->flags)
      list = g_slist_prepend (list, renderers[i]);

  return list;
}

static void
append_cap (gpointer data,
            gpointer user_data)
{
  CoglGstRenderer *renderer = (CoglGstRenderer *) data;
  GstCaps *caps = (GstCaps *) user_data;
  GstCaps *writable_caps;
  writable_caps =
    gst_caps_make_writable (gst_static_caps_get (&renderer->caps));
  gst_caps_append (caps, writable_caps);
}

static GstCaps *
cogl_gst_build_caps (GSList *renderers)
{
  GstCaps *caps;

  caps = gst_caps_new_empty ();

  g_slist_foreach (renderers, append_cap, caps);

  return caps;
}

void
cogl_gst_video_sink_set_context (CoglGstVideoSink *vt,
                                 CoglContext *ctx)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (vt);

  if (ctx)
    ctx = cogl_object_ref (ctx);

  if (priv->ctx)
    {
      cogl_object_unref (priv->ctx);
      g_slist_free (priv->renderers);
      priv->renderers = NULL;
      if (priv->caps)
        {
          gst_caps_unref (priv->caps);
          priv->caps = NULL;
        }
    }

  if (ctx)
    {
      priv->ctx = ctx;
      priv->renderers = cogl_gst_build_renderers_list (priv->ctx);
      priv->caps = cogl_gst_build_caps (priv->renderers);
    }
}

static CoglGstRenderer *
cogl_gst_find_renderer_by_format (CoglGstVideoSink *sink,
                                  CoglGstVideoFormat format)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  CoglGstRenderer *renderer = NULL;
  GSList *element;

  /* The renderers list is in decreasing order of priority so we'll
   * pick the first one that matches */
  for (element = priv->renderers; element; element = g_slist_next (element))
    {
      CoglGstRenderer *candidate = (CoglGstRenderer *) element->data;
      if (candidate->format == format)
        {
          renderer = candidate;
          break;
        }
    }

  return renderer;
}

static GstCaps *
cogl_gst_video_sink_get_caps (GstBaseSink *bsink,
                              GstCaps *filter)
{
  CoglGstVideoSink *sink;
  sink = COGL_GST_VIDEO_SINK (bsink);

  if (sink->priv->caps == NULL)
    return NULL;
  else
    return gst_caps_ref (sink->priv->caps);
}

static CoglBool
cogl_gst_video_sink_parse_caps (GstCaps *caps,
                                CoglGstVideoSink *sink,
                                CoglBool save)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  GstCaps *intersection;
  GstVideoInfo vinfo;
  CoglGstVideoFormat format;
  CoglBool bgr = FALSE;
  CoglGstRenderer *renderer;

  intersection = gst_caps_intersect (priv->caps, caps);
  if (gst_caps_is_empty (intersection))
    goto no_intersection;

  gst_caps_unref (intersection);

  if (!gst_video_info_from_caps (&vinfo, caps))
    goto unknown_format;

  switch (vinfo.finfo->format)
    {
    case GST_VIDEO_FORMAT_YV12:
      format = COGL_GST_YV12;
      break;
    case GST_VIDEO_FORMAT_I420:
      format = COGL_GST_I420;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      format = COGL_GST_AYUV;
      bgr = FALSE;
      break;
    case GST_VIDEO_FORMAT_NV12:
      format = COGL_GST_NV12;
      break;
    case GST_VIDEO_FORMAT_RGB:
      format = COGL_GST_RGB24;
      bgr = FALSE;
      break;
    case GST_VIDEO_FORMAT_BGR:
      format = COGL_GST_RGB24;
      bgr = TRUE;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      format = COGL_GST_RGB32;
      bgr = FALSE;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      format = COGL_GST_RGB32;
      bgr = TRUE;
      break;
    default:
      goto unhandled_format;
    }

  renderer = cogl_gst_find_renderer_by_format (sink, format);

  if (G_UNLIKELY (renderer == NULL))
    goto no_suitable_renderer;

  GST_INFO_OBJECT (sink, "found the %s renderer", renderer->name);

  if (save)
    {
      priv->info = vinfo;

      priv->format = format;
      priv->bgr = bgr;

      priv->renderer = renderer;
    }

  return TRUE;


no_intersection:
  {
    GST_WARNING_OBJECT (sink,
        "Incompatible caps, don't intersect with %" GST_PTR_FORMAT, priv->caps);
    return FALSE;
  }

unknown_format:
  {
    GST_WARNING_OBJECT (sink, "Could not figure format of input caps");
    return FALSE;
  }

unhandled_format:
  {
    GST_ERROR_OBJECT (sink, "Provided caps aren't supported by clutter-gst");
    return FALSE;
  }

no_suitable_renderer:
  {
    GST_ERROR_OBJECT (sink, "could not find a suitable renderer");
    return FALSE;
  }
}

static CoglBool
cogl_gst_video_sink_set_caps (GstBaseSink *bsink,
                              GstCaps *caps)
{
  CoglGstVideoSink *sink;
  CoglGstVideoSinkPrivate *priv;

  sink = COGL_GST_VIDEO_SINK (bsink);
  priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  if (!cogl_gst_video_sink_parse_caps (caps, sink, FALSE))
    return FALSE;

  g_mutex_lock (&priv->source->buffer_lock);
  priv->source->has_new_caps = TRUE;
  g_mutex_unlock (&priv->source->buffer_lock);

  return TRUE;
}

static CoglBool
cogl_gst_source_dispatch (GSource *source,
                          GSourceFunc callback,
                          void *user_data)
{
  CoglGstSource *gst_source= (CoglGstSource*) source;
  CoglGstVideoSinkPrivate *priv = gst_source->sink->priv;
  GstBuffer *buffer;
  gboolean pipeline_ready = FALSE;

  g_mutex_lock (&gst_source->buffer_lock);

  if (G_UNLIKELY (priv->flow_return != GST_FLOW_OK))
    goto dispatch_flow_ret;

  if (gst_source->buffer)
    gst_buffer_unref (gst_source->buffer);

  gst_source->buffer = gst_buffer_ref (buffer);
  g_mutex_unlock (&gst_source->buffer_lock);

  g_main_context_wakeup (NULL);

  return TRUE;

  dispatch_flow_ret:
  {
    g_mutex_unlock (&gst_source->buffer_lock);
    return priv->flow_return;
  }
}

static GSourceFuncs gst_source_funcs =
{
  cogl_gst_source_prepare,
  cogl_gst_source_check,
  cogl_gst_source_dispatch,
  cogl_gst_source_finalize
};

static CoglGstSource *
cogl_gst_source_new (CoglGstVideoSink *sink)
{
  GSource *source;
  CoglGstSource *gst_source;

  source = g_source_new (&gst_source_funcs, sizeof (CoglGstSource));
  gst_source = (CoglGstSource *) source;

  g_source_set_can_recurse (source, TRUE);
  g_source_set_priority (source, COGL_GST_DEFAULT_PRIORITY);

  gst_source->sink = sink;
  g_mutex_init (&gst_source->buffer_lock);
  gst_source->buffer = NULL;

  return gst_source;
}

static void
cogl_gst_video_sink_init (CoglGstVideoSink *sink)
{
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  sink->priv = priv;

  priv->display = NULL;
  priv->pipeline = NULL;
  priv->framebuffer = NULL;
  priv->texture = NULL;
  priv->caps = NULL;
  priv->width = 0;
  priv->height = 0;
  priv->custom_start = 0;
  priv->default_sample = TRUE;
  priv->free_layer = 0;
  priv->ctx = NULL;
  priv->source = NULL;
  priv->renderer = NULL;
  priv->frame_dirty = FALSE;
  priv->frame[0] = priv->frame[1] = priv->frame[2] = NULL;
}

static GstFlowReturn
_cogl_gst_video_sink_render (GstBaseSink *bsink,
                             GstBuffer *buffer)
{
  CoglGstVideoSink *sink = COGL_GST_VIDEO_SINK (bsink);
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);
  CoglGstSource *gst_source = priv->source;

  g_mutex_lock (&gst_source->buffer_lock);

  if (G_UNLIKELY (priv->flow_return != GST_FLOW_OK))
    goto dispatch_flow_ret;

  if (gst_source->buffer)
    gst_buffer_unref (gst_source->buffer);

  gst_source->buffer = gst_buffer_ref (buffer);
  g_mutex_unlock (&gst_source->buffer_lock);

  g_main_context_wakeup (NULL);

  return GST_FLOW_OK;

  dispatch_flow_ret:
  {
    g_mutex_unlock (&gst_source->buffer_lock);
    return priv->flow_return;
  }
}

static void
cogl_gst_video_sink_dispose (GObject *object)
{
  CoglGstVideoSink *self;
  CoglGstVideoSinkPrivate *priv;

  self = COGL_GST_VIDEO_SINK (object);
  priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (self);

  clear_frame_textures (self);

  if (priv->pipeline)
    {
      cogl_object_unref (priv->pipeline);
      priv->pipeline = NULL;
    }

  if (priv->caps)
    {
      gst_caps_unref (priv->caps);
      priv->caps = NULL;
    }

  G_OBJECT_CLASS (cogl_gst_video_sink_parent_class)->dispose (object);
}

static void
cogl_gst_video_sink_finalize (GObject *object)
{
  CoglGstVideoSink *self = COGL_GST_VIDEO_SINK (object);
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (self);

  cogl_gst_video_sink_set_context (self, NULL);

  G_OBJECT_CLASS (cogl_gst_video_sink_parent_class)->finalize (object);
}

static CoglBool
cogl_gst_video_sink_start (GstBaseSink *base_sink)
{
  CoglGstVideoSink *sink = COGL_GST_VIDEO_SINK (base_sink);
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  priv->source = cogl_gst_source_new (sink);
  g_source_attach ((GSource *) priv->source, NULL);
  priv->flow_return = GST_FLOW_OK;
  return TRUE;
}

static void
cogl_gst_video_sink_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  CoglGstVideoSink *sink = COGL_GST_VIDEO_SINK (object);

  switch (property_id)
    {
    case PROP_UPDATE_PRIORITY:
      cogl_gst_video_sink_set_priority (sink, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
cogl_gst_video_sink_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  CoglGstVideoSink *sink = COGL_GST_VIDEO_SINK (object);
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  switch (property_id)
    {
    case PROP_UPDATE_PRIORITY:
      g_value_set_int (value, g_source_get_priority ((GSource *) priv->source));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static CoglBool
cogl_gst_video_sink_stop (GstBaseSink *base_sink)
{
  CoglGstVideoSink *sink = COGL_GST_VIDEO_SINK (base_sink);
  CoglGstVideoSinkPrivate *priv = COGL_GST_VIDEO_SINK_GET_PRIVATE (sink);

  if (priv->source)
    {
      GSource *source = (GSource *) priv->source;
      g_source_destroy (source);
      g_source_unref (source);
      priv->source = NULL;
    }

  return TRUE;
}

static void
cogl_gst_video_sink_class_init (CoglGstVideoSinkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *video_sink_class = GST_VIDEO_SINK_CLASS (klass);
  GstPadTemplate *pad_template;
  GParamSpec *pspec;

  object_class->finalize = cogl_gst_video_sink_finalize;
  object_class->dispose = cogl_gst_video_sink_dispose;
  object_class->set_property = cogl_gst_video_sink_set_property;
  object_class->get_property = cogl_gst_video_sink_get_property;

  base_sink_class->start = cogl_gst_video_sink_start;
  base_sink_class->stop = cogl_gst_video_sink_stop;
  base_sink_class->render = _cogl_gst_video_sink_render;
  base_sink_class->set_caps = cogl_gst_video_sink_set_caps;
  base_sink_class->get_caps = cogl_gst_video_sink_get_caps;

  pad_template = gst_static_pad_template_get (&sinktemplate_all);
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass), pad_template);

  gst_element_class_set_metadata (GST_ELEMENT_CLASS (klass),
                                  "Cogl video sink", "Sink/Video",
                                  "Sends video data from GStreamer to a "
                                  "Cogl pipeline",
                                  "Jonathan Matthew <jonathan@kaolin.wh9.net>, "
                                  "Matthew Allum <mallum@o-hand.com, "
                                  "Chris Lord <chris@o-hand.com>, "
                                  "Plamena Manolova "
                                  "<plamena.n.manolova@intel.com>");

  pspec = g_param_spec_int ("update-priority",
                            "Update Priority",
                            "Priority for the update source",
                            G_MININT,
                            G_MAXINT,
                            G_PRIORITY_HIGH_IDLE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_property (object_class,
                                  PROP_UPDATE_PRIORITY,
                                  pspec);

  signals[PIPELINE_READY_SIGNAL] =
    g_signal_new ("pipeline-ready",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (CoglGstVideoSinkClass, pipeline_ready),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_FRAME_SIGNAL] =
    g_signal_new ("new-frame",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (CoglGstVideoSinkClass, new_frame),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

CoglGstVideoSink *
cogl_gst_video_sink_new (CoglContext *ctx)
{
  CoglGstVideoSink *sink = g_object_new (COGL_GST_TYPE_VIDEO_SINK, NULL);
  cogl_gst_video_sink_set_context (sink, ctx);

  return sink;
}

float
cogl_gst_video_sink_get_aspect (CoglGstVideoSink *vt)
{
  GstVideoInfo *info;

  g_return_val_if_fail (COGL_GST_IS_VIDEO_SINK (vt), 0.);

  info = &vt->priv->info;
  return ((float)info->width * (float)info->par_n) /
    ((float)info->height * (float)info->par_d);
}

float
cogl_gst_video_sink_get_width_for_height (CoglGstVideoSink *vt,
                                          float height)
{
  float aspect;

  g_return_val_if_fail (COGL_GST_IS_VIDEO_SINK (vt), 0.);

  aspect = cogl_gst_video_sink_get_aspect (vt);
  return height * aspect;
}

float
cogl_gst_video_sink_get_height_for_width (CoglGstVideoSink *vt,
                                          float width)
{
  float aspect;

  g_return_val_if_fail (COGL_GST_IS_VIDEO_SINK (vt), 0.);

  aspect = cogl_gst_video_sink_get_aspect (vt);
  return width / aspect;
}

void
cogl_gst_video_sink_fit_size (CoglGstVideoSink *vt,
                              const CoglGstRectangle *available,
                              CoglGstRectangle *output)
{
  g_return_if_fail (COGL_GST_IS_VIDEO_SINK (vt));
  g_return_if_fail (available != NULL);
  g_return_if_fail (output != NULL);

  if (available->height == 0.0f)
    {
      output->x = available->x;
      output->y = available->y;
      output->width = output->height = 0;
    }
  else
    {
      float available_aspect = available->width / available->height;
      float video_aspect = cogl_gst_video_sink_get_aspect (vt);

      if (video_aspect > available_aspect)
        {
          output->width = available->width;
          output->height = available->width / video_aspect;
          output->x = available->x;
          output->y = available->y + (available->height - output->height) / 2;
        }
      else
        {
          output->width = available->height * video_aspect;
          output->height = available->height;
          output->x = available->x + (available->width - output->width) / 2;
          output->y = available->y;
        }
    }
}

void
cogl_gst_video_sink_get_natural_size (CoglGstVideoSink *vt,
                                      float *width,
                                      float *height)
{
  GstVideoInfo *info;

  g_return_if_fail (COGL_GST_IS_VIDEO_SINK (vt));

  info = &vt->priv->info;

  if (info->par_n > info->par_d)
    {
      /* To display the video at the right aspect ratio then in this
       * case the pixels need to be stretched horizontally and so we
       * use the unscaled height as our reference.
       */

      if (height)
        *height = info->height;
      if (width)
        *width = cogl_gst_video_sink_get_width_for_height (vt, info->height);
    }
  else
    {
      if (width)
        *width = info->width;
      if (height)
        *height = cogl_gst_video_sink_get_height_for_width (vt, info->width);
    }
}

float
cogl_gst_video_sink_get_natural_width (CoglGstVideoSink *vt)
{
  float width;
  cogl_gst_video_sink_get_natural_size (vt, &width, NULL);
  return width;
}

float
cogl_gst_video_sink_get_natural_height (CoglGstVideoSink *vt)
{
  float height;
  cogl_gst_video_sink_get_natural_size (vt, NULL, &height);
  return height;
}

CoglBool
cogl_gst_video_sink_is_ready (CoglGstVideoSink *sink)
{
  return !!sink->priv->renderer;
}
