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
#include <themes.h>

blt::gfx::matrix_state_manager   global_matrices;
blt::gfx::resource_manager       resources;
blt::gfx::batch_renderer_2d      renderer_2d(resources, global_matrices);
blt::gfx::first_person_camera_2d camera;

assets_t                         assets;
std::optional<gpu_asset_manager> gpu_resources;
static size_t                    next_tab_id = 1;

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

struct tab_data_t;

std::vector<std::pair<std::unique_ptr<tab_data_t>, size_t>> tabs_to_add;


struct min_max_t
{
	float min = std::numeric_limits<float>::max(), max = std::numeric_limits<float>::min();

	void with(const float v)
	{
		if (v < min)
			min = v;
		if (v > max)
			max = v;
	}

	[[nodiscard]] float scale() const { return std::abs(max - min); }

	// [[nodiscard]] float normalize(const float f) const { return f; }
	[[nodiscard]] float normalize(const float f) const
	{
		return (f - min) / scale();;
	}

	void reset()
	{
		min = std::numeric_limits<float>::max();
		max = std::numeric_limits<float>::min();
	}
};


struct tab_data_t
{
	struct ordering_t
	{
		std::string        name;
		const gpu_image_t* texture;
		blt::vec3          average;
		float              dist_avg;
		float              dist_color;
		float              dist_kernel;

		ordering_t(std::string        name,
				   const gpu_image_t* texture,
				   const blt::vec3&   average,
				   const float        dist_avg,
				   const float        dist_color,
				   const float        dist_kernel) : name{std::move(name)},
													 texture{texture},
													 average{average},
													 dist_avg{dist_avg},
													 dist_color{dist_color},
													 dist_kernel{dist_kernel} {}
	};


	struct color_relationship_t
	{
		struct value_t
		{
			float                   offset = 0;
			blt::vec3               current_color;
			std::vector<ordering_t> ordering;

			explicit value_t(const float offset) : offset{offset} {}
		};


		std::vector<value_t> colors;
		std::string          name;

		color_relationship_t(const std::vector<value_t>& colors, std::string name) : colors{colors},
			name{std::move(name)} {}
	};


	void process_resource_for_order(std::vector<ordering_t>& order,
									const std::string&       namespace_str,
									const std::string&       name,
									const gpu_image_t&       images,
									sampler_interface_t&     sampler,
									comparator_interface_t&  comparator,
									std::optional<std::pair<sampler_interface_t&, sampler_interface_t&>>
									extra_samplers)
	{
		auto       image_sampler = color_sampler_t(images.image, samples);
		float      dist_diff     = 0;
		float      dist_kernel   = 0;
		const auto dist_avg      = comparator.compare(sampler, image_sampler);
		if (extra_samplers)
		{
			auto& [diff_sampler, kernel_sampler] = *extra_samplers;
			auto  color_diff                     = color_difference_sampler_t{images.image};
			auto  color_kernel                   = color_kernel_sampler_t{images.image};
			dist_diff                            = comparator.compare(diff_sampler, color_diff);
			dist_kernel                          = comparator.compare(kernel_sampler, color_kernel);
			color_difference_vals.with(dist_diff);
			kernel_difference_vals.with(dist_kernel);
		}
		avg_difference_vals.with(dist_avg);

		order.emplace_back(
			namespace_str + ":" += name,
			&images,
			image_sampler.get_values().front(),
			dist_avg,
			dist_diff,
			dist_kernel);
	}

	std::vector<ordering_t> make_ordering(sampler_interface_t&    sampler,
										  comparator_interface_t& comparator,
										  std::optional<std::pair<sampler_interface_t&, sampler_interface_t&>>
										  extra_samplers)
	{
		std::vector<ordering_t> order;
		color_difference_vals.reset();
		kernel_difference_vals.reset();
		avg_difference_vals.reset();
		for (const auto& [namespace_str, data] : gpu_resources->resources)
		{
			for (const auto& [name, images] : data)
				process_resource_for_order(order, namespace_str, name, images, sampler, comparator, extra_samplers);
		}

		if (include_non_solid)
		{
			for (const auto& [namespace_str, data] : gpu_resources->non_solid_resources)
			{
				for (const auto& [name, images] : data)
					process_resource_for_order(order, namespace_str, name, images, sampler, comparator, extra_samplers);
			}
		}

		auto l_weights = weights;

		if (!enable_noise || !extra_samplers)
		{
			l_weights[1] = 0;
			l_weights[2] = 0;
		}

		std::stable_sort(order.begin(),
						 order.end(),
						 [&l_weights, this](const ordering_t& a, const ordering_t& b) {
							 const auto a_avg =
								 l_weights[0] * avg_difference_vals.normalize(a.dist_avg) + l_weights[1] *
								 color_difference_vals.normalize(a.dist_color) + l_weights[2] * kernel_difference_vals.
								 normalize(a.dist_kernel);
							 const auto b_avg =
								 l_weights[0] * avg_difference_vals.normalize(b.dist_avg) + l_weights[1] *
								 color_difference_vals.normalize(b.dist_color) + l_weights[2] * kernel_difference_vals.
								 normalize(b.dist_kernel);
							 return a_avg < b_avg;
						 });

		return order;
	}

