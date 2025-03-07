/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 * SPDX-FileCopyrightText: 2003-2009 Blender Authors
 * SPDX-FileCopyrightText: 2005-2006 Peter Schlaile <peter [at] schlaile [dot] de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include "MEM_guardedalloc.h"

#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"
#include "DNA_space_types.h"

#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_path_utils.hh"
#include "BLI_string.h"

#ifdef WIN32
#  include "BLI_winstuff.h"
#else
#  include <unistd.h>
#endif

#include "BKE_global.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_scene.hh"

#include "WM_types.hh"

#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_metadata.hh"

#include "MOV_read.hh"

#include "SEQ_proxy.hh"
#include "SEQ_relations.hh"
#include "SEQ_render.hh"
#include "SEQ_sequencer.hh"
#include "SEQ_time.hh"

#include "multiview.hh"
#include "proxy.hh"
#include "render.hh"
#include "sequencer.hh"
#include "utils.hh"

struct SeqIndexBuildContext {
  MovieProxyBuilder *proxy_builder;

  int tc_flags;
  int size_flags;
  int quality;
  bool overwrite;
  int view_id;

  Main *bmain;
  Depsgraph *depsgraph;
  Scene *scene;
  Strip *seq, *orig_seq;
  SessionUID orig_seq_uid;
};

int SEQ_rendersize_to_proxysize(int render_size)
{
  switch (render_size) {
    case SEQ_RENDER_SIZE_PROXY_25:
      return IMB_PROXY_25;
    case SEQ_RENDER_SIZE_PROXY_50:
      return IMB_PROXY_50;
    case SEQ_RENDER_SIZE_PROXY_75:
      return IMB_PROXY_75;
    case SEQ_RENDER_SIZE_PROXY_100:
      return IMB_PROXY_100;
  }
  return IMB_PROXY_NONE;
}

double SEQ_rendersize_to_scale_factor(int render_size)
{
  switch (render_size) {
    case SEQ_RENDER_SIZE_PROXY_25:
      return 0.25;
    case SEQ_RENDER_SIZE_PROXY_50:
      return 0.50;
    case SEQ_RENDER_SIZE_PROXY_75:
      return 0.75;
  }
  return 1.0;
}

bool seq_proxy_get_custom_file_filepath(Strip *seq, char *filepath, const int view_id)
{
  /* Ideally this would be #PROXY_MAXFILE however BLI_path_abs clamps to #FILE_MAX. */
  char filepath_temp[FILE_MAX];
  char suffix[24];
  StripProxy *proxy = seq->data->proxy;

  if (proxy == nullptr) {
    return false;
  }

  BLI_path_join(filepath_temp, sizeof(filepath_temp), proxy->dirpath, proxy->filename);
  BLI_path_abs(filepath_temp, BKE_main_blendfile_path_from_global());

  if (view_id > 0) {
    SNPRINTF(suffix, "_%d", view_id);
    /* TODO(sergey): This will actually append suffix after extension
     * which is weird but how was originally coded in multi-view branch.
     */
    BLI_snprintf(filepath, PROXY_MAXFILE, "%s_%s", filepath_temp, suffix);
  }
  else {
    BLI_strncpy(filepath, filepath_temp, PROXY_MAXFILE);
  }

  return true;
}

