/*
 *  <Short Description>
 *  Copyright (C) 2025  Brett Terpstra
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <data_loader.h>
#include <render.h>
#include <tabs/color_wheel.h>
#include <imgui.h>

extern std::optional<gpu_asset_manager> gpu_resources;
extern assets_t                         assets;

void color_wheel_t::render()
{
	// if (ImGui::BeginChild("##Content", ImVec2(0, 0)))
	// {
	// 	ImGui::BeginChild("BlocksAndImages", ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
	// 	draw_order(ordered_images);
	// 	ImGui::EndChild();
	// 	ImGui::SameLine();
	// 	ImGui::BeginGroup();
	// 	ImGui::BeginChild("##Outside", ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
	// 	ImGui::BeginChild("##WhySilly", ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
	// 	if (ImGui::ColorPicker3("##SelectBlocks",
	// 							color_picker_data.data(),
	// 							ImGuiColorEditFlags_NoInputs |
	// 							ImGuiColorEditFlags_PickerHueBar))
	// 	{
	// 		skipped_index.clear();
	// 	}
	// 	ImGui::EndChild();
	// 	if (ImGui::Button("Paste"))
	// 	{
	// 		if (auto color        = history_stack.get_color())
	// 			color_picker_data = color->as_linear_rgb().unpack();
	// 	}
	// 	auto sampler = color_source_t(blt::vec3{color_picker_data}, samples);
	//
	// 	ordered_images = make_ordering(*sampler, *comparison_interface, {});
	// 	ImGui::Text("Click the image icon to remove it from the list. This is reset when the color changes.");
	// 	draw_config_tools();
	// 	ImGui::EndChild();
	// 	ImGui::EndGroup();
	// }
	// ImGui::EndChild();
}
