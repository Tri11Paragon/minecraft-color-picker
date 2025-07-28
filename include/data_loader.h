#pragma once
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

#ifndef DATA_LOADER_H
#define DATA_LOADER_H

#include <asset_loader.h>
#include <sql.h>
#include <filesystem>
#include <blt/math/vectors.h>
#include <blt/std/assert.h>
#include <blt/std/hashmap.h>

database_t load_database(const std::filesystem::path& path);

struct image_t;

blt::vec4 access_image(const image_t& image, blt::i32 x, blt::i32 y);

struct sampler_interface_t
{
	virtual ~sampler_interface_t() = default;
	[[nodiscard]] virtual std::vector<blt::vec3> get_values() const = 0;
};

struct sampler_single_value_t final : sampler_interface_t
{
	explicit sampler_single_value_t(const blt::vec3 value, blt::i32 samples = 1): value{value}, samples{samples}
	{}

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		std::vector<blt::vec3> values;
		for (blt::i32 i = 0; i < samples; i++)
			values.push_back(value);
		return values;
	}

	blt::vec3 value;
	blt::i32 samples;
};

struct sampler_oklab_op_t final : sampler_interface_t
{
	explicit sampler_oklab_op_t(const image_t& image, blt::i32 samples = 1);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return averages;
	}

	std::vector<blt::vec3> averages;
};

struct sampler_linear_rgb_op_t final : sampler_interface_t
{
	explicit sampler_linear_rgb_op_t(const image_t& image, blt::i32 samples = 1);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return averages;
	}

	std::vector<blt::vec3> averages;
};

struct sampler_srgb_op_t final : sampler_interface_t
{
	explicit sampler_srgb_op_t(const image_t& image, blt::i32 samples = 1);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return averages;
	}

	std::vector<blt::vec3> averages;
};

struct sampler_color_difference_oklab_t final : sampler_interface_t
{
	explicit sampler_color_difference_oklab_t(const image_t& image);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return color_differences;
	}

	std::vector<blt::vec3> color_differences;
};

struct sampler_kernel_filter_oklab_t final : sampler_interface_t
{
	explicit sampler_kernel_filter_oklab_t(const image_t& image);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return kernel_averages;
	}

	std::vector<blt::vec3> kernel_averages;
};

struct sampler_color_difference_rgb_t final : sampler_interface_t
{
	explicit sampler_color_difference_rgb_t(const image_t& image);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return color_differences;
	}

	std::vector<blt::vec3> color_differences;
};

struct sampler_kernel_filter_rgb_t final : sampler_interface_t
{
	explicit sampler_kernel_filter_rgb_t(const image_t& image);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return kernel_averages;
	}

	std::vector<blt::vec3> kernel_averages;
};

struct sampler_color_difference_srgb_t final : sampler_interface_t
{
	explicit sampler_color_difference_srgb_t(const image_t& image);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return color_differences;
	}

	std::vector<blt::vec3> color_differences;
};

struct sampler_kernel_filter_srgb_t final : sampler_interface_t
{
	explicit sampler_kernel_filter_srgb_t(const image_t& image);

	[[nodiscard]] std::vector<blt::vec3> get_values() const override
	{
		return kernel_averages;
	}

	std::vector<blt::vec3> kernel_averages;
};

struct comparator_interface_t
{
	virtual ~comparator_interface_t() = default;
	virtual float compare(sampler_interface_t& s1, sampler_interface_t& s2) = 0;

	float compare(sampler_interface_t& s1, const blt::vec3 point)
	{
		sampler_single_value_t value{point};
		return compare(s1, value);
	}
};

struct comparator_euclidean_t final : comparator_interface_t
{
	float compare(sampler_interface_t& s1, sampler_interface_t& s2) override;
};

struct comparator_mean_sample_euclidean_t final : comparator_interface_t
{
	float compare(sampler_interface_t& s1, sampler_interface_t& s2) override;
};

struct comparator_nearest_sample_euclidean_t final : comparator_interface_t
{
	float compare(sampler_interface_t& s1, sampler_interface_t& s2) override;
};

struct image_t
{
	blt::i32 width, height;
	std::vector<float> data;

	[[nodiscard]] auto get_default_sampler() const
	{
		return sampler_linear_rgb_op_t{*this};
	}
};

struct namespace_assets_t
{
	blt::hashmap_t<std::string, image_t> images;
	blt::hashmap_t<std::string, image_t> non_solid_images;
	blt::hashmap_t<std::string, biome_color_t> biome_colors;
	blt::hashmap_t<std::string, blt::hashset_t<std::string>> tags;
	blt::hashmap_t<std::string, blt::hashset_t<std::string>> block_to_textures;
};

struct assets_t
{
	database_t* db = nullptr;
	blt::hashmap_t<std::string, namespace_assets_t> assets;
	assets_t() = default;

	explicit assets_t(database_t& db): db{&db}
	{}

	std::vector<std::tuple<std::string, std::string>>& get_biomes();

	template <typename... Types>
	std::vector<std::tuple<Types...>> get_rows(const std::string& sql)
	{
		if (db == nullptr)
			BLT_ABORT("Database is null. Did you forget to load it?");

		auto stmt = db->prepare(sql);
		return get_rows<Types...>(stmt);
	}

	template <typename... Types>
	std::vector<std::tuple<Types...>> get_rows(statement_t& stmt)
	{
		if (db == nullptr)
			BLT_ABORT("Database is null. Did you forget to load it?");
		stmt.bind();
		std::vector<std::tuple<Types...>> results;
		while (stmt.execute().has_row())
			results.push_back(stmt.fetch().get<Types...>());
		return results;
	}
};

class data_loader_t
{
public:
	explicit data_loader_t(database_t data): db{std::move(data)}
	{}

	[[nodiscard]] assets_t load();

private:
	database_t db;
};

#endif //DATA_LOADER_H