static bool seq_proxy_get_filepath(Scene *scene,
                                   Strip *seq,
                                   int timeline_frame,
                                   eSpaceSeq_Proxy_RenderSize render_size,
                                   char *filepath,
                                   const int view_id)
{
  char dirpath[PROXY_MAXFILE];
  char suffix[24] = {'\0'};
  Editing *ed = SEQ_editing_get(scene);
  StripProxy *proxy = seq->data->proxy;

  if (proxy == nullptr) {
    return false;
  }

  /* Multi-view suffix. */
  if (view_id > 0) {
    SNPRINTF(suffix, "_%d", view_id);
  }

  /* Per strip with Custom file situation is handled separately. */
  if (proxy->storage & SEQ_STORAGE_PROXY_CUSTOM_FILE &&
      ed->proxy_storage != SEQ_EDIT_PROXY_DIR_STORAGE)
  {
    if (seq_proxy_get_custom_file_filepath(seq, filepath, view_id)) {
      return true;
    }
  }

  if (ed->proxy_storage == SEQ_EDIT_PROXY_DIR_STORAGE) {
    /* Per project default. */
    if (ed->proxy_dir[0] == 0) {
      STRNCPY(dirpath, "//BL_proxy");
    }
    else { /* Per project with custom dirpath. */
      STRNCPY(dirpath, ed->proxy_dir);
    }
    BLI_path_abs(filepath, BKE_main_blendfile_path_from_global());
  }
  else {
    /* Pre strip with custom dir. */
    if (proxy->storage & SEQ_STORAGE_PROXY_CUSTOM_DIR) {
      STRNCPY(dirpath, seq->data->proxy->dirpath);
    }
    else { /* Per strip default. */
      SNPRINTF(dirpath, "%s" SEP_STR "BL_proxy", seq->data->dirpath);
    }
  }

  /* Proxy size number to be used in path. */
  int proxy_size_number = SEQ_rendersize_to_scale_factor(render_size) * 100;

  BLI_snprintf(filepath,
               PROXY_MAXFILE,
               "%s" SEP_STR "images" SEP_STR "%d" SEP_STR "%s_proxy%s.jpg",
               dirpath,
               proxy_size_number,
               SEQ_render_give_stripelem(scene, seq, timeline_frame)->filename,
               suffix);
  BLI_path_abs(filepath, BKE_main_blendfile_path_from_global());
  return true;
}

bool SEQ_can_use_proxy(const SeqRenderData *context, const Strip *seq, int psize)
{
  if (seq->data->proxy == nullptr || !context->use_proxies) {
    return false;
  }

  short size_flags = seq->data->proxy->build_size_flags;
  return (seq->flag & SEQ_USE_PROXY) != 0 && psize != IMB_PROXY_NONE && (size_flags & psize) != 0;
}

ImBuf *seq_proxy_fetch(const SeqRenderData *context, Strip *seq, int timeline_frame)
{
  char filepath[PROXY_MAXFILE];
  StripProxy *proxy = seq->data->proxy;
  const eSpaceSeq_Proxy_RenderSize psize = eSpaceSeq_Proxy_RenderSize(
      context->preview_render_size);
  StripAnim *sanim;

  /* only use proxies, if they are enabled (even if present!) */
  if (!SEQ_can_use_proxy(context, seq, SEQ_rendersize_to_proxysize(psize))) {
    return nullptr;
  }

  if (proxy->storage & SEQ_STORAGE_PROXY_CUSTOM_FILE) {
    int frameno = round_fl_to_int(SEQ_give_frame_index(context->scene, seq, timeline_frame)) +
                  seq->anim_startofs;
    if (proxy->anim == nullptr) {
      if (seq_proxy_get_filepath(
              context->scene, seq, timeline_frame, psize, filepath, context->view_id) == 0)
      {
        return nullptr;
      }

      proxy->anim = openanim(filepath, IB_rect, 0, seq->data->colorspace_settings.name);
    }
    if (proxy->anim == nullptr) {
      return nullptr;
    }

    seq_open_anim_file(context->scene, seq, true);
    sanim = static_cast<StripAnim *>(seq->anims.first);

    frameno = MOV_calc_frame_index_with_timecode(
        sanim ? sanim->anim : nullptr, IMB_Timecode_Type(seq->data->proxy->tc), frameno);

    return MOV_decode_frame(proxy->anim, frameno, IMB_TC_NONE, IMB_PROXY_NONE);
  }

  if (seq_proxy_get_filepath(
          context->scene, seq, timeline_frame, psize, filepath, context->view_id) == 0)
  {
    return nullptr;
  }

  if (BLI_exists(filepath)) {
    ImBuf *ibuf = IMB_loadiffname(filepath, IB_rect | IB_metadata, nullptr);

    if (ibuf) {
      seq_imbuf_assign_spaces(context->scene, ibuf);
    }

    return ibuf;
  }

  return nullptr;
}

