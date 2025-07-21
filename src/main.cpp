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
#include <themes.h>

blt::gfx::matrix_state_manager   global_matrices;
blt::gfx::resource_manager       resources;
blt::gfx::batch_renderer_2d      renderer_2d(resources, global_matrices);
blt::gfx::first_person_camera_2d camera;

assets_t                         assets;
std::optional<gpu_asset_manager> gpu_resources;

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


struct tab_data_t
{
	enum tab_type_t
	{
		UNCONFIGURED, COLOR_SELECT, ASSET_BROWSER
	};


	explicit tab_data_t(const size_t id, std::vector<tab_data_t>& tabs):
		tab_name("Unconfigured##" + std::to_string(id)),
		id(id),
		tabs{&tabs} {}

	void render()
	{
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
		{
			ImGui::OpenPopup("RenameTab");
			const auto size = std::min(tab_name.find('#'), sizeof(buf) - 1);
			std::memset(buf, 0, sizeof(buf));
			std::memcpy(buf, tab_name.data(), size);
		}

		if (ImGui::BeginPopup("RenameTab", ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30);
			ImGui::SetKeyboardFocusHere();
			ImGui::Text("Rename Tab");
			if (ImGui::InputText(("##rename" + std::to_string(id)).c_str(),
								 buf,
								 sizeof(buf),
								 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				tab_name = buf;
				tab_name += "##";
				tab_name += std::to_string(id);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImVec2 avail = ImGui::GetContentRegionAvail();
		switch (configured)
		{
			case UNCONFIGURED:
			{
				const float btnWidth     = avail.x / 4.0f;
				const float btnHeight    = ImGui::GetFrameHeight() * 3;
				const int   btnCount     = 4;
				const float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;

				float menuWidth  = btnWidth;
				float menuHeight = btnCount * btnHeight + (btnCount - 1) * itemSpacingY;

				ImVec2 cursorStart = ImGui::GetCursorPos();  // remember where we were

				float offsetX = (avail.x - menuWidth) * 0.5f;
				float offsetY = (avail.y - menuHeight) * 0.5f;

				if (offsetX < 0)
					offsetX = 0;
				if (offsetY < 0)
					offsetY = 0;

				ImGui::SetCursorPos({cursorStart.x + offsetX, cursorStart.y + offsetY});

				ImGui::BeginGroup();
				{
					if (ImGui::Button("Color Picker", ImVec2(btnWidth, btnHeight)))
					{
						configured = COLOR_SELECT;
						tab_name   = "Color Picker##" + std::to_string(id);
					}

					if (ImGui::Button("Audio", ImVec2(btnWidth, btnHeight)))
						/* … */;
					if (ImGui::Button("Controls", ImVec2(btnWidth, btnHeight)))
						/* … */;
					if (ImGui::Button("Browser", ImVec2(btnWidth, btnHeight)))
					{
						configured = ASSET_BROWSER;
						tab_name   = "Browser##" + std::to_string(id);
					}
				}
				ImGui::EndGroup();
			}
			break;
			case COLOR_SELECT:
			{
				ImGui::BeginChild("##Selector", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
				if (ImGui::ColorPicker3("##SelectBlocks",
										color_picker_data,
										ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_PickerHueBar |
										ImGuiColorEditFlags_PickerHueWheel))
					skipped_index.clear();
				ImGui::EndChild();
				sampler_single_value_t sampler{blt::vec3{color_picker_data}.linear_rgb_to_oklab(), 1};

				comparator_euclidean_t comparator;

				struct ordering_t
				{
					std::string        name;
					const gpu_image_t* texture;
					blt::vec3          average;
					float              dist;
				};

				std::vector<ordering_t> ordered_images;
				for (const auto& [namespace_str, data] : gpu_resources->resources)
				{
					for (const auto& [name, images] : data)
					{
						auto       image_sampler = interface_generator(images.image);
						const auto dist          = comparator.compare(sampler, *image_sampler);
						ordered_images.push_back(ordering_t{
							namespace_str + ":" += name,
							&images,
							image_sampler->get_values().front(),
							dist});
					}
				}

				std::stable_sort(ordered_images.begin(),
								 ordered_images.end(),
								 [](const ordering_t& a, const ordering_t& b) { return a.dist < b.dist; });

				// ImGui::BeginChild("##Selector", ImVec2(0, 0));
				ImGui::Text("Click the image icon to remove it from the list. This is reset when the color changes.");
				ImGui::InputInt("Images to Display", &images);
				int        index           = 0;
				const auto amount_per_line = static_cast<int>(std::max(std::sqrt(images), 4.0));
				if (ImGui::BeginChild("ChildImageHolder"))
				{
					if (ImGui::BeginTable("ImageSelectionTable",
										  amount_per_line,
										  ImGuiTableFlags_PreciseWidths | ImGuiTableFlags_SizingFixedSame))
					{
						ImGui::TableNextColumn();
						for (int i = 0; i < images; i++)
						{
							if (index >= ordered_images.size())
								continue;
							while (skipped_index.contains(index)) { ++index; }
							auto& [name, texture, average, distance] = ordered_images[index];
							ImGui::Text("Avg: (%f, %f, %f)", average[0], average[1], average[2]);
							ImGui::Text("(%.6f)", distance);
							ImGui::Image(texture->texture->getTextureID(),
										 ImVec2{
											 static_cast<float>(texture->image.width) * 4,
											 static_cast<float>(texture->image.height) * 4
										 });
							ImGui::TableNextColumn();
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("%s", block_pretty_name(name).c_str());
								ImGui::EndTooltip();
							}
							if (ImGui::BeginPopupContextItem(std::to_string(index).c_str()))
							{
								ImGui::Text("%s", block_pretty_name(name).c_str());
								if (ImGui::Button("Find Similar")); // TODO
								ImGui::Separator();
								if (ImGui::Button("Remove"))
									skipped_index.insert(index);
								ImGui::Separator();
								if (ImGui::Button("Close"))
									ImGui::CloseCurrentPopup();
								ImGui::EndPopup();
							}
							++index;
						}
						ImGui::EndTable();
					}
				}
				ImGui::EndChild();
			}
			break;
			case ASSET_BROWSER:
				if (ImGui::BeginChild("##Browser", ImVec2(0, 0)))
				{
					if (!asset_rows)
					{
						static auto stmt = assets.db->prepare(
							"SELECT DISTINCT models.texture_namespace, models.texture "
							"FROM models INNER JOIN block_names ON "
							"block_names.model_namespace=models.namespace AND block_names.model=models.model "
							"ORDER BY block_names.block_name");
						asset_rows = assets.get_rows<std::string, std::string>(stmt);
					}
					const auto scale = static_cast<int>(avail.x / (16 * 5));

					static auto delete_models_stmt = assets.db->prepare(
						"DELETE FROM models WHERE texture_namespace=? AND texture=?");

					static auto delete_textures_stmt = assets.db->prepare(
						"DELETE FROM non_solid_textures WHERE namespace=? AND name=?");

					static auto delete_textures2_stmt = assets.db->prepare(
						"DELETE FROM solid_textures WHERE namespace=? AND name=?");

					static auto delete_blocks_stmt = assets.db->prepare("DELETE FROM block_names WHERE "
						"(SELECT COUNT(*) "
						"FROM models WHERE models.namespace=block_names.model_namespace AND models.model=block_names.model) = 0");

					const std::array<statement_t*, 4> statements{
						&delete_models_stmt,
						&delete_textures_stmt,
						&delete_textures2_stmt,
						&delete_blocks_stmt};

					int counter = 0;
					for (const auto& [namespace_str, texture_name] : *asset_rows)
					{
						if (!gpu_resources->resources.contains(namespace_str))
							continue;
						if (!gpu_resources->resources[namespace_str].contains(texture_name))
							continue;
						const auto& image = gpu_resources->resources[namespace_str][texture_name];
						ImGui::BeginGroup();
						ImGui::Image(image.texture->getTextureID(),
									 ImVec2{
										 static_cast<float>(image.image.width) * 4,
										 static_cast<float>(image.image.height) * 4
									 });
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::Text("%s", block_pretty_name(texture_name).c_str());
							ImGui::EndTooltip();
						}
						if (ImGui::BeginPopupContextItem(std::to_string(counter).c_str()))
						{
							ImGui::Text("%s", block_pretty_name(texture_name).c_str());
							ImGui::Separator();
							if (ImGui::Button("DELETE PERMANENTLY"))
							{
								for (const auto stmt : statements)
								{
									stmt->bind().bind_all(namespace_str, texture_name);
									if (const auto res = stmt->execute(); res.has_error())
										BLT_ERROR("Failed to delete texture {}:{}. Reason '{}'",
											  namespace_str,
											  texture_name,
											  assets.db->get_error());
								}
								asset_rows.reset();
								ImGui::CloseCurrentPopup();
								ImGui::EndPopup();
								ImGui::EndGroup();
								ImGui::EndChild();
								return;
							}
							ImGui::Separator();
							if (ImGui::Button("Close"))
								ImGui::CloseCurrentPopup();
							ImGui::EndPopup();
						}
						ImGui::EndGroup();
						if (counter % scale != scale - 1)
							ImGui::SameLine();
						++counter;
					}

					for (const auto& [i, namespace_str, texture_name] : blt::enumerate(*asset_rows).flatten())
					{
						if (!gpu_resources->non_solid_resources.contains(namespace_str))
							continue;
						if (!gpu_resources->non_solid_resources[namespace_str].contains(texture_name))
							continue;
						const auto& image = gpu_resources->non_solid_resources[namespace_str][texture_name];
						ImGui::BeginGroup();
						ImGui::Image(image.texture->getTextureID(),
									 ImVec2{
										 static_cast<float>(image.image.width) * 4,
										 static_cast<float>(image.image.height) * 4
									 });
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::Text("%s", block_pretty_name(texture_name).c_str());
							ImGui::EndTooltip();
						}
						if (ImGui::BeginPopupContextItem(std::to_string(counter).c_str()))
						{
							ImGui::Text("%s", block_pretty_name(texture_name).c_str());
							ImGui::Separator();
							if (ImGui::Button("DELETE PERMANENTLY"))
							{
								for (const auto stmt : statements)
								{
									stmt->bind().bind_all(namespace_str, texture_name);
									if (const auto res = stmt->execute(); res.has_error())
										BLT_ERROR("Failed to delete texture {}:{}. Reason '{}'",
											  namespace_str,
											  texture_name,
											  assets.db->get_error());
								}
								asset_rows.reset();
								ImGui::CloseCurrentPopup();
								ImGui::EndPopup();
								ImGui::EndGroup();
								ImGui::EndChild();
								return;
							}
							ImGui::Separator();
							if (ImGui::Button("Close"))
								ImGui::CloseCurrentPopup();
							ImGui::EndPopup();
						}
						ImGui::EndGroup();
						if (counter % scale != scale - 1)
							ImGui::SameLine();
						++counter;
					}
				}
				ImGui::EndChild();
				break;
		}
	}

	std::optional<std::vector<std::tuple<std::string, std::string>>> asset_rows;

	std::string                                                         tab_name   = "Unconfigured";
	tab_type_t                                                          configured = UNCONFIGURED;
	char                                                                buf[64]{};
	float                                                               color_picker_data[3]{};
	blt::hashset_t<int>                                                 skipped_index;
	int                                                                 images = 16;
	size_t                                                              id;
	std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> interface_generator = [](const auto& image) {
		return std::make_unique<sampler_oklab_op_t>(image, 1);
	};

	std::vector<tab_data_t>* tabs;
};


std::vector<tab_data_t> window_tabs;

void init(const blt::gfx::window_data&)
{
	using namespace blt::gfx;

	Themes::setBessDarkColors();

	static std::optional<database_t> db;
	if (!std::filesystem::exists("1.21.5.assets"))
	{
		asset_loader_t loader{"1.21.5"};

		if (const auto result = loader.load_assets("../res/assets", "../res/data"))
		{
			BLT_ERROR("Failed to load assets. Reason: {}", result->to_string());
		}

		db = std::move(loader.load_textures());
	} else { db = load_database("1.21.5.assets"); }
	static data_loader_t loader{std::move(*db)};
	assets        = loader.load();
	gpu_resources = gpu_asset_manager{assets};

	global_matrices.create_internals();
	resources.load_resources();
	renderer_2d.create();

	window_tabs.emplace_back(0, window_tabs);
	window_tabs.back().tab_name = "Main";
}

void update(const blt::gfx::window_data& data)
{
	global_matrices.update_perspectives(data.width, data.height, 90, 0.1, 2000);

	camera.update();
	camera.update_view(global_matrices);
	global_matrices.update();

	renderer_2d.render(data.width, data.height);

	const auto s = gpu_resources->get_icon_render_list();
	if (auto block = show_block_picker(blt::vec2{200, 200}, s))
		BLT_TRACE("Selected block {}", *block);

	ImGui::SetNextWindowSize(ImVec2{static_cast<float>(data.width), static_cast<float>(data.height)}, ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2{0, 0}, ImGuiCond_Always);
	ImGui::Begin("##Main",
				 nullptr,
				 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
				 ImGuiWindowFlags_NoTitleBar);
	ImGui::BeginGroup();
	auto avail       = ImGui::GetContentRegionAvail();
	bool should_open = false;
	if (ImGui::BeginChild("Control Panel", ImVec2(300, avail.y), ImGuiChildFlags_Border))
	{
		ImGui::Text("Control Panel");
		ImGui::Separator();
		ImGui::Text("Select Biome");
		ImGui::SameLine();
		HelpMarker("Select a biome to view grass, leaves, etc with their respective textures.");
		if (ImGui::BeginListBox("##Biomes"))
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
					gpu_resources->update_textures(assets.assets[namespace_str].biome_colors[biome]);
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndListBox();
		}
		ImGui::Separator();
		if (ImGui::Button("Silly")) { should_open = true; }
	}
	ImGui::EndChild();
	ImGui::EndGroup();
	ImGui::SameLine();
	avail = ImGui::GetContentRegionAvail();
	if (ImGui::BeginChild("MainTabs", avail, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		static size_t next_tab_id = 1;
		if (ImGui::BeginTabBar(
			"Color Views",
			ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
		{
			if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
				window_tabs.emplace_back(next_tab_id++, window_tabs);

			for (size_t n = 0; n < window_tabs.size();)
			{
				bool open = true;
				if (ImGui::BeginTabItem(window_tabs[n].tab_name.c_str(), &open, ImGuiTabItemFlags_None))
				{
					window_tabs[n].render();
					ImGui::EndTabItem();
				}

				if (!open)
					window_tabs.erase(window_tabs.begin() + static_cast<blt::ptrdiff_t>(n));
				else
					n++;
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::EndChild();
	ImGui::End();

	if (should_open)
		ImGui::OpenPopup("##BlockPicker");

	ImGui::ShowDemoWindow(nullptr);

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
