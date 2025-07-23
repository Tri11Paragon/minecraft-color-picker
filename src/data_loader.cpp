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
#include <blt/math/log_util.h>
#include <data_loader.h>
#include <blt/logging/logging.h>

database_t load_database(const std::filesystem::path& path)
{
	database_t db{path.string()};
	return db;
}

blt::vec4 access_image(const image_t& image, const blt::i32 x, const blt::i32 y)
{
	return {
		image.data[(y * image.width + x) * 4 + 0],
		image.data[(y * image.width + x) * 4 + 1],
		image.data[(y * image.width + x) * 4 + 2],
		image.data[(y * image.width + x) * 4 + 3]
	};
}

assets_t data_loader_t::load()
{
	constexpr auto SQL  = "SELECT * FROM solid_textures";
	auto           stmt = db.prepare(SQL);

	assets_t assets{db};

	while (stmt.execute().has_row())
	{
		auto column = stmt.fetch();

		const auto [namespace_str, name, width, height, ptr] = column.get<
			std::string, std::string, blt::i32, blt::i32, const std::byte*>();

		const auto size_floats = width * height * 4;

		auto& namespace_hash = assets.assets[namespace_str].images;
		auto& image          = namespace_hash[name];
		image.width          = width;
		image.height         = height;
		image.data.resize(size_floats);
		std::memcpy(image.data.data(), ptr, size_floats * sizeof(float));
	}

	stmt = db.prepare("SELECT * FROM non_solid_textures");
	stmt.bind();
	while (stmt.execute().has_row())
	{
		auto column = stmt.fetch();

		const auto [namespace_str, name, width, height, ptr] = column.get<
			std::string, std::string, blt::i32, blt::i32, const std::byte*>();

		const auto size_floats = width * height * 4;

		auto& namespace_hash = assets.assets[namespace_str].non_solid_images;
		auto& image          = namespace_hash[name];
		image.width          = width;
		image.height         = height;
		image.data.resize(size_floats);
		std::memcpy(image.data.data(), ptr, size_floats * sizeof(float));
	}

	stmt = db.prepare("SELECT * FROM biome_color");
	stmt.bind();
	while (stmt.execute().has_row())
	{
		auto       column                                                                          = stmt.fetch();
		const auto [namespace_str, biome, grass_r, grass_g, grass_b, leaves_r, leaves_g, leaves_b] = column.get<
			std::string, std::string, float, float, float, float, float, float>();

		const blt::vec3 grass{grass_r, grass_g, grass_b};
		const blt::vec3 leaves{leaves_r, leaves_g, leaves_b};

		assets.assets[namespace_str].biome_colors[biome] = {grass, leaves};
	}

	blt::hashmap_t<std::string, blt::hashmap_t<std::string, blt::hashset_t<std::string>>> tags;
	stmt = db.prepare("SELECT namespace,tag,block FROM tags");
	stmt.bind();
	while (stmt.execute().has_row())
	{
		auto       column                      = stmt.fetch();
		const auto [namespace_str, tag, block] = column.get<std::string, std::string, std::string>();
		tags[namespace_str][tag].insert(block);
	}

	std::vector<std::tuple<std::string, std::string, std::string>> tag_list;
	for (const auto& [namespace_str, tag_map] : tags)
	{
		for (const auto& [tag, block_set] : tag_map)
		{
			for (const auto& block : block_set) { tag_list.emplace_back(namespace_str, tag, block); }
		}
	}

	while (!tag_list.empty())
	{
		auto [namespace_str, tag, block] = tag_list.back();
		tag_list.pop_back();

		if (blt::string::starts_with(block, '#'))
		{
			const auto parts = blt::string::split(block.substr(1), ':');
			if (parts.size() == 1)
			{
				for (const auto& block2 : tags[namespace_str][parts[0]])
					tag_list.emplace_back(namespace_str, tag, block2);
			} else
			{
				for (const auto& block2 : tags[parts[0]][parts[1]])
					tag_list.emplace_back(namespace_str, tag, block2);
			}
		} else
		{
			const auto parts = blt::string::split(block, ':');
			if (parts.size() == 1)
				assets.assets[namespace_str].tags[tag].insert(namespace_str + ":" += parts[0]);
			else
				assets.assets[namespace_str].tags[tag].insert(block);
		}
	}

	stmt = db.prepare("SELECT DISTINCT b.namespace, b.block_name, t.namespace, t.name "
		"FROM (SELECT * FROM non_solid_textures UNION SELECT * FROM solid_textures) as t "
		"INNER JOIN models as m ON m.texture_namespace = t.namespace AND m.texture = t.name "
		"INNER JOIN block_names as b ON m.namespace = b.model_namespace AND m.model = b.model");
	stmt.bind();
	while (stmt.execute().has_row())
	{
		auto       column                                                  = stmt.fetch();
		const auto [namespace_str, block_name, texture_namespace, texture] = column.get<
			std::string, std::string, std::string, std::string>();
		assets.assets[namespace_str].block_to_textures[block_name].insert(texture_namespace + ":" += texture);
	}

	return assets;
}