static void seq_proxy_build_frame(const SeqRenderData *context,
                                  SeqRenderState *state,
                                  Strip *seq,
                                  int timeline_frame,
                                  int proxy_render_size,
                                  const bool overwrite)
{
  char filepath[PROXY_MAXFILE];
  ImBuf *ibuf_tmp, *ibuf;
  Scene *scene = context->scene;

  if (!seq_proxy_get_filepath(scene,
                              seq,
                              timeline_frame,
                              eSpaceSeq_Proxy_RenderSize(proxy_render_size),
                              filepath,
                              context->view_id))
  {
    return;
  }

  if (!overwrite && BLI_exists(filepath)) {
    return;
  }

  ibuf_tmp = seq_render_strip(context, state, seq, timeline_frame);

  int rectx = (proxy_render_size * ibuf_tmp->x) / 100;
  int recty = (proxy_render_size * ibuf_tmp->y) / 100;

  if (ibuf_tmp->x != rectx || ibuf_tmp->y != recty) {
    ibuf = IMB_scale_into_new(ibuf_tmp, rectx, recty, IMBScaleFilter::Nearest, true);
    IMB_freeImBuf(ibuf_tmp);
  }
  else {
    ibuf = ibuf_tmp;
  }

  const int quality = seq->data->proxy->quality;
  const bool save_float = ibuf->float_buffer.data != nullptr;
  ibuf->foptions.quality = quality;
  if (save_float) {
    /* Float image: save as EXR with FP16 data and DWAA compression. */
    ibuf->ftype = IMB_FTYPE_OPENEXR;
    ibuf->foptions.flag = OPENEXR_HALF | R_IMF_EXR_CODEC_DWAA;
  }
  else {
    /* Byte image: save as JPG. */
    ibuf->ftype = IMB_FTYPE_JPG;
    if (ibuf->planes == 32) {
      ibuf->planes = 24; /* JPGs do not support alpha. */
    }
  }
  BLI_file_ensure_parent_dir_exists(filepath);

  const bool ok = IMB_saveiff(ibuf, filepath, save_float ? IB_rectfloat : IB_rect);
  if (ok == false) {
    perror(filepath);
  }

  IMB_freeImBuf(ibuf);
}

/**
 * Cache the result of #BKE_scene_multiview_view_prefix_get.
 */
struct MultiViewPrefixVars {
  char prefix[FILE_MAX];
  const char *ext;
};

/**
 * Returns whether the file this context would read from even exist,
 * if not, don't create the context.
 *
 * \param prefix_vars: Stores prefix variables for reuse,
 * these variables are for internal use, the caller must not depend on them.
 *
 * \note This function must first a `view_id` of zero, to initialize `prefix_vars`
 * for use with other views.
 */
static bool seq_proxy_multiview_context_invalid(Strip *seq,
                                                Scene *scene,
                                                const int view_id,
                                                MultiViewPrefixVars *prefix_vars)
{
  if ((scene->r.scemode & R_MULTIVIEW) == 0) {
    return false;
  }

  if ((seq->type == SEQ_TYPE_IMAGE) && (seq->views_format == R_IMF_VIEWS_INDIVIDUAL)) {
    if (view_id == 0) {
      /* Clear on first use. */
      prefix_vars->prefix[0] = '\0';
      prefix_vars->ext = nullptr;

      char filepath[FILE_MAX];
      BLI_path_join(
          filepath, sizeof(filepath), seq->data->dirpath, seq->data->stripdata->filename);
      BLI_path_abs(filepath, BKE_main_blendfile_path_from_global());
      BKE_scene_multiview_view_prefix_get(scene, filepath, prefix_vars->prefix, &prefix_vars->ext);
    }

    if (prefix_vars->prefix[0] == '\0') {
      return view_id != 0;
    }

    char filepath[FILE_MAX];
    seq_multiview_name(scene, view_id, prefix_vars->prefix, prefix_vars->ext, filepath, FILE_MAX);
    if (BLI_access(filepath, R_OK) == 0) {
      return false;
    }

    return view_id != 0;
  }
  return false;
}

