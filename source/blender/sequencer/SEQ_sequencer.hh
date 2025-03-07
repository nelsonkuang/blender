/* SPDX-FileCopyrightText: 2004-2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup sequencer
 */

#include "DNA_scene_types.h"

struct BlendDataReader;
struct BlendWriter;
struct Depsgraph;
struct Editing;
struct Main;
struct MetaStack;
struct Scene;
struct SeqTimelineChannel;
struct Strip;
struct SequencerToolSettings;

constexpr int SEQ_MAX_CHANNELS = 128;

/* RNA enums, just to be more readable */
enum {
  SEQ_SIDE_MOUSE = -1,
  SEQ_SIDE_NONE = 0,
  SEQ_SIDE_LEFT,
  SEQ_SIDE_RIGHT,
  SEQ_SIDE_BOTH,
  SEQ_SIDE_NO_CHANGE,
};

/* seq_dupli' flags */
#define SEQ_DUPE_UNIQUE_NAME (1 << 0)
#define SEQ_DUPE_ALL (1 << 3) /* otherwise only selected are copied */

SequencerToolSettings *SEQ_tool_settings_init();
SequencerToolSettings *SEQ_tool_settings_ensure(Scene *scene);
void SEQ_tool_settings_free(SequencerToolSettings *tool_settings);
eSeqImageFitMethod SEQ_tool_settings_fit_method_get(Scene *scene);
void SEQ_tool_settings_fit_method_set(Scene *scene, eSeqImageFitMethod fit_method);
short SEQ_tool_settings_snap_flag_get(Scene *scene);
short SEQ_tool_settings_snap_mode_get(Scene *scene);
int SEQ_tool_settings_snap_distance_get(Scene *scene);
eSeqOverlapMode SEQ_tool_settings_overlap_mode_get(Scene *scene);
int SEQ_tool_settings_pivot_point_get(Scene *scene);
SequencerToolSettings *SEQ_tool_settings_copy(SequencerToolSettings *tool_settings);
Editing *SEQ_editing_get(const Scene *scene);
Editing *SEQ_editing_ensure(Scene *scene);
void SEQ_editing_free(Scene *scene, bool do_id_user);
/**
 * Get seqbase that is being viewed currently. This can be main seqbase or meta strip seqbase
 *
 * \param ed: sequence editor data
 * \return pointer to active seqbase. returns NULL if ed is NULL
 */
ListBase *SEQ_active_seqbase_get(const Editing *ed);
/**
 * Set seqbase that is being viewed currently. This can be main seqbase or meta strip seqbase
 *
 * \param ed: sequence editor data
 * \param seqbase: ListBase with strips
 */
void SEQ_seqbase_active_set(Editing *ed, ListBase *seqbase);
Strip *SEQ_sequence_alloc(ListBase *lb, int timeline_frame, int machine, int type);
void SEQ_sequence_free(Scene *scene, Strip *seq);
/**
 * Get #MetaStack that corresponds to current level that is being viewed
 *
 * \return pointer to meta stack
 */
MetaStack *SEQ_meta_stack_active_get(const Editing *ed);
/**
 * Open Meta strip content for editing.
 *
 * \param ed: sequence editor data
 * \param seqm: meta sequence or NULL for top level view
 */
void SEQ_meta_stack_set(const Scene *scene, Strip *dst_seq);
/**
 * Close last Meta strip open for editing.
 *
 * \param ed: sequence editor data
 */
Strip *SEQ_meta_stack_pop(Editing *ed);
Strip *SEQ_sequence_dupli_recursive(
    const Scene *scene_src, Scene *scene_dst, ListBase *new_seq_list, Strip *seq, int dupe_flag);
void SEQ_sequence_base_dupli_recursive(const Scene *scene_src,
                                       Scene *scene_dst,
                                       ListBase *nseqbase,
                                       const ListBase *seqbase,
                                       int dupe_flag,
                                       int flag);
bool SEQ_is_valid_strip_channel(const Strip *seq);

/**
 * Read and Write functions for `.blend` file data.
 */
void SEQ_blend_write(BlendWriter *writer, ListBase *seqbase);
void SEQ_blend_read(BlendDataReader *reader, ListBase *seqbase);

void SEQ_doversion_250_sound_proxy_update(Main *bmain, Editing *ed);

/* Depsgraph update function. */

/**
 * Evaluate parts of sequences which needs to be done as a part of a dependency graph evaluation.
 * This does NOT include actual rendering of the strips, but rather makes them up-to-date for
 * animation playback and makes them ready for the sequencer's rendering pipeline to render them.
 */
void SEQ_eval_sequences(Depsgraph *depsgraph, Scene *scene, ListBase *seqbase);

/**
 * Find a sequence with a given name.
 * If lookup hash doesn't exist, it will be created. If hash is tagged as invalid, it will be
 * rebuilt.
 *
 * \param scene: scene that owns lookup hash
 * \param key: Sequence name without SQ prefix (seq->name + 2)
 *
 * \return pointer to Sequence
 */
Strip *SEQ_sequence_lookup_seq_by_name(const Scene *scene, const char *key);

/**
 * Find which meta strip the given timeline channel belongs to. Returns nullptr if it is a global
 * channel.
 */
Strip *SEQ_sequence_lookup_owner_by_channel(const Scene *scene, const SeqTimelineChannel *channel);

/**
 * Free lookup hash data.
 *
 * \param scene: scene that owns lookup hash
 */
void SEQ_sequence_lookup_free(const Scene *scene);

/**
 * Mark sequence lookup as invalid (i.e. will need rebuilding).
 */
void SEQ_sequence_lookup_invalidate(const Scene *scene);
