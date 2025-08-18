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
#include <block_picker.h>
#include <data_loader.h>
#include <filesystem>
#include <imgui.h>
#include <render.h>
#include <sql.h>
#include <stack>
#include <tabs.h>
#include <utility>
#include <blt/fs/stream_wrappers.h>
#include <blt/gfx/window.h>
#include <blt/math/log_util.h>
#include <blt/std/ranges.h>
#include <misc/cpp/imgui_stdlib.h>
#include "blt/gfx/renderer/batch_2d_renderer.h"
#include "blt/gfx/renderer/resource_manager.h"

extern std::optional<gpu_asset_manager> gpu_resources;
extern assets_t                         assets;

struct tab_data_t;
static size_t next_tab_id = 1;

std::vector<std::pair<std::unique_ptr<tab_data_t>, size_t>> tabs_to_add;

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


enum class comparator_mode_t
{
	OKLAB, HSV, RGB
};


struct tab_data_t
{
	enum class color_mode_t
	{
		COLOR_OKLAB,
		COLOR_RGB,
		COLOR_SRGB,
		COLOR_HSV
	};


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
			blt::vec3               current_color{0, 0, 0};
			std::vector<ordering_t> ordering;

			explicit value_t(const float offset) : offset{offset} {}

			value_t() = default;
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
		const auto dist_avg      = comparator.compare(sampler, *image_sampler);
		if (extra_samplers)
		{
			auto& [diff_sampler, kernel_sampler] = *extra_samplers;
			auto  color_diff                     = color_difference_sampler_t(images.image);
			auto  color_kernel                   = color_kernel_sampler_t(images.image);
			dist_diff                            = comparator.compare(diff_sampler, *color_diff);
			dist_kernel                          = comparator.compare(kernel_sampler, *color_kernel);
			color_difference_vals.with(dist_diff);
			kernel_difference_vals.with(dist_kernel);
		}
		avg_difference_vals.with(dist_avg);

		order.emplace_back(
			namespace_str + ":" += name,
			&images,
			image_sampler->get_values().front(),
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
		pending_change |= ImGui::InputInt("Images to Display", &images);
		pending_change |= ImGui::InputInt("Samples (per axis)", &samples);
		if (ImGui::InputText("Access Control String", &control_list))
		{
			list = get_blocks_control_list();
			pending_change |= true;
		}
		ImGui::SameLine();
		HelpMarker(
			"Prefix with # to use tags, separate by commas for multiple tags or blocks. Eg: #minecraft:block/leaves,minecraft:block/grass_block");
		static constexpr const char* const color_modes[] = {"OkLab", "Linear RGB", "sRGB", "HSV"};
		if (ImGui::Combo("Color Mode",
						 reinterpret_cast<int*>(&selected_color_mode),
						 color_modes,
						 IM_ARRAYSIZE(color_modes)))
		{
			switch (selected_color_mode)
			{
				case color_mode_t::COLOR_OKLAB:
				default:
					switch_to_oklab();
					update_comparator(comparator_mode_t::OKLAB);
					break;
				case color_mode_t::COLOR_RGB:
					switch_to_linrgb();
					update_comparator(comparator_mode_t::RGB);
					break;
				case color_mode_t::COLOR_SRGB:
					switch_to_srgb();
					update_comparator(comparator_mode_t::RGB);
					break;
				case color_mode_t::COLOR_HSV:
					switch_to_hsv();
					update_comparator(comparator_mode_t::HSV);
					break;
			}
			pending_change |= true;
		}
		pending_change |= ImGui::Checkbox("Extra Items", &include_non_solid);
		switch (selected_color_mode)
		{
			case color_mode_t::COLOR_RGB:
			case color_mode_t::COLOR_SRGB:
				break;
			case color_mode_t::COLOR_HSV:
				ImGui::Text(
					"Changing These values allows you to control how much they contribute to the color's ranking");
				pending_change |= ImGui::SliderFloat("Factor Hue", &comparison_interface->factor0, 0, 1);
				pending_change |= ImGui::SliderFloat("Factor Saturation", &comparison_interface->factor1, 0, 1);
				pending_change |= ImGui::SliderFloat("Factor Value", &comparison_interface->factor2, 0, 1);
				break;
			case color_mode_t::COLOR_OKLAB:
			default:
				ImGui::Text(
					"Changing These values allows you to control how much they contribute to the color's ranking");
				pending_change |= ImGui::SliderFloat("Factor Lightness", &comparison_interface->factor0, 0, 1);
				pending_change |= ImGui::SliderFloat("Factor Chroma", &comparison_interface->factor1, 0, 1);
				pending_change |= ImGui::SliderFloat("Factor Hue", &comparison_interface->factor2, 0, 1);
				break;
		}
		if (configured == BLOCK_SELECT)
		{
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
		}
		if (samples < 1)
			samples = 1;
		if (samples > 8)
			samples = 8;
	}