/**
 * This returns the maximum possible number of required contexts
 */
static int seq_proxy_context_count(Strip *seq, Scene *scene)
{
  int num_views = 1;

  if ((scene->r.scemode & R_MULTIVIEW) == 0) {
    return 1;
  }

  switch (seq->type) {
    case SEQ_TYPE_MOVIE: {
      num_views = BLI_listbase_count(&seq->anims);
      break;
    }
    case SEQ_TYPE_IMAGE: {
      switch (seq->views_format) {
        case R_IMF_VIEWS_INDIVIDUAL:
          num_views = BKE_scene_multiview_num_views_get(&scene->r);
          break;
        case R_IMF_VIEWS_STEREO_3D:
          num_views = 2;
          break;
        case R_IMF_VIEWS_MULTIVIEW:
        /* not supported at the moment */
        /* pass through */
        default:
          num_views = 1;
      }
      break;
    }
  }

  return num_views;
}

static bool seq_proxy_need_rebuild(Strip *seq, MovieReader *anim)
{
  if ((seq->data->proxy->build_flags & SEQ_PROXY_SKIP_EXISTING) == 0) {
    return true;
  }

  IMB_Proxy_Size required_proxies = IMB_Proxy_Size(seq->data->proxy->build_size_flags);
  int built_proxies = MOV_get_existing_proxies(anim);
  return (required_proxies & built_proxies) != required_proxies;
}

bool SEQ_proxy_rebuild_context(Main *bmain,
                               Depsgraph *depsgraph,
                               Scene *scene,
                               Strip *seq,
                               blender::Set<std::string> *processed_paths,
                               ListBase *queue,
                               bool build_only_on_bad_performance)
{
  SeqIndexBuildContext *context;
  Strip *nseq;
  LinkData *link;
  int num_files;
  int i;

  if (!seq->data || !seq->data->proxy) {
    return true;
  }

  if (!(seq->flag & SEQ_USE_PROXY)) {
    return true;
  }

  num_files = seq_proxy_context_count(seq, scene);

  MultiViewPrefixVars prefix_vars; /* Initialized by #seq_proxy_multiview_context_invalid. */
  for (i = 0; i < num_files; i++) {
    if (seq_proxy_multiview_context_invalid(seq, scene, i, &prefix_vars)) {
      continue;
    }

    /* Check if proxies are already built here, because actually opening anims takes a lot of
     * time. */
    seq_open_anim_file(scene, seq, false);
    StripAnim *sanim = static_cast<StripAnim *>(BLI_findlink(&seq->anims, i));
    if (sanim->anim && !seq_proxy_need_rebuild(seq, sanim->anim)) {
      continue;
    }

    SEQ_relations_sequence_free_anim(seq);

    context = static_cast<SeqIndexBuildContext *>(
        MEM_callocN(sizeof(SeqIndexBuildContext), "seq proxy rebuild context"));

    nseq = SEQ_sequence_dupli_recursive(scene, scene, nullptr, seq, 0);

    context->tc_flags = nseq->data->proxy->build_tc_flags;
    context->size_flags = nseq->data->proxy->build_size_flags;
    context->quality = nseq->data->proxy->quality;
    context->overwrite = (nseq->data->proxy->build_flags & SEQ_PROXY_SKIP_EXISTING) == 0;

    context->bmain = bmain;
    context->depsgraph = depsgraph;
    context->scene = scene;
    context->orig_seq = seq;
    context->orig_seq_uid = seq->runtime.session_uid;
    context->seq = nseq;

    context->view_id = i; /* only for images */

    if (nseq->type == SEQ_TYPE_MOVIE) {
      seq_open_anim_file(scene, nseq, true);
      sanim = static_cast<StripAnim *>(BLI_findlink(&nseq->anims, i));

      if (sanim->anim) {
        context->proxy_builder = MOV_proxy_builder_start(sanim->anim,
                                                         IMB_Timecode_Type(context->tc_flags),
                                                         context->size_flags,
                                                         context->quality,
                                                         context->overwrite,
                                                         processed_paths,
                                                         build_only_on_bad_performance);
      }
      if (!context->proxy_builder) {
        MEM_freeN(context);
        return false;
      }
    }

    link = BLI_genericNodeN(context);
    BLI_addtail(queue, link);
  }

  return true;
}

