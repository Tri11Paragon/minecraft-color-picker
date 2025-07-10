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
#include <fstream>
#include <nlohmann/json.hpp>
#include <blt/logging/logging.h>
#include <blt/std/hashmap.h>
#include <blt/std/string.h>
using json = nlohmann::json;

asset_loader_t::asset_loader_t(std::string asset_folder, std::string name, std::optional<std::string> data_folder): data{
	database_t{name + ".assets"}, std::move(asset_folder), std::move(data_folder), std::move(name)
}
{}

blt::expected<assets_data_t, load_failure_t> asset_loader_t::load_assets()
{
	if (!contains)
		return blt::unexpected(load_failure_t::ASSETS_ALREADY_LOADED);
	if (!std::filesystem::exists(data.asset_folder))
		return blt::unexpected(load_failure_t::ASSET_FOLDER_NOT_FOUND);

	/*
	 * Tables
	 */
	auto texture_table = data.db.builder().create_table("textures");
	texture_table.with_column<std::string>("name").primary_key();
	texture_table.with_column<blt::u32>("width").not_null();
	texture_table.with_column<blt::u32>("height").not_null();
	texture_table.with_column<std::byte*>("data").not_null();
	texture_table.build().execute();

	std::optional<std::filesystem::path> model_folder;
	std::optional<std::filesystem::path> texture_folder;
	std::optional<std::filesystem::path> tags_folder;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(data.asset_folder))
	{
		if (!entry.is_directory())
			continue;
		if (entry.path().filename().compare("models") == 0)
		{
			model_folder = entry.path();
		}
		if (entry.path().filename().compare("textures") == 0)
		{
			texture_folder = entry.path();
		}
	}

	if (!model_folder)
		return blt::unexpected(load_failure_t::MODEL_FOLDER_NOT_FOUND);

	if (!texture_folder)
		return blt::unexpected(load_failure_t::TEXTURE_FOLDER_NOT_FOUND);

	if (!exists(*model_folder / "block"))
		return blt::unexpected(load_failure_t::MODEL_FOLDER_NOT_FOUND);

	if (!exists(*texture_folder / "block"))
		return blt::unexpected(load_failure_t::TEXTURE_FOLDER_NOT_FOUND);

	const auto namespace_name = model_folder->parent_path().filename();

	if (data.data_folder)
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator(*data.data_folder))
		{
			if (!entry.is_directory())
				continue;
			if (entry.path().string().find("tags") != std::string::npos)
			{
				tags_folder = entry.path();
				break;
			}
		}
	}

	if (data.data_folder && !tags_folder)
		return blt::unexpected(load_failure_t::TAGS_FOLDER_NOT_FOUND);

	if (data.data_folder && !exists(*tags_folder / "block"))
		return blt::unexpected(load_failure_t::TAGS_FOLDER_NOT_FOUND);

	BLT_INFO("Loading assets '{}' for namespace '{}'", data.name, namespace_name.string());
	if (texture_folder->parent_path().filename() != namespace_name)
		return blt::unexpected(load_failure_t::INCORRECT_NAMESPACE);

	blt::hashmap_t<std::string, namespace_data_t> namespaced_models;

	for (auto entry : std::filesystem::recursive_directory_iterator(*model_folder / "block"))
	{
		if (!entry.path().has_extension())
			continue;
		if (!entry.is_regular_file())
			continue;
		if (entry.path().extension().compare(".json") != 0)
			continue;
		std::filesystem::path relative_path;
		{
			auto p_begin = entry.path().begin();
			auto p_end = entry.path().end();
			auto m_begin = model_folder->begin();
			while (p_begin != p_end && m_begin != model_folder->end() && *p_begin == *m_begin)
			{
				++p_begin;
				++m_begin;
			}
			for (; p_begin != p_end; ++p_begin)
				relative_path /= p_begin->stem();
		}

		std::ifstream file{entry.path()};
		json data = json::parse(file);

		std::optional<namespaced_object> parent;
		std::vector<namespaced_object> textures;

		if (data.contains("parent"))
		{
			const auto lparent = data["parent"].get<std::string>();
			const auto parts = blt::string::split_sv(lparent, ":");
			if (parts.size() == 1)
				parent = namespaced_object{namespace_name.string(), std::string{parts[0]}};
			else
				parent = namespaced_object{std::string{parts[0]}, std::string{parts[1]}};
		}
		if (data.contains("textures"))
		{
			for (const auto& texture_entry : data["textures"])
			{
				auto str = texture_entry.get<std::string>();
				// not a real texture we care about
				if (blt::string::starts_with(str, "#"))
					continue;
				const auto texture_parts = blt::string::split_sv(str, ":");
				if (texture_parts.size() == 1)
					textures.push_back(namespaced_object{namespace_name.string(), std::string{texture_parts[0]}});
				else
					textures.push_back(namespaced_object{std::string{texture_parts[0]}, std::string{texture_parts[1]}});
			}
		}

		if (!namespaced_models.contains(namespace_name.string()))
			namespaced_models[namespace_name.string()] = {};
		namespaced_models[namespace_name.string()].models.insert({
			relative_path.string(), model_data_t{parent, textures.empty() ? std::nullopt : std::move(textures)}
		});
	}

	contains = false;
	return std::move(data);
}