sampler_oklab_op_t::sampler_oklab_op_t(const image_t& image, const blt::i32 samples)
{
	const auto x_step = image.width / samples;
	const auto y_step = image.height / samples;
	for (blt::i32 y_pos = 0; y_pos < samples; y_pos++)
	{
		for (blt::i32 x_pos = 0; x_pos < samples; x_pos++)
		{
			float     alpha = 0;
			blt::vec3 average{};
			for (blt::i32 y = y_step * y_pos; y < std::min(image.height, y_step * (y_pos + 1)); y++)
			{
				for (blt::i32 x = x_step * x_pos; x < std::min(image.width, x_step * (x_pos + 1)); x++)
				{
					auto value = access_image(image, x, y);

					const auto a = value.a();
					average += blt::make_vec3(value).linear_rgb_to_oklab() * a;
					alpha += a;
				}
			}
			if (alpha != 0)
				average = average / alpha;
			averages.push_back(average);
		}
	}
}

sampler_linear_rgb_op_t::sampler_linear_rgb_op_t(const image_t& image, const blt::i32 samples)
{
	const auto x_step = image.width / samples;
	const auto y_step = image.height / samples;
	for (blt::i32 x_pos = 0; x_pos < samples; x_pos++)
	{
		for (blt::i32 y_pos = 0; y_pos < samples; y_pos++)
		{
			float     alpha = 0;
			blt::vec3 average{};
			for (blt::i32 y = y_step * y_pos; y < std::min(image.height, y_step * (y_pos + 1)); y++)
			{
				for (blt::i32 x = x_step * x_pos; x < std::min(image.width, x_step * (x_pos + 1)); x++)
				{
					auto value = access_image(image, x, y);

					const auto a = value.a();
					average += blt::make_vec3(value) * a;
					alpha += a;
				}
			}
			if (alpha != 0)
				average = average / alpha;
			averages.push_back(average);
		}
	}
}

sampler_color_difference_op_t::sampler_color_difference_op_t(const image_t& image)
{
	const sampler_oklab_op_t oklab{image, 1};
	auto                     average_color = oklab.get_values()[0];
	float                    alpha         = 0;
	blt::vec3                color_difference;
	for (blt::i32 y = 0; y < image.height; y++)
	{
		for (blt::i32 x = 0; x < image.width; x++)
		{
			auto value = access_image(image, x, y);

			const auto a    = value.a();
			auto       diff = average_color - blt::make_vec3(value).linear_rgb_to_oklab();
			color_difference += diff * diff * a;
			alpha += a;
		}
	}
	if (alpha != 0)
		color_difference = color_difference.sqrt() / alpha;
	color_differences.push_back(color_difference);
}

