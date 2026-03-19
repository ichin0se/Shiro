#!/usr/bin/env python3

import argparse
import os
import sys

import hou

try:
    import loputils
except ImportError as exc:
    raise SystemExit(f"Failed to import loputils: {exc}")


RENDER_SETTINGS_PRIM_PATH = "/Render/Settings/Shiro"
RENDER_PRODUCT_PRIM_PATH = "/Render/Products/ShiroBeauty"
RENDER_VARS_PARENT_PATH = "/Render/Products/Vars"
DEFAULT_PRODUCT_PATH = "$HIP/render/shiro.$F4.exr"


def _set_parm(node, name, value):
    parm = node.parm(name)
    if parm is not None:
        parm.set(value)


def _make_passthrough_subnet(parent, name):
    subnet = parent.createNode("subnet", name)
    indirect_input = subnet.indirectInputs()[0]
    input_node = subnet.createNode("null", "IN")
    input_node.setInput(0, indirect_input)
    output_node = subnet.node("output0")
    return subnet, input_node, output_node


def _link_matching_parms(outer_node, inner_node):
    for inner_parm_tuple in inner_node.parmTuples():
        outer_parm_tuple = outer_node.parmTuple(inner_parm_tuple.name())
        if outer_parm_tuple is None:
            continue
        try:
            inner_parm_tuple.set(outer_parm_tuple)
        except (hou.OperationFailed, hou.PermissionError):
            continue


def _create_wrapper_asset(parent, library_path, asset_name, label, inner_type_name, configure_inner):
    asset_node_name = asset_name.split("::", 1)[0]
    subnet, input_node, output_node = _make_passthrough_subnet(parent, asset_node_name)
    inner_node = subnet.createNode(inner_type_name, inner_type_name)
    inner_node.setInput(0, input_node)
    configure_inner(inner_node)
    output_node.setInput(0, inner_node)

    subnet.setParmTemplateGroup(inner_node.parmTemplateGroup())
    _link_matching_parms(subnet, inner_node)
    outer_parm_group = subnet.parmTemplateGroup()
    subnet.layoutChildren()

    asset = subnet.createDigitalAsset(
        name=asset_name,
        hda_file_name=library_path,
        description=label,
        min_num_inputs=0,
        max_num_inputs=1,
    )
    _finalize_asset_definition(asset, outer_parm_group)
    asset.destroy()


def _configure_shiro_render_settings(node):
    _set_parm(node, "primpath", RENDER_SETTINGS_PRIM_PATH)
    _set_parm(node, "createprims", "on")
    _set_parm(node, "products_control", "set")
    _set_parm(node, "products", RENDER_PRODUCT_PRIM_PATH)
    _set_parm(node, "camera_control", "set")
    loputils.addRendererParmFolders(node, "Global")


def _configure_shiro_render_product(node):
    _set_parm(node, "primpath", RENDER_PRODUCT_PRIM_PATH)
    _set_parm(node, "createprims", "on")
    _set_parm(node, "productName_control", "set")
    _set_parm(node, "productName", DEFAULT_PRODUCT_PATH)
    _set_parm(node, "orderedVars_control", "set")
    _set_parm(
        node,
        "orderedVars",
        " ".join(
            (
                f"{RENDER_VARS_PARENT_PATH}/Beauty",
                f"{RENDER_VARS_PARENT_PATH}/Albedo",
                f"{RENDER_VARS_PARENT_PATH}/Normal",
                f"{RENDER_VARS_PARENT_PATH}/Depth",
            )
        ),
    )
    _set_parm(node, "camera_control", "set")


def _configure_shiro_additional_rendervars(node):
    _set_parm(node, "parentprimpath", RENDER_VARS_PARENT_PATH)


def _set_string_expression(node, parm_name, expression):
    parm = node.parm(parm_name)
    if parm is not None:
        parm.setExpression(expression, hou.exprLanguage.Hscript)


def _set_python_expression(node, parm_name, expression):
    parm = node.parm(parm_name)
    if parm is not None:
        parm.setExpression(expression, hou.exprLanguage.Python)


def _build_standard_rendervars_interface():
    parm_group = hou.ParmTemplateGroup()

    common = hou.FolderParmTemplate("common", "Common")
    common.addParmTemplate(
        hou.StringParmTemplate(
            "parentprimpath",
            "Parent Primitive Path",
            1,
            default_value=(RENDER_VARS_PARENT_PATH,),
        )
    )

    primary = hou.FolderParmTemplate("primary", "Primary")
    primary.addParmTemplate(
        hou.ToggleParmTemplate("beauty_enabled", "Beauty", default_value=True)
    )
    primary.addParmTemplate(
        hou.StringParmTemplate(
            "beauty_name",
            "Beauty Prim Name",
            1,
            default_value=("Beauty",),
        )
    )

    utility = hou.FolderParmTemplate("utility", "Utility")
    utility.addParmTemplate(
        hou.ToggleParmTemplate("albedo_enabled", "Albedo", default_value=True)
    )
    utility.addParmTemplate(
        hou.StringParmTemplate(
            "albedo_name",
            "Albedo Prim Name",
            1,
            default_value=("Albedo",),
        )
    )
    utility.addParmTemplate(
        hou.ToggleParmTemplate("normal_enabled", "Normal", default_value=True)
    )
    utility.addParmTemplate(
        hou.StringParmTemplate(
            "normal_name",
            "Normal Prim Name",
            1,
            default_value=("Normal",),
        )
    )
    utility.addParmTemplate(
        hou.ToggleParmTemplate("depth_enabled", "Depth", default_value=True)
    )
    utility.addParmTemplate(
        hou.StringParmTemplate(
            "depth_name",
            "Depth Prim Name",
            1,
            default_value=("Depth",),
        )
    )

    parm_group.append(common)
    parm_group.append(primary)
    parm_group.append(utility)
    return parm_group


