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

std::optional<std::string> show_block_picker(const blt::vec2& pos, const std::vector<std::pair<std::string, const gpu_image_t*>>& block_textures,
											const int icons_per_row, const blt::vec2& icon_size, const float window_size)
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
		// if (ImGui::BeginChild("##block_picker_scroll_region", ImVec2{0, window_size}, true))
		// {
		// ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4, 4});

		// ImGui::Columns(icons_per_row, nullptr, false);
		if (ImGui::BeginTable("##block_display", icons_per_row, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
							ImVec2{0, window_size}))
		{
			for (const auto& [name, texture] : block_textures)
			{
				if (!filter_str.empty())
				{
					auto lower = blt::string::toLowerCase(block_pretty_name(name));
					auto lower_filter = blt::string::toLowerCase(filter_str);
					if (!blt::string::contains(lower, lower_filter))
						continue;
				}

				if (ImGui::ImageButton(block_pretty_name(name).c_str(), texture->texture->getTextureID(), ImVec2{icon_size[0], icon_size[1]}))
				{
					ImGui::CloseCurrentPopup();
					ImGui::EndTable();
					// ImGui::PopStyleVar();
					// ImGui::EndChild();
					ImGui::EndPopup();
					return name;
				}

				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", block_pretty_name(name).c_str());

				ImGui::TableNextColumn();
			}

			ImGui::EndTable();
		}

		// ImGui::PopStyleVar();
		// }
		// ImGui::EndChild();
		ImGui::EndPopup();
	}

	return {};
}
