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
#include <blt/std/hashmap.h>

class load_failure_t
{
public:
	enum type_t
	{
		ASSETS_ALREADY_LOADED,
		ASSET_FOLDER_NOT_FOUND,
		MODEL_FOLDER_NOT_FOUND,
		TEXTURE_FOLDER_NOT_FOUND,
		TAGS_FOLDER_NOT_FOUND,
		INCORRECT_NAMESPACE
	};

	load_failure_t(const type_t type): type{type} // NOLINT
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
				return "Asset folder could not be found";
			case MODEL_FOLDER_NOT_FOUND:
				return "Model folder could not be found";
			case TEXTURE_FOLDER_NOT_FOUND:
				return "Texture folder could not be found";
			case TAGS_FOLDER_NOT_FOUND:
				return "Tags folder could not be found";
			case INCORRECT_NAMESPACE:
				return "Namespace names of models, textures, or data files do not match!";
			default:
				return "Unknown failure type";
		}
	}

private:
	type_t type;
};

struct namespaced_object
{
	std::string namespace_str;
	std::string key_str;
};

struct model_data_t
{
	std::optional<namespaced_object> parent;
	std::optional<std::vector<namespaced_object>> textures;
};

struct namespace_data_t
{
	blt::hashmap_t<std::string, model_data_t> models;
};

struct assets_data_t
{
	database_t db;
	std::string asset_folder;
	std::optional<std::string> data_folder;
	std::string name;

	assets_data_t(database_t&& db, std::string asset_folder, std::optional<std::string> data_folder, std::string name) : db{std::move(db)},
																														asset_folder{
																															std::move(asset_folder)
																														},
																														data_folder{
																															std::move(data_folder)
																														}, name{std::move(name)}
	{}
};

class asset_loader_t
{
public:
	asset_loader_t(std::string folder, std::string name, std::optional<std::string> data_folder = std::nullopt);

	blt::expected<assets_data_t, load_failure_t> load_assets();

private:
	// feels like a weird hack?
	bool contains = true;
	assets_data_t data;
};

#endif //ASSET_LOADER_H
