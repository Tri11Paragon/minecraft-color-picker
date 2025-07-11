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
#include <blt/gfx/stb/stb_image.h>
#include <nlohmann/json.hpp>
#include <blt/logging/logging.h>
#include <blt/std/hashmap.h>
#include <blt/std/string.h>

using json = nlohmann::json;

inline namespaced_object empty_object{"NULL", "NULL"};

asset_loader_t::asset_loader_t(std::string name):
	db{name + ".assets"}, name{std::move(name)}
{}

std::optional<load_failure_t> asset_loader_t::load_assets(const std::string& asset_folder, const std::optional<std::string>& data_folder)
{
	if (!std::filesystem::exists(asset_folder))
		return load_failure_t::ASSET_FOLDER_NOT_FOUND;

	std::optional<std::filesystem::path> model_folder;
	std::optional<std::filesystem::path> texture_folder;
	std::optional<std::filesystem::path> tags_folder;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(asset_folder))
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
		return load_failure_t::MODEL_FOLDER_NOT_FOUND;

	if (!texture_folder)
		return load_failure_t::TEXTURE_FOLDER_NOT_FOUND;

	if (!exists(*model_folder / "block"))
		return load_failure_t::MODEL_FOLDER_NOT_FOUND;

	if (!exists(*texture_folder / "block"))
		return load_failure_t::TEXTURE_FOLDER_NOT_FOUND;

	const auto namespace_name = model_folder->parent_path().filename();

	if (data_folder)
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator(*data_folder))
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

	if (data_folder && !tags_folder)
		return load_failure_t::TAGS_FOLDER_NOT_FOUND;

	if (data_folder && !exists(*tags_folder / "block"))
		return load_failure_t::TAGS_FOLDER_NOT_FOUND;

	if (data_folder)
		data.namespace_data_folders[namespace_name] = data_folder->parent_path().string();

	data.namespace_asset_folders[namespace_name] = model_folder->parent_path().string();
	data.namespace_texture_folders[namespace_name] = texture_folder->string();

	BLT_INFO("Loading assets '{}' for namespace '{}'", name, namespace_name.string());
	if (texture_folder->parent_path().filename() != namespace_name)
		return load_failure_t::INCORRECT_NAMESPACE;

	blt::hashmap_t<std::string, namespace_data_t>& namespaced_models = data.json_data;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(*model_folder / "block"))
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
			relative_path.string(),
			model_data_t{parent, textures.empty() ? std::optional<std::vector<namespaced_object>>{} : std::optional{std::move(textures)}}
		});
	}

	auto& map = data.json_data[namespace_name.string()];

	std::vector<namespaced_object> textures_to_load;

	for (auto& [name, model] : map.models)
	{
		auto parents = data.resolve_parents(model.parent.value_or(empty_object));
		bool solid = false;
		for (const auto& [namespace_str, key_str] : parents)
		{
			if (namespace_str == "minecraft" && key_str == "block/cube")
			{
				if (model.textures)
				{
					for (auto& texture : *model.textures)
						data.solid_textures_to_load[texture.namespace_str] = texture.key_str;
				}
				solid = true;
				break;
			}
		}
		if (!solid)
		{
			if (model.textures)
			{
				for (auto& texture : *model.textures)
					data.non_solid_textures_to_load[texture.namespace_str] = texture.key_str;
			}
		}
	}

	return {};
}

asset_data_t& asset_loader_t::load_textures()
{
	auto texture_table = db.builder().create_table("solid_textures");
	texture_table.with_column<std::string>("namespace").primary_key();
	texture_table.with_column<std::string>("name").primary_key();
	texture_table.with_column<blt::u32>("width").not_null();
	texture_table.with_column<blt::u32>("height").not_null();
	texture_table.with_column<std::byte*>("data").not_null();
	texture_table.build().execute();

	for (const auto& [namespace_str, texture] : data.solid_textures_to_load)
	{
		auto texture_path = std::filesystem::path(data.namespace_texture_folders.at(namespace_str));
		texture_path /= texture;

	}

	return data;
}

std::vector<namespaced_object> asset_data_t::resolve_parents(const namespaced_object& model) const
{
	std::vector<namespaced_object> parents;
	auto current_parent = model;
	while (json_data.contains(current_parent.namespace_str) && json_data.at(current_parent.namespace_str).models.contains(current_parent.key_str))
	{
		const auto& [parent, textures] = json_data.at(current_parent.namespace_str).models.at(current_parent.key_str);
		parents.push_back(current_parent);
		if (parent)
			current_parent = *parent;
		else
			break;
	}
	return parents;
}
