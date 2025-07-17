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
#include <blt/gfx/window.h>
#include "blt/gfx/renderer/resource_manager.h"
#include "blt/gfx/renderer/batch_2d_renderer.h"
#include "blt/gfx/renderer/camera.h"
#include <imgui.h>
#include <sql.h>
#include <data_loader.h>
#include <blt/math/log_util.h>
#include <render.h>

blt::gfx::matrix_state_manager global_matrices;
blt::gfx::resource_manager resources;
blt::gfx::batch_renderer_2d renderer_2d(resources, global_matrices);
blt::gfx::first_person_camera_2d camera;

assets_t assets;
std::optional<gpu_asset_manager> gpu_resources;

void init(const blt::gfx::window_data&)
{
	using namespace blt::gfx;

	std::optional<database_t> db;
	if (!std::filesystem::exists("1.21.5.assets"))
	{
		asset_loader_t loader{"1.21.5"};

		if (const auto result = loader.load_assets("../res/assets", "../res/data"))
		{
			BLT_ERROR("Failed to load assets. Reason: {}", result->to_string());
		}

		db = std::move(loader.load_textures());
	} else
	{
		db = load_database("1.21.5.assets");
	}
	data_loader_t loader{std::move(*db)};
	assets = loader.load();
	gpu_resources = gpu_asset_manager{assets};

	global_matrices.create_internals();
	resources.load_resources();
	renderer_2d.create();
}

void update(const blt::gfx::window_data& data)
{
	global_matrices.update_perspectives(data.width, data.height, 90, 0.1, 2000);

	camera.update();
	camera.update_view(global_matrices);
	global_matrices.update();

	renderer_2d.render(data.width, data.height);

	// ImGui::ShowDemoWindow(nullptr);

	const auto s = gpu_resources->get_icon_render_list();
	if (auto block = show_block_picker(blt::vec2{200, 200}, s))
		BLT_TRACE("Selected block {}", *block);

	auto open_picker = false;
	if (ImGui::Begin("Hello"))
	{
		if (ImGui::Button("Start"))
		{
			open_picker = true;
		}
	}
	ImGui::End();

	if (open_picker)
		ImGui::OpenPopup("##BlockPicker");


	// ImGui::SetNextWindowSize(ImVec2{static_cast<float>(data.width), static_cast<float>(data.height)});
	// ImGui::SetNextWindowPos(ImVec2{0, 0});
	// ImGui::Begin("##Main", nullptr,
	// 			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
	// ImGui::BeginChild("Hello", ImVec2{0, 0}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
	// for (const auto& [namespace_str, textures] : gpu_resources->resources)
	// {
	// 	for (const auto& [name, resource] : textures)
	// 	{
	// 		ImGui::BeginGroup();
	// 		ImGui::Text("%s:%s", namespace_str.c_str(), name.c_str());
	// 		ImGui::Image(resource.texture->getTextureID(), ImVec2{
	// 						static_cast<float>(resource.image.width * 4), static_cast<float>(resource.image.height * 4)
	// 					});
	// 		ImGui::EndGroup();
	// 	}
	// }
	// ImGui::EndChild();
	// ImGui::SameLine();
	// ImGui::BeginChild("Silly", ImVec2{0, 0}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
	// static char block_name[512] = "";
	// ImGui::InputText("Input Block", block_name, sizeof(block_name));
	// const std::string block_name_str(block_name);
	// const auto blocks = gpu_resources->resources["minecraft"].find(block_name_str);
	// if (blocks != gpu_resources->resources["minecraft"].end())
	// {
	// 	auto& [_, block_data] = *blocks;
	// 	ImGui::Text("Found Image:");
	// 	ImGui::Image(block_data.texture->getTextureID(), ImVec2{
	// 					static_cast<float>(block_data.image.width) * 8, static_cast<float>(block_data.image.height) * 8
	// 				});
	// 	ImGui::Text("Closest Images:");
	// 	auto sampler = block_data.image.get_default_sampler();
	//
	// 	comparator_euclidean_t comparator;
	//
	// 	std::vector<std::tuple<std::string, const gpu_image_t*, blt::vec3>> ordered_images;
	// 	for (const auto& [name, images] : gpu_resources->resources["minecraft"])
	// 	{
	// 		auto image_sampler = images.image.get_default_sampler();
	// 		auto dist = comparator.compare(sampler, image_sampler);
	// 		ordered_images.emplace_back("minecraft:" + name, &images, dist);
	// 	}
	// 	std::sort(ordered_images.begin(), ordered_images.end(), [](const auto& a, const auto& b) {
	// 		auto& [a_name, a_texture, a_dist] = a;
	// 		auto& [b_name, b_texture, b_dist] = b;
	// 		return b_dist.magnitude() > a_dist.magnitude();
	// 	});
	// 	for (int i = 0; i < 4; i++)
	// 	{
	// 		for (int j = 0; j < 4; j++)
	// 		{
	// 			auto& [name, texture, distance] = ordered_images[j * 4 + i];
	// 			ImGui::BeginGroup();
	// 			ImGui::Text("%s", name.c_str());
	// 			ImGui::Text("(%f,%f,%f)", distance[0], distance[1], distance[2]);
	// 			ImGui::Text("(Mag: %f)", distance.magnitude());
	// 			ImGui::Image(texture->texture->getTextureID(), ImVec2{static_cast<float>(texture->image.width) * 4, static_cast<float>(texture->image.height) * 4});
	// 			ImGui::EndGroup();
	// 			if (j != 3)
	// 				ImGui::SameLine();
	// 		}
	// 	}
	// }
	// ImGui::EndChild();
	// ImGui::End();
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