	[[nodiscard]] blt::hashset_t<std::string> get_blocks_control_list() const
	{
		blt::hashset_t<std::string> blocks;
		const auto                  sections = blt::string::split(control_list, ',');
		for (const auto& section : sections)
		{
			if (blt::string::starts_with(section, "#"))
			{
				auto tag_str = section.substr(1);
				auto parts   = blt::string::split(tag_str, ':');
				if (parts.empty())
					continue;
				if (parts.size() == 1)
					parts.insert(parts.begin(), "minecraft");
				auto it = assets.assets.find(parts[0]);
				if (it == assets.assets.end())
					continue;
				auto& [namespace_str, ns] = *it;
				auto  it2                 = ns.tags.find(parts[1]);
				if (it2 == ns.tags.end())
					continue;
				auto& [tag_name, set] = *it2;
				for (const auto& block : set)
					blocks.insert(block);
			} else
			{
				auto parts = blt::string::split(section, ':');
				if (parts.empty())
					continue;
				if (parts.size() == 1)
					parts.insert(parts.begin(), "minecraft");
				blocks.insert(parts[0] + ":" += parts[1]);
			}
		}
		blt::hashset_t<std::string> list;
		for (const auto& str : blocks)
		{
			auto parts = blt::string::split(str, ':');
			if (parts.empty())
				continue;
			auto it = assets.assets.find(parts[0]);
			if (it == assets.assets.end())
				continue;
			auto [_, ns] = *it;
			auto it2     = ns.block_to_textures.find(parts[1]);
			if (it2 == ns.block_to_textures.end())
				continue;
			auto& [block_name, textures] = *it2;
			for (const auto& texture : textures)
				list.insert(texture);
		}
		return list;
	}

	void draw_config_tools()
	{
		ImGui::InputInt("Images to Display", &images);
		pending_change |= ImGui::InputInt("Samples (per axis)", &samples);
		if (ImGui::InputText("Access Control String", &control_list))
			list = get_blocks_control_list();
		ImGui::SameLine();
		HelpMarker(
			"Prefix with # to use tags, separate by commas for multiple tags or blocks. Eg: #minecraft:block/leaves,minecraft:block/grass_block");
		ImGui::Checkbox("Blacklist?", &is_blacklist);
		ImGui::SameLine();
		HelpMarker("If not enabled then list acts as a whitelist");
		pending_change |= ImGui::Checkbox("Extra Items", &include_non_solid);
		pending_change |= ImGui::SliderFloat("Average Color Weight", &weights[0], 0, 1);
		pending_change |= ImGui::Checkbox("Enable Noise In Selection", &enable_noise);
		if (enable_noise)
		{
			pending_change |= ImGui::SliderFloat("Color Difference Weight", &weights[1], 0, 1);
			pending_change |= ImGui::SliderFloat("Kernel Difference Weight", &weights[2], 0, 1);
		}
		ImGui::Checkbox("Enable Noise Cutoffs", &enable_cutoffs);
		if (enable_cutoffs)
		{
			ImGui::SliderFloat("Color Difference Cutoff",
							   &cutoff_color_difference,
							   color_difference_vals.min,
							   color_difference_vals.max,
							   "%.9f");
			ImGui::SliderFloat("Kernel Difference Cutoff",
							   &cutoff_kernel_difference,
							   kernel_difference_vals.min,
							   kernel_difference_vals.max,
							   "%.9f");
		}
		if (samples < 1)
			samples = 1;
		if (samples > 8)
			samples = 8;
	}