	void draw_blocks(std::vector<ordering_t>& ordered_images, const std::string& table_id)
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

		if (ImGui::BeginTable(table_id.c_str(),
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

	void draw_order(std::vector<ordering_t>& ordered_images)
	{
		draw_config_tools();
		if (ImGui::BeginChild("ChildImageHolder"))
			draw_blocks(ordered_images, "ImageSelectionTable");
		ImGui::EndChild();
	}

	void process_update(color_relationship_t& rel, const blt::size_t color_index)
	{
		if (rel.colors.empty())
			return;
		auto& selector = rel.colors[color_index];
		{
			auto sampler = color_source_t(blt::vec3{selector.current_color},
										  samples);

			selector.ordering = make_ordering(*sampler, *comparison_interface, {});
		}

		const auto current_offset = selector.offset;
		for (const auto& [i, e] : blt::enumerate(rel.colors))
		{
			if (i == color_index)
				continue;
			auto diff = e.offset - current_offset;
			// BLT_TRACE("With color {}", e.current_color);
			// BLT_TRACE("Diff: {} (From Offset {} and Val {})", diff, e.offset, current_offset);
			switch (selected_conversion_mode)
			{
				case 0:
				default:
				{
					auto converted  = selector.current_color.linear_rgb_to_oklab().oklab_to_oklch();
					converted[2]    = converted[2] + diff;
					e.current_color = converted.oklch_to_oklab().oklab_to_linear_rgb();
					break;
				}
				case 1:
				{
					auto converted  = selector.current_color.linear_rgb_to_hsv();
					converted[0]    = converted[0] + diff;
					e.current_color = converted.hsv_to_linear_rgb();
					break;
				}
			}


			auto sampler = color_source_t(
				blt::vec3{e.current_color},
				samples);

			e.ordering = make_ordering(*sampler, *comparison_interface, {});
		}
		// BLT_TRACE("------");
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
						process_update(color_relationships[selected], 0);
						tab_name = "Color Wheel##" + std::to_string(id);
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
										color_picker_data.data(),
										ImGuiColorEditFlags_InputRGB |
										ImGuiColorEditFlags_PickerHueBar)) { skipped_index.clear(); }
				ImGui::EndChild();
				auto sampler = color_source_t(blt::vec3{color_picker_data}, samples);

				ordered_images = make_ordering(*sampler, *comparison_interface, {});

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
							const auto image_sampler  = color_sampler_t(selected_block_texture->image, samples);
							const auto color_sampler  = color_difference_sampler_t(selected_block_texture->image);
							const auto kernel_sampler = color_kernel_sampler_t(selected_block_texture->image);

							ordered_images = make_ordering(*image_sampler,
														   *comparison_interface,
														   std::pair<sampler_interface_t&, sampler_interface_t&>{
															   *color_sampler,
															   *kernel_sampler});
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
				if (ImGui::BeginChild("##Parent", avail, false, ImGuiWindowFlags_HorizontalScrollbar))
				{
					if (ImGui::BeginChild("##ColorWheel",
										  ImVec2(0, 0),
										  ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
					{
						ImGui::BeginGroup();
						ImGui::Text("Relationship Selector");
						if (ImGui::BeginListBox("##Relationship Selector"))
						{
							for (const auto& [n, item] : blt::enumerate(color_relationships))
							{
								const bool is_selected = selected == n;
								if (ImGui::Selectable(item.name.c_str(), is_selected))
								{
									selected = n;
									process_update(color_relationships[selected], 0);
								}
								if (is_selected)
									ImGui::SetItemDefaultFocus();
							}

							ImGui::EndListBox();
						}
						static constexpr const char* const conversion_modes[] = {"OkLab", "HSV"};
						if (ImGui::Combo("Conversion Mode",
										 &selected_conversion_mode,
										 conversion_modes,
										 IM_ARRAYSIZE(conversion_modes)))
							pending_change = true;
						ImGui::EndGroup();
						ImGui::SameLine();
						ImGui::BeginGroup();
						draw_config_tools();
						ImGui::EndGroup();
					}
					ImGui::EndChild();
					{
						auto& current_mode = color_relationships[selected];

						if (pending_change)
						{
							process_update(current_mode, 0);
							pending_change = false;
						}

						// auto av2 = ImGui::GetContentRegionAvail();
						if (ImGui::BeginChild("##ColorContainers",
											  ImVec2(0, 0),
											  ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
						{
							for (const auto& [i, selector] : blt::enumerate(current_mode.colors))
							{
								float data[3];
								data[0] = selector.current_color[0];
								data[1] = selector.current_color[1];
								data[2] = selector.current_color[2];
								if (ImGui::BeginChild(("SillyColors" + std::to_string(i)).c_str(),
													  ImVec2(0, 0),
													  ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX))
								{
									if (ImGui::ColorPicker3(("##SelectAna" + std::to_string(i)).c_str(),
															data,
															ImGuiColorEditFlags_InputRGB |
															ImGuiColorEditFlags_PickerHueBar))
									{
										selector.current_color[0] = data[0];
										selector.current_color[1] = data[1];
										selector.current_color[2] = data[2];
										process_update(current_mode, i);
									}
									draw_blocks(selector.ordering,
												"ImageSelectionTable" + std::to_string(selector.offset));
								}
								ImGui::EndChild();
								if (i != current_mode.colors.size() - 1)
									ImGui::SameLine();
							}
						}
						ImGui::EndChild();
					}
				}
				ImGui::EndChild();
				break;
		}
	}

	void save(blt::fs::writer_t& writer) const
	{
		blt::fs::writer_serializer_t serial{writer};
		serial.write(tab_name);
		serial.write(control_list);
		serial.write(include_non_solid);
		serial.write(enable_noise);
		serial.write(enable_cutoffs);
		serial.write(cutoff_color_difference);
		serial.write(cutoff_kernel_difference);
		serial.write(configured);
		serial.write(images);
		serial.write(samples);
		serial.write(selected_color_mode);
		serial.write(selected_conversion_mode);
		serial.write(color_picker_data);
		serial.write(id);
		serial.write(weights);
		serial.write(selected_block);
		serial.write(selected);
		serial.write(color_relationships.back().colors);
		// wrapper.write()
	}

	void load(blt::fs::reader_t& reader)
	{
		blt::fs::reader_serializer_t serial{reader};
		serial.read(tab_name);
		serial.read(control_list);
		serial.read(include_non_solid);
		serial.read(enable_noise);
		serial.read(enable_cutoffs);
		serial.read(cutoff_color_difference);
		serial.read(cutoff_kernel_difference);
		serial.read(configured);
		serial.read(images);
		serial.read(samples);
		serial.read(selected_color_mode);
		serial.read(selected_conversion_mode);
		serial.read(color_picker_data);
		serial.read(id);
		serial.read(weights);
		serial.read(selected_block);
		serial.read(selected);
		serial.read(color_relationships.back().colors);

		switch (configured)
		{
			case COLOR_WHEEL:
				process_update(color_relationships[selected], 0);
				break;
			default:
				break;
		}

		pending_change = true;
	}

	void update_comparator(const std::optional<comparator_mode_t> new_val = {})
	{
		if (new_val)
			selected_comparator = *new_val;
		switch (selected_comparator)
		{
			case comparator_mode_t::HSV:
				comparison_interface = std::make_unique<comparator_mean_sample_hsv_euclidean_t>();
				break;
			case comparator_mode_t::OKLAB:
				comparison_interface = std::make_unique<comparator_mean_sample_euclidean_t>();
				break;
			case comparator_mode_t::RGB:
			default:
				comparison_interface = std::make_unique<comparator_mean_sample_oklab_euclidean_t>();
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
	color_mode_t                selected_color_mode      = color_mode_t::COLOR_OKLAB;
	int                         selected_conversion_mode = 0;
	min_max_t                   avg_difference_vals;
	min_max_t                   color_difference_vals;
	min_max_t                   kernel_difference_vals;
	std::array<float, 3>        color_picker_data{};
	blt::hashset_t<int>         skipped_index;
	blt::hashset_t<std::string> list;
	size_t                      id;
	std::array<float, 3>        weights{0.5, 0.15, 0.40};
	std::vector<ordering_t>     ordered_images;

	std::unique_ptr<comparator_interface_t> comparison_interface =
		std::make_unique<comparator_mean_sample_oklab_euclidean_t>();
	comparator_mode_t selected_comparator = comparator_mode_t::OKLAB;

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
				color_relationship_t::value_t{-80},
				color_relationship_t::value_t{-40},
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{40},
				color_relationship_t::value_t{80}
			},
			"Analogous (40*)"},
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
				color_relationship_t::value_t{0},
				color_relationship_t::value_t{60},
				color_relationship_t::value_t{120},
				color_relationship_t::value_t{180},
				color_relationship_t::value_t{240},
				color_relationship_t::value_t{300}
			},
			"Hexadic"
		},
		color_relationship_t{
			{
			},
			"Custom"
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

	std::function<std::unique_ptr<sampler_interface_t>(const image_t&, int)>   color_sampler_t = make_sampler_oklab();
	std::function<std::unique_ptr<sampler_interface_t>(const blt::vec3&, int)> color_source_t = make_source_oklab();
	std::function<std::unique_ptr<sampler_interface_t>(const image_t&)>        color_difference_sampler_t =
		make_difference_oklab();
	std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> color_kernel_sampler_t = make_kernel_oklab();

	void switch_to_oklab()
	{
		color_sampler_t            = make_sampler_oklab();
		color_source_t             = make_source_oklab();
		color_difference_sampler_t = make_difference_oklab();
		color_kernel_sampler_t     = make_kernel_oklab();
		pending_change             = true;
	}

	void switch_to_linrgb()
	{
		color_sampler_t            = make_sampler_linrgb();
		color_source_t             = make_source_linrgb();
		color_difference_sampler_t = make_difference_linrgb();
		color_kernel_sampler_t     = make_kernel_linrgb();
		pending_change             = true;
	}

	void switch_to_srgb()
	{
		color_sampler_t            = make_sampler_srgb();
		color_source_t             = make_source_srgb();
		color_difference_sampler_t = make_difference_srgb();
		color_kernel_sampler_t     = make_kernel_srgb();
		pending_change             = true;
	}

	void switch_to_hsv()
	{
		color_sampler_t            = make_sampler_hsv();
		color_source_t             = make_source_hsv();
		color_difference_sampler_t = make_difference_hsv();
		color_kernel_sampler_t     = make_kernel_hsv();
		pending_change             = true;
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const blt::vec3&, int)> make_source_oklab()
	{
		return [](const blt::vec3& image, const int samples) {
			return std::make_unique<sampler_single_value_t>(image.linear_rgb_to_oklab(), samples * samples);
		};
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const blt::vec3&, int)> make_source_linrgb()
	{
		return [](const blt::vec3& image, const int samples) {
			return std::make_unique<sampler_single_value_t>(image, samples * samples);
		};
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const blt::vec3&, int)> make_source_srgb()
	{
		return [](const blt::vec3& image, const int samples) {
			return std::make_unique<sampler_single_value_t>(image.linear_to_srgb(), samples * samples);
		};
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const blt::vec3&, int)> make_source_hsv()
	{
		return [](const blt::vec3& image, const int samples) {
			return std::make_unique<sampler_single_value_t>(image.linear_rgb_to_hsv(), samples * samples);
		};
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&, int)> make_sampler_oklab()
	{
		return [](
			const image_t& image,
			int            samples) {
			return std::make_unique<sampler_oklab_op_t>(image, samples);
		};
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&, int)> make_sampler_linrgb()
	{
		return [](const image_t& image, int samples) {
			return std::make_unique<sampler_linear_rgb_op_t>(image, samples);
		};
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&, int)> make_sampler_srgb()
	{
		return [](const image_t& image, int samples) { return std::make_unique<sampler_srgb_op_t>(image, samples); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&, int)> make_sampler_hsv()
	{
		return [](const image_t& image, int samples) { return std::make_unique<sampler_hsv_op_t>(image, samples); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_difference_oklab()
	{
		return [](const image_t& image) { return std::make_unique<sampler_color_difference_oklab_t>(image); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_difference_linrgb()
	{
		return [](const image_t& image) { return std::make_unique<sampler_color_difference_rgb_t>(image); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_difference_srgb()
	{
		return [](const image_t& image) { return std::make_unique<sampler_color_difference_srgb_t>(image); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_difference_hsv()
	{
		return [](const image_t& image) { return std::make_unique<sampler_color_difference_hsv_t>(image); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_kernel_oklab()
	{
		return [](const image_t& image) { return std::make_unique<sampler_kernel_filter_oklab_t>(image); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_kernel_linrgb()
	{
		return [](const image_t& image) { return std::make_unique<sampler_kernel_filter_rgb_t>(image); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_kernel_srgb()
	{
		return [](const image_t& image) { return std::make_unique<sampler_kernel_filter_srgb_t>(image); };
	}

	static std::function<std::unique_ptr<sampler_interface_t>(const image_t&)> make_kernel_hsv()
	{
		return [](const image_t& image) { return std::make_unique<sampler_kernel_filter_hsv_t>(image); };
	}
};


std::vector<tab_data_t> window_tabs;

void init_tabs()
{
	window_tabs.emplace_back(0);
	window_tabs.back().tab_name = "Main";
}

void render_tabs()
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
		for (auto& ptr : tabs_to_add)
			window_tabs.insert(window_tabs.begin() + static_cast<blt::ptrdiff_t>(ptr.second),
							   std::move(*ptr.first));
		tabs_to_add.clear();

		ImGui::EndTabBar();
	}
}