def _configure_rendervar_node(node, toggle_parm, name_parm, data_type, source_name, aov_name):
    _set_parm(node, "createprims", "on")
    _set_parm(node, "sourceType_control", "set")
    _set_parm(node, "sourceType", "raw")
    _set_parm(node, "sourceName_control", "set")
    _set_parm(node, "sourceName", source_name)
    _set_parm(node, "dataType_control", "set")
    _set_parm(node, "dataType", data_type)
    _set_parm(node, "xn__driverparametersaovname_control_krbkd", "set")
    _set_parm(node, "xn__driverparametersaovname_jebkd", aov_name)
    _set_string_expression(node, "createprims", f'if(ch("../{toggle_parm}"), 1, 0)')
    _set_python_expression(
        node,
        "primpath",
        'hou.node("..").parm("parentprimpath").evalAsString()'
        f' + "/" + hou.node("..").parm("{name_parm}").evalAsString()',
    )


def _create_standard_rendervars_asset(parent, library_path):
    subnet, input_node, output_node = _make_passthrough_subnet(parent, "shirostandardrendervars")
    outer_parm_group = _build_standard_rendervars_interface()
    subnet.setParmTemplateGroup(outer_parm_group)

    beauty = subnet.createNode("rendervar", "beauty")
    beauty.setInput(0, input_node)
    _configure_rendervar_node(
        beauty,
        toggle_parm="beauty_enabled",
        name_parm="beauty_name",
        data_type="color4f",
        source_name="color",
        aov_name="rgba",
    )

    albedo = subnet.createNode("rendervar", "albedo")
    albedo.setInput(0, beauty)
    _configure_rendervar_node(
        albedo,
        toggle_parm="albedo_enabled",
        name_parm="albedo_name",
        data_type="color3f",
        source_name="albedo",
        aov_name="albedo",
    )

    normal = subnet.createNode("rendervar", "normal")
    normal.setInput(0, albedo)
    _configure_rendervar_node(
        normal,
        toggle_parm="normal_enabled",
        name_parm="normal_name",
        data_type="normal3f",
        source_name="normal",
        aov_name="normal",
    )

    depth = subnet.createNode("rendervar", "depth")
    depth.setInput(0, normal)
    _configure_rendervar_node(
        depth,
        toggle_parm="depth_enabled",
        name_parm="depth_name",
        data_type="float",
        source_name="depth",
        aov_name="depth",
    )

    output_node.setInput(0, depth)
    subnet.layoutChildren()

    asset = subnet.createDigitalAsset(
        name="shirostandardrendervars::1.0",
        hda_file_name=library_path,
        description="Shiro Standard Render Vars",
        min_num_inputs=0,
        max_num_inputs=1,
    )
    _finalize_asset_definition(asset, outer_parm_group)
    asset.destroy()


def _finalize_asset_definition(asset, parm_group):
    definition = asset.type().definition()
    options = definition.options()
    options.setSaveSpareParms(True)
    definition.setOptions(options)
    definition.setParmTemplateGroup(parm_group)
    asset.setParmTemplateGroup(parm_group)
    definition.updateFromNode(asset)


def _generate_assets(library_path):
    stage = hou.node("/stage")
    if stage is None:
        raise RuntimeError("Expected /stage network to exist in Houdini session")

    _create_wrapper_asset(
        stage,
        library_path,
        "shiro::1.0",
        "Shiro",
        "rendersettings",
        _configure_shiro_render_settings,
    )
    _create_wrapper_asset(
        stage,
        library_path,
        "shirorenderproperties::1.0",
        "Shiro Render Settings",
        "rendersettings",
        _configure_shiro_render_settings,
    )
    _create_wrapper_asset(
        stage,
        library_path,
        "shirorenderproducts::1.0",
        "Shiro Render Products",
        "renderproduct",
        _configure_shiro_render_product,
    )
    _create_standard_rendervars_asset(stage, library_path)
    _create_wrapper_asset(
        stage,
        library_path,
        "shiroadditionalrendervars::1.0",
        "Shiro Additional Render Vars",
        "additionalrendervars",
        _configure_shiro_additional_rendervars,
    )


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, help="Output .hda library path")
    args = parser.parse_args(argv)

    output_path = os.path.abspath(args.output)
    output_dir = os.path.dirname(output_path)
    os.makedirs(output_dir, exist_ok=True)
    if os.path.exists(output_path):
        os.remove(output_path)

    _generate_assets(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
