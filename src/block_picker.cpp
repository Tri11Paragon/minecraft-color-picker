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
#include <asset_loader.h>
#include <imgui.h>
#include <block_picker.h>
#include <blt/std/string.h>
#include <blt/gfx/window.h>
#include <blt/math/log_util.h>

std::optional<size_t> draw_block_list(const std::vector<block_picker_data_t>& block_textures, const bool selectable,
									const std::optional<std::string>& filter, const int icons_per_row, const blt::vec2& icon_size)
{
	std::stringstream ss;
	ss << "##block_display_";
	ss << block_textures.size();
	ss << '_';
	ss << icons_per_row;
	ss << '_';
	ss << icon_size;
	if (ImGui::BeginTable(ss.str().c_str(), icons_per_row, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg))
	{
		for (const auto& [i, tuple] : blt::enumerate(block_textures))
		{
			auto [name, texture] = tuple;
			if (filter)
			{
				auto lower = blt::string::toLowerCase(block_pretty_name(name));
				auto lower_filter = blt::string::toLowerCase(*filter);
				if (!blt::string::contains(lower, lower_filter))
					continue;
			}

			if (selectable)
			{
				if (ImGui::ImageButton(block_pretty_name(name).c_str(), texture->texture->getTextureID(), ImVec2{icon_size[0], icon_size[1]}))
				{
					ImGui::EndTable();
					return i;
				}
			} else
				ImGui::Image(texture->texture->getTextureID(), ImVec2{icon_size[0], icon_size[1]});

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", block_pretty_name(name).c_str());

			ImGui::TableNextColumn();
		}

		ImGui::EndTable();
	}
	return {};
}

std::optional<size_t> show_block_picker(const blt::vec2& pos, const std::vector<block_picker_data_t>& block_textures, const int icons_per_row,
											const blt::vec2& icon_size, const float window_size)
{
	if (pos == blt::vec2{-1, -1})
	{
		ImGui::SetNextWindowPos(ImVec2{static_cast<float>(blt::gfx::getMouseX()), static_cast<float>(blt::gfx::getMouseY())}, ImGuiCond_Appearing);
	} else
		ImGui::SetNextWindowPos(ImVec2{pos[0], pos[1]}, ImGuiCond_Appearing);

	if (ImGui::BeginPopup("##BlockPicker", ImGuiWindowFlags_AlwaysAutoResize))
	{
		static char filter[128] = "";
		ImGui::InputTextWithHint("##filter", "Search for block or texture...", filter, sizeof(filter));
		const std::string filter_str(filter);
		ImGui::Separator();

		if (ImGui::BeginChild("#BlockPickerChild", ImVec2(0, window_size), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_Border))
		{
			if (const auto i = draw_block_list(block_textures, true, filter_str.empty() ? std::optional<std::string>{} : std::optional(filter_str), icons_per_row,
							icon_size))
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndChild();
				ImGui::EndPopup();
				return *i;
			}
		}
		ImGui::EndChild();

		ImGui::EndPopup();
	}

	return {};
}