sampler_kernel_filter_op_t::sampler_kernel_filter_op_t(const image_t& image)
{
	struct kernel
	{
		explicit kernel(const blt::i32 size = 2): size(size) {}

		[[nodiscard]] blt::vec3 calculate(const image_t& image, const blt::i32 x, const blt::i32 y) const
		{
			blt::vec3 avg;
			float     alpha = 0.0f;
			for (blt::i32 i = -size; i <= size; i++)
			{
				for (blt::i32 j = -size; j <= size; j++)
				{
					const auto px = x + i;
					const auto py = y + i;
					if (px < 0 || px >= image.width)
						continue;
					if (py < 0 || py >= image.height)
						continue;
					auto value = access_image(image, px, py);
					avg += blt::make_vec3(value).linear_rgb_to_oklab() * value.a();
					alpha += value.a();
				}
			}
			return avg / alpha;
		}

		blt::i32 size;
	};

	const sampler_oklab_op_t oklab{image, 1};
	const auto               average_color = oklab.get_values()[0];
	blt::vec3                total;
	for (blt::i32 y = 0; y < image.height; y++)
	{
		for (blt::i32 x = 0; x < image.width; x++)
		{
			const auto diff = average_color - kernel{}.calculate(image, x, y);
			total += diff * diff;
		}
	}
	kernel_averages.push_back(total.sqrt() / (image.width * image.height));
}

float comparator_euclidean_t::compare(sampler_interface_t& s1, sampler_interface_t& s2)
{
	const auto s1_v = s1.get_values();
	const auto s2_v = s2.get_values();
	BLT_ASSERT(s1_v.size() == s2_v.size() && s1_v.size() == 1 && "Please use other comparators for multi-sample sets!");
	const blt::vec3 diff  = s1_v.front() - s2_v.front();
	float           total = 0;
	for (const float f : diff)
		total += f * f;
	return std::sqrt(total);
}

float comparator_mean_sample_euclidean_t::compare(sampler_interface_t& s1, sampler_interface_t& s2)
{
	const auto s1_v = s1.get_values();
	const auto s2_v = s2.get_values();
	BLT_ASSERT(s1_v.size() == s2_v.size() && "samplers must provide the same number of elements");
	float total = 0;
	for (const auto& [a, b] : blt::in_pairs(s1_v, s2_v))
	{
		const blt::vec3 diff   = s1_v.front() - s2_v.front();
		float           ltotal = 0;
		for (const float f : diff)
			ltotal += f * f;
		total += std::sqrt(ltotal);
	}
	return total / static_cast<float>(s1_v.size());
}

float comparator_nearest_sample_euclidean_t::compare(sampler_interface_t& s1, sampler_interface_t& s2)
{
	const auto s1_v = s1.get_values();
	const auto s2_v = s2.get_values();
	BLT_ASSERT(s1_v.size() == s2_v.size() && "samplers must provide the same number of elements");
	float best = std::numeric_limits<float>::max();

	for (const auto& [a, b] : blt::in_pairs(s1_v, s2_v))
	{
		const blt::vec3 diff   = s1_v.front() - s2_v.front();
		float           ltotal = 0;
		for (const float f : diff)
			ltotal += f * f;
		ltotal = std::sqrt(ltotal);
		if (ltotal < best)
			best = ltotal;
	}
	return best;
}

std::vector<std::tuple<std::string, std::string>>& assets_t::get_biomes()
{
	static std::vector<std::tuple<std::string, std::string>> ret;
	if (ret.empty())
	{
		for (const auto& [namespace_str, data] : assets)
		{
			for (const auto& [biome_str, biome_color] : data.biome_colors)
			{
				ret.emplace_back(namespace_str, biome_str);
			}
		}
		std::sort(ret.begin(),
				  ret.end(),
				  [](const auto& a, const auto& b) {
					  auto [ns1, biome1] = a;
					  auto [ns2, biome2] = b;
					  auto combined1     = ns1 + ':' + biome1;
					  auto combined2     = ns2 + ':' + biome2;
					  return combined1.compare(combined2) < 0;
				  });
	}
	return ret;
}
