/* SPDX-FileCopyrightText: 2006 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup cmpnodes
 */

#include "GPU_material.hh"

#include "COM_shader_node.hh"

#include "node_composite_util.hh"

/* **************** SEPARATE YUVA ******************** */

namespace blender::nodes::node_composite_separate_yuva_cc {

static void cmp_node_sepyuva_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Image")
      .default_value({1.0f, 1.0f, 1.0f, 1.0f})
      .compositor_domain_priority(0);
  b.add_output<decl::Float>("Y").translation_context(BLT_I18NCONTEXT_COLOR);
  b.add_output<decl::Float>("U").translation_context(BLT_I18NCONTEXT_COLOR);
  b.add_output<decl::Float>("V").translation_context(BLT_I18NCONTEXT_COLOR);
  b.add_output<decl::Float>("A").translation_context(BLT_I18NCONTEXT_COLOR);
}

using namespace blender::compositor;

class SeparateYUVAShaderNode : public ShaderNode {
 public:
  using ShaderNode::ShaderNode;

  void compile(GPUMaterial *material) override
  {
    GPUNodeStack *inputs = get_inputs_array();
    GPUNodeStack *outputs = get_outputs_array();

    GPU_stack_link(material, &bnode(), "node_composite_separate_yuva_itu_709", inputs, outputs);
  }
};

static ShaderNode *get_compositor_shader_node(DNode node)
{
  return new SeparateYUVAShaderNode(node);
}

}  // namespace blender::nodes::node_composite_separate_yuva_cc

void register_node_type_cmp_sepyuva()
{
  namespace file_ns = blender::nodes::node_composite_separate_yuva_cc;

  static blender::bke::bNodeType ntype;

  cmp_node_type_base(
      &ntype, CMP_NODE_SEPYUVA_LEGACY, "Separate YUVA (Legacy)", NODE_CLASS_CONVERTER);
  ntype.enum_name_legacy = "SEPYUVA";
  ntype.declare = file_ns::cmp_node_sepyuva_declare;
  ntype.gather_link_search_ops = nullptr;
  ntype.get_compositor_shader_node = file_ns::get_compositor_shader_node;

  blender::bke::node_register_type(&ntype);
}

/* **************** COMBINE YUVA ******************** */

namespace blender::nodes::node_composite_combine_yuva_cc {

static void cmp_node_combyuva_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Y")
      .min(0.0f)
      .max(1.0f)
      .compositor_domain_priority(0)
      .translation_context(BLT_I18NCONTEXT_COLOR);
  b.add_input<decl::Float>("U")
      .min(0.0f)
      .max(1.0f)
      .compositor_domain_priority(1)
      .translation_context(BLT_I18NCONTEXT_COLOR);
  b.add_input<decl::Float>("V")
      .min(0.0f)
      .max(1.0f)
      .compositor_domain_priority(2)
      .translation_context(BLT_I18NCONTEXT_COLOR);
  b.add_input<decl::Float>("A")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .compositor_domain_priority(3)
      .translation_context(BLT_I18NCONTEXT_COLOR);
  b.add_output<decl::Color>("Image");
}

using namespace blender::compositor;

class CombineYUVAShaderNode : public ShaderNode {
 public:
  using ShaderNode::ShaderNode;

  void compile(GPUMaterial *material) override
  {
    GPUNodeStack *inputs = get_inputs_array();
    GPUNodeStack *outputs = get_outputs_array();

    GPU_stack_link(material, &bnode(), "node_composite_combine_yuva_itu_709", inputs, outputs);
  }
};

static ShaderNode *get_compositor_shader_node(DNode node)
{
  return new CombineYUVAShaderNode(node);
}

}  // namespace blender::nodes::node_composite_combine_yuva_cc

void register_node_type_cmp_combyuva()
{
  namespace file_ns = blender::nodes::node_composite_combine_yuva_cc;

  static blender::bke::bNodeType ntype;

  cmp_node_type_base(
      &ntype, CMP_NODE_COMBYUVA_LEGACY, "Combine YUVA (Legacy)", NODE_CLASS_CONVERTER);
  ntype.enum_name_legacy = "COMBYUVA";
  ntype.declare = file_ns::cmp_node_combyuva_declare;
  ntype.gather_link_search_ops = nullptr;
  ntype.get_compositor_shader_node = file_ns::get_compositor_shader_node;

  blender::bke::node_register_type(&ntype);
}
