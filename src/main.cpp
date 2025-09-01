/*
*  Copyright (C) 2024  Brett Terpstra
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
#include <block_picker.h>
#include <filesystem>
#include <utility>
#include <blt/gfx/window.h>
#include "blt/gfx/renderer/resource_manager.h"
#include "blt/gfx/renderer/batch_2d_renderer.h"
#include "blt/gfx/renderer/camera.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <sql.h>
#include <data_loader.h>
#include <blt/math/log_util.h>
#include <render.h>
#include <stack>
#include <tabs.h>
#include <themes.h>
#include <blt/config.h>
#include <blt/fs/stream_wrappers.h>
#include <blt/std/ranges.h>
#include <tabs/base.h>

blt::gfx::matrix_state_manager   global_matrices;
blt::gfx::resource_manager       resources;
blt::gfx::batch_renderer_2d      renderer_2d(resources, global_matrices);
blt::gfx::first_person_camera_2d camera;

assets_t                              assets;
std::optional<gpu_asset_manager>      gpu_resources;
std::vector<std::filesystem::path>    asset_locations;
blt::hashmap_t<std::string, assets_t> loaded_assets;
std::vector<data_loader_t>            data_loaders;

static void HelpMarker(const std::string& desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::BeginItemTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void check_for_res(std::string path)
{
	const auto current_path = std::filesystem::current_path();
	if (!blt::string::ends_with(path, '/'))
		path += '/';
	BLT_INFO("Trying to load from directory {}", path);
	auto dir_iter = std::filesystem::recursive_directory_iterator{path};
	for (const auto& file : dir_iter)
	{
		if (file.is_directory())
			continue;
		if (file.path().has_extension() && file.path().extension() == ".assets")
		{
			auto file_path = file.path().lexically_relative(current_path);
			if (file_path.empty())
				continue;
			BLT_TRACE("Found {}", file_path.string());
			if (std::find_if(asset_locations.begin(),
							 asset_locations.end(),
							 [&](const auto& a) { return std::filesystem::equivalent(file_path, a); }) ==
				asset_locations.end())
			{
				asset_locations.push_back(file_path);
				BLT_DEBUG("Unique Found {}", file_path.string());
			}
		}
	}
}

void update_current_assets(const assets_t& a)
{
	gpu_resources.reset();
	assets        = a;
	gpu_resources = gpu_asset_manager{assets};
}

void init(const blt::gfx::window_data&)
{
	using namespace blt::gfx;

	Themes::setBessDarkColors();

	if (std::filesystem::exists(CMAKE_SOURCE_DIR)) { check_for_res(CMAKE_SOURCE_DIR); }
	check_for_res("./");

	data_loaders.reserve(asset_locations.size());
	for (const auto& location : asset_locations)
	{
		auto db = load_database(location.string());
		data_loaders.emplace_back(std::move(db));
		const auto assets                = data_loaders.back().load();
		loaded_assets[location.string()] = assets;
	}
	if (!loaded_assets.empty())
		update_current_assets(loaded_assets[asset_locations.front().string()]);

	global_matrices.create_internals();
	resources.load_resources();
	renderer_2d.create();

	// window_tabs.reserve(16);
	init_tabs();
}

void update(const blt::gfx::window_data& data)
{
	global_matrices.update_perspectives(data.width, data.height, 90, 0.1, 2000);

	camera.update();
	camera.update_view(global_matrices);
	global_matrices.update();

	renderer_2d.render(data.width, data.height);

	// const auto s = gpu_resources->get_icon_render_list();
	// if (auto block = show_block_picker(blt::vec2{200, 200}, s))
	// 	BLT_TRACE("Selected block {}", *block);

	ImGui::SetNextWindowSize(ImVec2{static_cast<float>(data.width), static_cast<float>(data.height)}, ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2{0, 0}, ImGuiCond_Always);
	ImGui::Begin("##Main",
				 nullptr,
				 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
				 ImGuiWindowFlags_NoTitleBar);
	ImGui::BeginGroup();
	auto avail       = ImGui::GetContentRegionAvail();
	bool should_open = false;
	if (ImGui::BeginChild("Control Panel", ImVec2(200, avail.y), ImGuiChildFlags_Border))
	{
		ImGui::Text("Control Panel");
		ImGui::Separator();
		ImGui::Text("Select Biome");
		ImGui::SameLine();
		HelpMarker("Select a biome to view grass, leaves, etc with their respective textures.");
		avail = ImGui::GetContentRegionAvail();
		if (ImGui::BeginListBox("##Biomes", ImVec2(avail.x, 0)))
		{
			auto&         biomes_vec        = assets.get_biomes();
			static size_t item_selected_idx = std::distance(biomes_vec.begin(),
															std::find_if(
																biomes_vec.begin(),
																biomes_vec.end(),
																[&](const auto& item) {
																	return std::get<1>(item) == "plains";
																}));
			for (const auto& [i, namespace_str, biome] : blt::enumerate(biomes_vec).flatten())
			{
				const bool is_selected = (item_selected_idx == i);
				if (ImGui::Selectable((block_pretty_name(biome)).c_str(), is_selected))
				{
					item_selected_idx = i;
					if (gpu_resources)
						gpu_resources->update_textures(assets.assets[namespace_str].biome_colors[biome]);
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndListBox();
		}
		ImGui::Separator();
		static size_t asset_index_selected = 0;
		ImGui::Text("Assets Databases");
		ImGui::SameLine();
		HelpMarker("This will change only for new tabs");
		avail = ImGui::GetContentRegionAvail();
		if (ImGui::BeginListBox("##Assets Databases", ImVec2(avail.x, 0)))
		{
			for (const auto& [i, str] : blt::enumerate(asset_locations))
			{
				if (ImGui::Selectable(str.c_str(), i == asset_index_selected))
				{
					asset_index_selected = i;
					update_current_assets(loaded_assets[asset_locations[i].string()]);
					BLT_TRACE("Switching to {}", asset_locations[i].string());
				}
			}
			ImGui::EndListBox();
		}
		ImGui::Separator();
		if (ImGui::Button("Generate Assets")) { should_open = true; }
	}
	ImGui::EndChild();
	ImGui::EndGroup();
	ImGui::SameLine();
	avail = ImGui::GetContentRegionAvail();
	if (ImGui::BeginChild("MainTabs", avail, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImGui::BeginChild("##PICKER", ImVec2(), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
		static blt::color_t color = blt::color::linear_rgb_t{blt::vec3{0.5, 0.5, 0.5}};
		std::array modes{color_mode_t{color_mode_t::OkLab}};
		if (tab_base_t::draw_color_picker(modes, color))
		{

		}
		ImGui::EndChild();
		if (gpu_resources)
			render_tabs();
		else
			ImGui::Text("No Asset Database Loaded!");
	}
	ImGui::EndChild();
	ImGui::End();

	if (should_open)
		ImGui::OpenPopup("##BlockPicker");

	ImGui::ShowDemoWindow(nullptr);
}

void destroy(const blt::gfx::window_data&)
{
	gpu_resources.reset();
	global_matrices.cleanup();
	resources.cleanup();
	renderer_2d.cleanup();
	blt::gfx::cleanup();
}

int main()
{
	blt::gfx::init(blt::gfx::window_data{"Minecraft Color Picker", init, update, destroy}.setSyncInterval(1));

	return 0;
}