	void draw_blocks(std::vector<ordering_t>& ordered_images)
	{
		if (ordered_images.empty())
			return;
		const auto amount_per_line = static_cast<int>(std::max(std::sqrt(images), 4.0));

		const auto iter = blt::enumerate(ordered_images).filter([this](const auto& beep) {
			const auto& [index, image] = beep;
			if (enable_cutoffs && image.dist_color > cutoff_color_difference)
				return false;
			if (enable_cutoffs && image.dist_kernel > cutoff_kernel_difference)
				return false;
			if (skipped_index.contains(index))
				return false;
			if (list.contains(image.name))
				return false;
			if (!selected_block.empty() && image.name == selected_block)
				return false;
			return true;
		});
		auto begin = iter.begin();
		BLT_TRACE("{} Equals {} {}", ordered_images.size(), iter.begin() == iter.end(), begin != iter.end());
		if (ImGui::BeginChild("ChildImageHolder"))
		{
			if (ImGui::BeginTable("ImageSelectionTable",
								  amount_per_line,
								  ImGuiTableFlags_PreciseWidths | ImGuiTableFlags_SizingFixedSame))
			{
				ImGui::TableNextColumn();
				for (int i = 0; i < images; i++, ++begin)
				{
					while (begin != iter.end() && !(*begin).has_value())
						++begin;
					if (begin == iter.end())
						continue;
					auto  [index, tab_order]                                          = (*begin).value();
					auto& [name, texture, average, distance, color_dist, kernel_dist] = tab_order;

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
					if (ImGui::BeginPopupContextItem(name.c_str()))
					{
						ImGui::Text("%s", block_pretty_name(name).c_str());
						ImGui::Text("[%f | %f | %f]", distance, color_dist, kernel_dist);
						if (ImGui::Button("Find Similar"))
						{
							tab_data_t data{next_tab_id++};
							data.selected_block         = name;
							data.selected_block_texture = texture;
							data.configured             = BLOCK_SELECT;
							data.tab_name               = "Block Picker##" + std::to_string(data.id);
							tabs_to_add.emplace_back(std::make_unique<tab_data_t>(std::move(data)), id);
						}
						ImGui::Separator();
						if (ImGui::Button("Remove"))
							skipped_index.insert(index);
						ImGui::Separator();
						if (ImGui::Button("Close"))
							ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
					}
				}
				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
	}

	void draw_order(std::vector<ordering_t>& ordered_images)
	{
		draw_config_tools();
		draw_blocks(ordered_images);
	}

	void process_update(color_relationship_t& rel, const blt::size_t index)
	{
		auto& selector = rel.colors[index];
		{
			sampler_single_value_t sampler{
				blt::vec3{selector.current_color}.linear_rgb_to_oklab(),
				samples * samples};

			comparator_mean_sample_euclidean_t comparator;
			selector.ordering   = make_ordering(sampler, comparator, {});
		}
		auto current_offset = selector.offset;
		for (const auto& [i, e] : blt::enumerate(rel.colors))
		{
			if (i == index)
				continue;
			auto diff = e.offset - current_offset;
			auto converted = e.current_color.linear_rgb_to_oklab().oklab_to_oklch();
			converted[2] += diff;
			e.current_color = converted.oklch_to_oklab().oklab_to_linear_rgb();

			sampler_single_value_t sampler{
				blt::vec3{e.current_color}.linear_rgb_to_oklab(),
				samples * samples};

			comparator_mean_sample_euclidean_t comparator;

			e.ordering = make_ordering(sampler, comparator, {});
		}
	}

	enum tab_type_t
	{
		UNCONFIGURED, COLOR_SELECT, ASSET_BROWSER, BLOCK_SELECT, COLOR_WHEEL
	};


	explicit tab_data_t(const size_t id):
		tab_name("Unconfigured##" + std::to_string(id)),
		id(id) { list = get_blocks_control_list(); }

	void render()
	{
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
		{
			ImGui::OpenPopup("RenameTab");
			const auto size = std::min(tab_name.find('#'), tab_name.size());
			input_buf       = tab_name.substr(0, size);
		}

		if (ImGui::BeginPopup("RenameTab", ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30);
			ImGui::SetKeyboardFocusHere();
			ImGui::Text("Rename Tab");
			if (ImGui::InputText(("##rename" + std::to_string(id)).c_str(),
								 &input_buf,
								 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				tab_name = input_buf;
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

					if (ImGui::Button("Block Picker", ImVec2(btnWidth, btnHeight)))
					{
						configured = BLOCK_SELECT;
						tab_name   = "Block Picker##" + std::to_string(id);
					}
					if (ImGui::Button("Color Relationship Helper", ImVec2(btnWidth, btnHeight)))
					{
						configured = COLOR_WHEEL;
						tab_name   = "Color Wheel##" + std::to_string(id);
					}
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
										ImGuiColorEditFlags_InputRGB |
										ImGuiColorEditFlags_PickerHueWheel)) { skipped_index.clear(); }
				ImGui::EndChild();
				sampler_single_value_t sampler{blt::vec3{color_picker_data}.linear_rgb_to_oklab(), samples * samples};

				comparator_mean_sample_euclidean_t comparator;

				ordered_images = make_ordering(sampler, comparator, {});

				// ImGui::BeginChild("##Selector", ImVec2(0, 0));
				ImGui::Text("Click the image icon to remove it from the list. This is reset when the color changes.");
				draw_order(ordered_images);
			}
			break;
			case ASSET_BROWSER:
				if (ImGui::BeginChild("##Browser", ImVec2(0, 0)))
				{
					ImGui::Text("Search: ");
					ImGui::InputText("##InputSearch", &input_buf);
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

					const auto& search = input_buf;

					int counter = 0;
					for (const auto& [namespace_str, texture_name] : *asset_rows)
					{
						if (!gpu_resources->resources.contains(namespace_str))
							continue;
						if (!gpu_resources->resources[namespace_str].contains(texture_name))
							continue;
						auto name = namespace_str + ":" += texture_name;
						if (!search.empty() && !blt::string::contains(name, search))
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
						auto name = namespace_str + ":" += texture_name;
						if (!search.empty() && !blt::string::contains(name, search))
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
			case BLOCK_SELECT:
				if (ImGui::BeginChild("BLOCK_SELECT"))
				{
					bool should_open = false;
					if (ImGui::Button("Select Block", ImVec2(avail.x * 0.5f, 50)))
						should_open = true;
					if (should_open)
						ImGui::OpenPopup("##BlockPicker");
					const auto s            = gpu_resources->get_icon_render_list();
					auto       content_min  = ImGui::GetWindowContentRegionMin();
					auto       content_max  = ImGui::GetWindowContentRegionMax();
					auto       local_center = ImVec2((content_min.x + content_max.x) * 0.5f,
													 (content_min.y + content_max.y) * 0.5f);

					const auto window = ImGui::GetWindowPos();

					if (const auto block = show_block_picker(blt::vec2{
																 window.x + local_center.x - (32 * 16 + 48) * 0.5f,
																 window.y + local_center.y - 32 * 8 * 0.5 - 48},
															 s))
					{
						selected_block         = s[*block].block_name;
						selected_block_texture = s[*block].texture;
						pending_change         = true;
					}

					if (selected_block_texture != nullptr)
					{
						ImGui::Text("Block: %s", block_pretty_name(selected_block).c_str());
						ImGui::Image(selected_block_texture->texture->getTextureID(), ImVec2{64, 64});

						if (pending_change)
						{
							color_sampler_t                    image_sampler(selected_block_texture->image, samples);
							color_difference_sampler_t         color_sampler(selected_block_texture->image);
							color_kernel_sampler_t             kernel_sampler(selected_block_texture->image);
							comparator_mean_sample_euclidean_t comparator;

							ordered_images = make_ordering(image_sampler,
														   comparator,
														   std::pair<sampler_interface_t&, sampler_interface_t&>{
															   color_sampler,
															   kernel_sampler});
							pending_change = false;
						}

						ImGui::Text(
							"Click the image icon to remove it from the list. This is reset when the block changes.");
						draw_order(ordered_images);
					}
				}
				ImGui::EndChild();
				break;
			case COLOR_WHEEL:
				if (ImGui::BeginChild("##ColorWheel", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY))
				{
					ImGui::Text("Relationship Selector");
					if (ImGui::BeginListBox("##Relationship Selector"))
					{
						for (const auto& [n, item] : blt::enumerate(color_relationships))
						{
							const bool is_selected = selected == n;
							if (ImGui::Selectable(item.name.c_str(), is_selected))
								selected = n;
							if (is_selected)
								ImGui::SetItemDefaultFocus();
						}

						ImGui::SameLine();
						ImGui::BeginGroup();
						draw_config_tools();
						ImGui::EndGroup();

						auto& current_mode = color_relationships[selected];

						for (const auto& [i, selector] : blt::enumerate(current_mode.colors))
						{
							float data[3];
							data[0] = selector.current_color[0];
							data[1] = selector.current_color[1];
							data[2] = selector.current_color[2];
							if (ImGui::ColorPicker3(("##SelectAna" + std::to_string(i)).c_str(),
													data,
													ImGuiColorEditFlags_InputRGB |
													ImGuiColorEditFlags_PickerHueWheel))
							{
								selector.current_color[0] = data[0];
								selector.current_color[1] = data[1];
								selector.current_color[2] = data[2];
								process_update(current_mode, selected);
							}
							if (i != current_mode.colors.size() - 1)
								ImGui::SameLine();
						}

						for (auto& selector : current_mode.colors)
						{
							ImGui::BeginGroup();
							draw_blocks(selector.ordering);
							ImGui::EndGroup();
							ImGui::SameLine();
						}

						ImGui::EndListBox();
					}
				}
				ImGui::EndChild();
				break;
		}
	}

	std::optional<std::vector<std::tuple<std::string, std::string>>> asset_rows;

	std::string                 input_buf;
	std::string                 tab_name                 = "Unconfigured";
	std::string                 control_list             = "#block/leaves,tnt";
	bool                        is_blacklist             = true;
	bool                        include_non_solid        = false;
	bool                        pending_change           = true;
	bool                        enable_noise             = false;
	bool                        enable_cutoffs           = false;
	float                       cutoff_color_difference  = 0;
	float                       cutoff_kernel_difference = 0;
	tab_type_t                  configured               = UNCONFIGURED;
	int                         images                   = 16;
	int                         samples                  = 1;
	min_max_t                   avg_difference_vals;
	min_max_t                   color_difference_vals;
	min_max_t                   kernel_difference_vals;
	float                       color_picker_data[3]{};
	blt::hashset_t<int>         skipped_index;
	blt::hashset_t<std::string> list;
	size_t                      id;
	std::array<float, 3>        weights{0.5, 0.15, 0.40};
	std::vector<ordering_t>     ordered_images;

	std::string        selected_block;
	const gpu_image_t* selected_block_texture = nullptr;

	size_t                            selected = 0;
	std::vector<color_relationship_t> color_relationships{
		color_relationship_t{
			{
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{180}
			},
			"Complementary"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{-60},
				color_relationship_t::value_t{-30},
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{30},
				color_relationship_t::value_t{60}
			},
			"Analogous (30*)"},
		color_relationship_t{
			{
				color_relationship_t::value_t{-40},
				color_relationship_t::value_t{-20},
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{20},
				color_relationship_t::value_t{40}
			},
			"Analogous (20*)"},
		color_relationship_t{
			{
				color_relationship_t::value_t{-150},
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{150}
			},
			"Split-Complementary"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{-120},
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{120}
			},
			"Triadic"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{90},
				color_relationship_t::value_t{180},
				color_relationship_t::value_t{270}
			},
			"Square"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{30},
				color_relationship_t::value_t{180},
				color_relationship_t::value_t{210}
			},
			"Tetradic (30)"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{60},
				color_relationship_t::value_t{180},
				color_relationship_t::value_t{240}
			},
			"Tetradic (60)"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{180 - 30},
				color_relationship_t::value_t{180},
				color_relationship_t::value_t{180 + 30}
			},
			"Accent Complement"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{90}
			},
			"Clash"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{72},
				color_relationship_t::value_t{144},
				color_relationship_t::value_t{216},
				color_relationship_t::value_t{288}
			},
			"Pentadic"
		},
		color_relationship_t{
			{
				color_relationship_t::value_t{60},
				color_relationship_t::value_t{120},
				color_relationship_t::value_t{180},
				color_relationship_t::value_t{240},
				color_relationship_t::value_t{300}
			},
			"Hexadic"
		}
	};

	static float wrap_hue(float hue)
	{
		while (hue >= 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;
		return hue;
	}

	using color_sampler_t            = sampler_oklab_op_t;
	using color_difference_sampler_t = sampler_color_difference_op_t;
	using color_kernel_sampler_t     = sampler_kernel_filter_op_t;
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

	// window_tabs.reserve(16);
	window_tabs.emplace_back(0);
	window_tabs.back().tab_name = "Main";
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
		if (ImGui::BeginTabBar(
			"Color Views",
			ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
		{
			if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
				window_tabs.emplace_back(next_tab_id++);

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

			// stupid hack
			for (const auto& ptr : tabs_to_add)
				window_tabs.insert(window_tabs.begin() + static_cast<blt::ptrdiff_t>(ptr.second), *ptr.first);
			tabs_to_add.clear();

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
