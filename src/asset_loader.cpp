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

asset_loader_t::asset_loader_t(std::string folder, std::string name): db{name + ".assets"}, folder{std::move(folder)}, name{std::move(name)}
{
	if (!std::filesystem::exists(folder))
		throw std::runtime_error("Folder does not exist!");

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

	auto block_model_folder = folder + "minecraft/models/block/";
	for (const auto& entry : std::filesystem::directory_iterator(block_model_folder))
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

	if (!model_folder)
		throw std::runtime_error("Could not find model folder!");
	if (!texture_folder)
		throw std::runtime_error("Could not find texture folder!");

}
