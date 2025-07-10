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

#include <utility>
#include <filesystem>
#include <istream>

asset_loader_t::asset_loader_t(std::string asset_folder, std::string name, std::optional<std::string> data_folder): data{database_t{name + ".assets"},
																														std::move(asset_folder),
																														std::move(data_folder),
																														std::move(name)}
{
}

blt::expected<assets_data_t, load_failure_t> asset_loader_t::load_assets()
{
	if (!contains)
		return blt::unexpected(load_failure_t::ASSETS_ALREADY_LOADED);
	if (!std::filesystem::exists(data.asset_folder))
		return blt::unexpected(load_failure_t::ASSET_FOLDER_NOT_FOUND);

	/*
	 * Tables
	 */
	auto texture_table = db.builder().create_table("textures");
	texture_table.with_column<std::string>("name").primary_key();
	texture_table.with_column<blt::u32>("width").not_null();
	texture_table.with_column<blt::u32>("height").not_null();
	texture_table.with_column<std::byte*>("data").not_null();
	texture_table.build().execute();

	std::optional<std::filesystem::path> model_folder;
	std::optional<std::filesystem::path> texture_folder;
	std::optional<std::filesystem::path> tags_folder;

	for (const auto& entry : std::filesystem::directory_iterator(data.asset_folder))
	{
		if (!entry.is_directory())
			continue;
		if (entry.path().string().find("models") != std::string::npos)
		{
			model_folder = entry.path();
		}
		if (entry.path().string().find("textures") != std::string::npos)
		{
			texture_folder = entry.path();
		}
	}

	if (data.data_folder)
	{
		for (const auto& entry : std::filesystem::directory_iterator(*data.data_folder))
		{
			if (!entry.is_directory())
				continue;
			if (entry.path().string().find("tags") != std::string::npos)
			{
				tags_folder = entry.path();
			}
		}
	}

	if (!model_folder)
		return blt::unexpected(load_failure_t::MODEL_FOLDER_NOT_FOUND);
	if (!texture_folder)
		return blt::unexpected(load_failure_t::TEXTURE_FOLDER_NOT_FOUND);
	if (data.data_folder && !tags_folder)
		return blt::unexpected(load_failure_t::TAGS_FOLDER_NOT_FOUND);

	auto namespace_name = model_folder->parent_path();

	contains = false;
	return std::move(data);
}
