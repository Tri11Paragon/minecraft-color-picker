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

#ifndef ASSET_LOADER_H
#define ASSET_LOADER_H

#include <sql.h>
#include <blt/std/expected.h>

#include <utility>
#include <blt/math/vectors.h>
#include <blt/std/hashmap.h>

class load_failure_t
{
public:
	enum type_t
	{
		ASSET_FOLDER_NOT_FOUND,
		MODEL_FOLDER_NOT_FOUND,
		TEXTURE_FOLDER_NOT_FOUND,
		TAGS_FOLDER_NOT_FOUND,
		TAGS_BLOCKSTATES_NOT_FOUND,
		INVALID_BLOCKSTATE_FORMAT,
		INCORRECT_NAMESPACE,
		INCORRECT_TAG_FILE
	};

	load_failure_t(const type_t type): type{type} // NOLINT
	{}

	load_failure_t(const type_t type, const std::string& message): type{type}, message{message}
	{}

	operator type_t() const // NOLINT
	{
		return type;
	}

	[[nodiscard]] std::string to_string() const
	{
		switch (type)
		{
			case ASSET_FOLDER_NOT_FOUND:
				return "Asset folder could not be found. " + message.value_or("");
			case MODEL_FOLDER_NOT_FOUND:
				return "Model folder could not be found. " + message.value_or("");
			case TEXTURE_FOLDER_NOT_FOUND:
				return "Texture folder could not be found. " + message.value_or("");
			case TAGS_FOLDER_NOT_FOUND:
				return "Tags folder could not be found. " + message.value_or("");
			case INCORRECT_NAMESPACE:
				return "Namespace names of models, textures, or data files do not match! " + message.value_or("");
			case INVALID_BLOCKSTATE_FORMAT:
				return "Blockstate json file is not structured correctly. " + message.value_or("");
			default:
				return "Unknown failure type. " + message.value_or("");
		}
	}

private:
	type_t type;
	std::optional<std::string> message;
};


struct namespaced_object
{
	std::string namespace_str;
	std::string key_str;

	[[nodiscard]] std::string string() const
	{
		return namespace_str + ':' + key_str;
	}
};

struct model_data_t
{
	std::optional<namespaced_object> parent;
	std::optional<std::vector<namespaced_object>> textures;
};

struct tag_data_t
{
	blt::hashset_t<std::string> list;
};

struct block_state_t
{
	blt::hashmap_t<std::string, blt::hashset_t<std::string>> models;
};

struct biome_color
{
	blt::vec3 grass_color;
	blt::vec3 leaves_color;
};

struct namespace_data_t
{
	blt::hashmap_t<std::string, model_data_t> models;
	blt::hashmap_t<std::string, tag_data_t> tags;
	blt::hashmap_t<std::string, block_state_t> block_states;
	std::string asset_namespace_folder;
	std::string data_namespace_folder;

	std::string model_folder;
	std::string tag_folder;
	std::string texture_folder;

	blt::hashmap_t<std::string, std::string> textures;
	blt::hashmap_t<std::string, biome_color> biome_colors;
};

struct asset_data_t
{
	blt::hashmap_t<std::string, namespace_data_t> json_data;
	blt::hashmap_t<std::string, blt::hashset_t<std::string>> solid_textures_to_load;
	blt::hashmap_t<std::string, blt::hashset_t<std::string>> non_solid_textures_to_load;

	[[nodiscard]] std::vector<namespaced_object> resolve_parents(const namespaced_object& model) const;
};

class asset_loader_t
{
public:
	explicit asset_loader_t(std::string name);

	std::optional<load_failure_t> load_assets(const std::string& asset_folder, const std::optional<std::string>& data_folder = {});

	database_t& load_textures();

private:
	void process_texture(const statement_t& stmt, const std::string& namespace_str, const std::string& texture);

	asset_data_t data;
	database_t db;
	std::string name;
};

std::string block_pretty_name(std::string block_name);

#endif //ASSET_LOADER_H
