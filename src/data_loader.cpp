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

assets_t data_loader_t::load() const
{
	constexpr auto SQL = "SELECT * FROM solid_textures";
	const auto stmt = db->prepare(SQL);

	assets_t assets;

	while (stmt.execute().has_row())
	{
		auto column = stmt.fetch();

		const auto [namespace_str, name, width, height, ptr] = column.get<std::string, std::string, blt::i32, blt::i32, const std::byte*>();

		const auto size_floats = width * height * 4;

		auto& namespace_hash = assets.images[namespace_str];
		auto& image = namespace_hash[name];
		image.width = width;
		image.height = height;
		image.data.resize(size_floats);
		std::memcpy(image.data.data(), ptr, size_floats * sizeof(float));
	}

	return assets;
}

struct sample_pair
{
	sample_pair(sampler_interface_t& s1, sampler_interface_t& s2): sample1{s1.get_sample()}, sample2{s2.get_sample()}
	{}

	[[nodiscard]] bool has_value() const
	{
		return sample1.has_value() && sample2.has_value();
	}

	explicit operator bool() const
	{
		return has_value();
	}

	std::optional<blt::vec3> sample1;
	std::optional<blt::vec3> sample2;
};

sampler_one_point_t::sampler_one_point_t(const image_t& image)
{
	float alpha = 0;
	for (blt::i32 y = 0; y < image.height; y++)
	{
		for (blt::i32 x = 0; x < image.width; x++)
		{
			const auto a = image.data[y * image.width + x + 3];
			average += blt::vec3{
				image.data[y * image.width + x + 0], image.data[y * image.width + x + 1], image.data[y * image.width + x + 2]
			} * a;
			alpha += a;
		}
	}
	if (alpha != 0)
		average = average / (alpha / 3);
}

blt::vec3 comparator_euclidean_t::compare(sampler_interface_t& s1, sampler_interface_t& s2)
{
	s1.clear();
	s2.clear();
	blt::vec3 total{};
	while (auto sampler = sample_pair{s1, s2})
	{
		auto [sample1, sample2] = sampler;
		const auto dif = *sample1 - *sample2;
		total += dif * dif;
	}
	return {std::sqrt(total[0]), std::sqrt(total[1]), std::sqrt(total[2])};
}
