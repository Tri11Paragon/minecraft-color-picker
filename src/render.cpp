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
#include <block_picker.h>
#include <render.h>
#include <blt/math/log_util.h>

gpu_asset_manager::gpu_asset_manager(assets_t& assets): assets(&assets)
{
	auto ass = assets;
	for (auto& [namespace_str, data] : ass.assets)
	{
		for (auto& [image_name, image] : data.images)
		{
			if (image.width != image.height)
			{
				const auto smallest = std::min(image.width, image.height);
				image.width         = smallest;
				image.height        = smallest;
			}

			auto texture = std::make_unique<blt::gfx::texture_gl2D>(image.width, image.height);
			texture->bind();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			for (auto& f : image.data)
				f = blt::linear_to_srgb(f);

			texture->upload(image.data.data(), image.width, image.height, GL_RGBA, GL_FLOAT);

			resources[namespace_str][image_name] = gpu_image_t{std::move(image), std::move(texture)};
		}

		for (auto& [image_name, image] : data.non_solid_images)
		{
			auto texture = std::make_unique<blt::gfx::texture_gl2D>(image.width, image.height);
			texture->bind();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			for (auto& f : image.data)
				f = blt::linear_to_srgb(f);
			texture->upload(image.data.data(), image.width, image.height, GL_RGBA, GL_FLOAT);

			non_solid_resources[namespace_str][image_name] = gpu_image_t{std::move(image), std::move(texture)};
		}
	}
	// can you tell I've stopped caring about code quality?
	auto minecraft_namespace = assets.assets.find("minecraft");
	if (minecraft_namespace != assets.assets.end())
	{
		auto plains_biome = minecraft_namespace->second.biome_colors.find("plains");
		if (plains_biome != minecraft_namespace->second.biome_colors.end())
			update_textures(plains_biome->second);
	}
}

std::vector<block_picker_data_t> gpu_asset_manager::get_icon_render_list()
{
	std::vector<block_picker_data_t> ret;
	for (const auto& [namespace_str, map] : resources)
	{
		for (const auto& [texture_str, gpu_image] : map)
			ret.emplace_back(texture_str, &gpu_image);
	}
	for (const auto& [namespace_str, map] : non_solid_resources)
	{
		for (const auto& [texture_str, gpu_image] : map)
			ret.emplace_back(texture_str, &gpu_image);
	}
	return ret;
}

inline float srgb_to_linear(const float v) noexcept
{
	return (v <= 0.04045f) ? (v / 12.92f) : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

void gpu_asset_manager::update_textures(biome_color_t color)
{
	static auto stmt = assets->db->prepare("SELECT DISTINCT b.namespace, b.block_name, s"
		".namespace, s"
		".name, s"
		".width, s.height FROM (SELECT * FROM solid_textures UNION SELECT * FROM non_solid_textures) AS s, "
		"models AS m, block_names as b "
		"WHERE s.namespace = m.texture_namespace AND "
		"s.name = m.texture AND "
		"m.namespace = b.model_namespace AND "
		"m.model = b.model");
	// hard coded because fuck mojang.
	auto rows = assets->get_rows<std::string, std::string, std::string, std::string, int, int>(stmt);

	const blt::hashset_t<std::string> grass_blocks{
		"minecraft:grass_block",
		"minecraft:short_grass",
		"minecraft:tall_grass",
		"minecraft:fern",
		"minecraft:large_fern",
		"minecraft:potted_fern",
		"minecraft:bush",
		"minecraft:sugar_cane"
	};
	const blt::hashset_t<std::string> leaves_blocks{
		"minecraft:oak_leaves",
		"minecraft:jungle_leaves",
		"minecraft:acacia_leaves",
		"minecraft:dark_oak_leaves",
		"minecraft:mangrove_leaves",
		"minecraft:spruce_leaves",
		"minecraft:birch_leaves",
		"minecraft:vine"
	};
	for (const auto& [block_namespace, block_name, namespace_str, texture_name, width, height] : rows)
	{
		auto fullname = block_namespace + ":" += block_name;

		if (namespace_str == "minecraft" && texture_name == "block/dirt")
			continue;

		std::optional<blt::vec3> fill_color;
		if (grass_blocks.contains(fullname))
			fill_color = color.grass_color;
		else if (leaves_blocks.contains(fullname))
			fill_color = color.leaves_color;

		if (fill_color)
		{
			if (texture_name == "block/grass_block_snow")
				continue;
			// BLT_TRACE("Updating block {} with model {}:{}", fullname, namespace_str, texture_name);

			auto iter = resources.find(namespace_str);
			if (iter == resources.end())
			{
				iter = non_solid_resources.find(namespace_str);
				if (iter == non_solid_resources.end())
				{
					BLT_WARN("[Namespace] Unable to find resource for {} texture {}:{}",
							 fullname,
							 namespace_str,
							 texture_name);
					continue;
				}
			}
			auto tex_iter = iter->second.find(texture_name);
			if (tex_iter == iter->second.end())
			{
				iter = non_solid_resources.find(namespace_str);
				if (iter == non_solid_resources.end())
				{
					BLT_WARN("[Namespace] Unable to find resource for {} texture {}:{}",
							 fullname,
							 namespace_str,
							 texture_name);
					continue;
				}
				tex_iter = iter->second.find(texture_name);
				if (tex_iter == iter->second.end())
				{
					BLT_WARN("[Texture] Unable to find resource for {} texture {}:{}",
							 fullname,
							 namespace_str,
							 texture_name);
					continue;
				}
			}

			auto& map        = tex_iter->second;
			map.image.width  = width;
			map.image.height = height;
			map.image.data   = assets->assets[namespace_str].images[texture_name].data;

			image_t* image = &map.image;

			for (int x = 0; x < width; x++)
			{
				for (int y = 0; y < height; y++)
				{
					auto i             = (y * width + x) * 4;
					image->data[i + 0] = image->data[i + 0] * fill_color->x();
					image->data[i + 1] = image->data[i + 1] * fill_color->y();
					image->data[i + 2] = image->data[i + 2] * fill_color->z();
				}
			}


			for (auto& f : map.image.data)
				f = std::pow(f, 1.0f / 2.2f);

			map.texture->upload(map.image.data.data(), map.image.width, map.image.height, GL_RGBA, GL_FLOAT);
		}
	}
}
