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

#include <sql.h>
#include <filesystem>
#include <blt/math/vectors.h>
#include <blt/std/hashmap.h>

database_t load_database(const std::filesystem::path& path);

struct image_t;

template<typename Value>
struct sequence_t
{
	sequence_t() = default;

	explicit sequence_t(const Value& value): m_value{value}
	{}

	sequence_t(const Value& value, sequence_t next): m_value{value}, m_next{std::make_unique<sequence_t>(std::move(next))}
	{}

	std::optional<sequence_t> next()
	{
		auto result = m_next == nullptr ? std::optional<sequence_t>{} : std::optional<sequence_t>{std::move(*m_next)};
		m_next = nullptr;
		return result;
	}

	const Value& value()
	{
		return m_value;
	}

private:
	Value m_value;
	std::unique_ptr<sequence_t> m_next = nullptr;
};

using sample_sequence_t = sequence_t<blt::vec3>;

sample_sequence_t make_sequence(std::initializer_list<blt::vec3> list);

struct sampler_interface_t
{
	virtual sample_sequence_t get_sampler() = 0;

	virtual ~sampler_interface_t() = default;
};

struct sampler_single_value_t final : sampler_interface_t
{
	explicit sampler_single_value_t(const blt::vec3 value): value{value}
	{}

	sample_sequence_t get_sampler() override
	{
		return sample_sequence_t{value};
	}

	blt::vec3 value;
};

struct sampler_one_point_t final : sampler_interface_t
{
	explicit sampler_one_point_t(const image_t& image);

	sample_sequence_t get_sampler() override
	{
		return sample_sequence_t{average};
	}
	blt::vec3 average;
};

struct comparator_interface_t
{
	virtual ~comparator_interface_t() = default;
	virtual blt::vec3 compare(sampler_interface_t& s1, sampler_interface_t& s2) = 0;

	blt::vec3 compare(sampler_interface_t& s1, const blt::vec3 point)
	{
		sampler_single_value_t value{point};
		return compare(s1, value);
	}
};

struct comparator_euclidean_t final : comparator_interface_t
{
	blt::vec3 compare(sampler_interface_t& s1, sampler_interface_t& s2) override;
};

struct image_t
{
	blt::i32 width, height;
	std::vector<float> data;

	[[nodiscard]] auto get_default_sampler() const
	{
		return sampler_one_point_t{*this};
	}
};

struct assets_t
{
	blt::hashmap_t<std::string, blt::hashmap_t<std::string, image_t>> images;
};

class data_loader_t
{
public:
	explicit data_loader_t(database_t& data): db{&data}
	{}

	[[nodiscard]] assets_t load() const;

private:
	database_t* db;
};

#endif //DATA_LOADER_H