void SEQ_proxy_rebuild(SeqIndexBuildContext *context, wmJobWorkerStatus *worker_status)
{
  const bool overwrite = context->overwrite;
  SeqRenderData render_context;
  Strip *seq = context->seq;
  Scene *scene = context->scene;
  Main *bmain = context->bmain;
  int timeline_frame;

  if (seq->type == SEQ_TYPE_MOVIE) {
    if (context->proxy_builder) {
      MOV_proxy_builder_process(context->proxy_builder,
                                &worker_status->stop,
                                &worker_status->do_update,
                                &worker_status->progress);
    }

    return;
  }

  if (!(seq->flag & SEQ_USE_PROXY)) {
    return;
  }

  /* that's why it is called custom... */
  if (seq->data->proxy && seq->data->proxy->storage & SEQ_STORAGE_PROXY_CUSTOM_FILE) {
    return;
  }

  /* fail safe code */
  int width, height;
  BKE_render_resolution(&scene->r, false, &width, &height);

  SEQ_render_new_render_data(
      bmain, context->depsgraph, context->scene, width, height, 100, false, &render_context);

  render_context.skip_cache = true;
  render_context.is_proxy_render = true;
  render_context.view_id = context->view_id;

  SeqRenderState state;

  for (timeline_frame = SEQ_time_left_handle_frame_get(scene, seq);
       timeline_frame < SEQ_time_right_handle_frame_get(scene, seq);
       timeline_frame++)
  {
    if (context->size_flags & IMB_PROXY_25) {
      seq_proxy_build_frame(&render_context, &state, seq, timeline_frame, 25, overwrite);
    }
    if (context->size_flags & IMB_PROXY_50) {
      seq_proxy_build_frame(&render_context, &state, seq, timeline_frame, 50, overwrite);
    }
    if (context->size_flags & IMB_PROXY_75) {
      seq_proxy_build_frame(&render_context, &state, seq, timeline_frame, 75, overwrite);
    }
    if (context->size_flags & IMB_PROXY_100) {
      seq_proxy_build_frame(&render_context, &state, seq, timeline_frame, 100, overwrite);
    }

    worker_status->progress = float(timeline_frame - SEQ_time_left_handle_frame_get(scene, seq)) /
                              (SEQ_time_right_handle_frame_get(scene, seq) -
                               SEQ_time_left_handle_frame_get(scene, seq));
    worker_status->do_update = true;

    if (worker_status->stop || G.is_break) {
      break;
    }
  }
}

void SEQ_proxy_rebuild_finish(SeqIndexBuildContext *context, bool stop)
{
  if (context->proxy_builder) {
    LISTBASE_FOREACH (StripAnim *, sanim, &context->seq->anims) {
      MOV_close_proxies(sanim->anim);
    }

    MOV_proxy_builder_finish(context->proxy_builder, stop);
  }

  seq_free_sequence_recurse(nullptr, context->seq, true);

  MEM_freeN(context);
}

void SEQ_proxy_set(Strip *seq, bool value)
{
  if (value) {
    seq->flag |= SEQ_USE_PROXY;
    if (seq->data->proxy == nullptr) {
      seq->data->proxy = seq_strip_proxy_alloc();
    }
  }
  else {
    seq->flag &= ~SEQ_USE_PROXY;
  }
}

void seq_proxy_index_dir_set(MovieReader *anim, const char *base_dir)
{
  char dirname[FILE_MAX];
  char filename[FILE_MAXFILE];

  MOV_get_filename(anim, filename, FILE_MAXFILE);
  BLI_path_join(dirname, sizeof(dirname), base_dir, filename);
  MOV_set_custom_proxy_dir(anim, dirname);
}

void free_proxy_seq(Strip *seq)
{
  if (seq->data && seq->data->proxy && seq->data->proxy->anim) {
    MOV_close(seq->data->proxy->anim);
    seq->data->proxy->anim = nullptr;
  }
}
