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

assets_t data_loader_t::load()
{
	constexpr auto SQL = "SELECT * FROM solid_textures";
	auto stmt = db.prepare(SQL);

	assets_t assets{db};

	while (stmt.execute().has_row())
	{
		auto column = stmt.fetch();

		const auto [namespace_str, name, width, height, ptr] = column.get<std::string, std::string, blt::i32, blt::i32, const std::byte*>();

		const auto size_floats = width * height * 4;

		auto& namespace_hash = assets.assets[namespace_str].images;
		auto& image = namespace_hash[name];
		image.width = width;
		image.height = height;
		image.data.resize(size_floats);
		std::memcpy(image.data.data(), ptr, size_floats * sizeof(float));
	}

	stmt = db.prepare("SELECT * FROM non_solid_textures");
	stmt.bind();
	while (stmt.execute().has_row())
	{
		auto column = stmt.fetch();

		const auto [namespace_str, name, width, height, ptr] = column.get<std::string, std::string, blt::i32, blt::i32, const std::byte*>();

		const auto size_floats = width * height * 4;

		auto& namespace_hash = assets.assets[namespace_str].non_solid_images;
		auto& image = namespace_hash[name];
		image.width = width;
		image.height = height;
		image.data.resize(size_floats);
		std::memcpy(image.data.data(), ptr, size_floats * sizeof(float));
	}

	stmt = db.prepare("SELECT * FROM biome_color");
	stmt.bind();
	while (stmt.execute().has_row())
	{
		auto column = stmt.fetch();
		const auto [namespace_str, biome, grass_r, grass_g, grass_b, leaves_r, leaves_g, leaves_b] = column.get<
			std::string, std::string, float, float, float, float, float, float>();

		const blt::vec3 grass{grass_r, grass_g, grass_b};
		const blt::vec3 leaves{leaves_r, leaves_g, leaves_b};

		assets.assets[namespace_str].biome_colors[biome] = {grass, leaves};
	}

	return assets;
}

sampler_oklab_op_t::sampler_oklab_op_t(const image_t& image, const blt::i32 samples)
{
	const auto x_step = image.width / samples;
	const auto y_step = image.height / samples;
	for (blt::i32 x_pos = 0; x_pos < samples; x_pos++)
	{
		for (blt::i32 y_pos = 0; y_pos < samples; y_pos++)
		{
			float alpha = 0;
			blt::vec3 average;
			blt::i32 y = y_step * y_pos;
			blt::i32 x = x_step * x_pos;
			for (; y < std::min(image.height, y_step * (y_pos + 1)); y++)
			{
				for (; x < std::min(image.width, x_step * (x_pos + 1)); x++)
				{
					const blt::vec3 value{
						image.data[(y * image.width + x) * 4 + 0], image.data[(y * image.width + x) * 4 + 1],
						image.data[(y * image.width + x) * 4 + 2]
					};

					const auto a = image.data[(y * image.width + x) * 4 + 3];
					average += value.linear_rgb_to_oklab() * a;
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
			float alpha = 0;
			blt::vec3 average;
			blt::i32 y = y_step * y_pos;
			blt::i32 x = x_step * x_pos;
			for (; y < std::min(image.height, y_step * (y_pos + 1)); y++)
			{
				for (; x < std::min(image.width, x_step * (x_pos + 1)); x++)
				{
					const blt::vec3 value{
						image.data[(y * image.width + x) * 4 + 0], image.data[(y * image.width + x) * 4 + 1],
						image.data[(y * image.width + x) * 4 + 2]
					};

					const auto a = image.data[(y * image.width + x) * 4 + 3];
					average += value * a;
					alpha += a;
				}
			}
			if (alpha != 0)
				average = average / alpha;
			averages.push_back(average);
		}
	}
}

sampler_color_difference_op_t::sampler_color_difference_op_t(const image_t& image, const std::vector<blt::vec3>& average_color,
															const blt::i32 samples)
{
	blt::i32 current_sample = 0;
	const auto x_step = image.width / samples;
	const auto y_step = image.height / samples;
	for (blt::i32 x_pos = 0; x_pos < samples; x_pos++)
	{
		for (blt::i32 y_pos = 0; y_pos < samples; y_pos++)
		{
			float alpha = 0;
			blt::vec3 color_difference;
			blt::i32 y = y_step * y_pos;
			blt::i32 x = x_step * x_pos;
			for (; y < std::min(image.height, y_step * (y_pos + 1)); y++)
			{
				for (; x < std::min(image.width, x_step * (x_pos + 1)); x++)
				{
					const blt::vec3 value{
						image.data[(y * image.width + x) * 4 + 0], image.data[(y * image.width + x) * 4 + 1],
						image.data[(y * image.width + x) * 4 + 2]
					};

					const auto a = image.data[(y * image.width + x) * 4 + 3];
					auto diff = (average_color[current_sample++] - value) * a;
					color_difference += diff * diff;
					alpha += a;
				}
			}
			if (alpha != 0)
				color_difference = color_difference.sqrt() / alpha;
			color_differences.push_back(color_difference);
		}
	}
}

blt::vec3 comparator_euclidean_t::compare(sampler_interface_t& s1, sampler_interface_t& s2)
{
	auto s1_v = s1.get_values();
	auto s2_v = s2.get_values();
	auto s1_i = s1_v.begin();
	auto s2_i = s2_v.begin();
	blt::vec3 total{};
	for (; s1_i != s1_v.end() && s2_i != s2_v.end(); ++s1_i, ++s2_i)
	{
		const auto sample1 = *s1_i;
		const auto sample2 = *s2_i;
		const auto dif = sample1 - sample2;
		total += dif * dif;
	}
	return {std::sqrt(total[0]), std::sqrt(total[1]), std::sqrt(total[2])};
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
		std::sort(ret.begin(), ret.end(), [](const auto& a, const auto& b) {
			auto [ns1, biome1] = a;
			auto [ns2, biome2] = b;
			auto combined1 = ns1 + ':' + biome1;
			auto combined2 = ns2 + ':' + biome2;
			return combined1.compare(combined2) < 0;
		});
	}
	return ret;
}
