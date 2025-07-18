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
#include <blt/std/ranges.h>
#include <blt/std/string.h>

using json = nlohmann::json;

inline namespaced_object empty_object{"NULL", "NULL"};

struct search_for_t
{
	search_for_t(const json& obj, std::string search_tag): search_tag{std::move(search_tag)}
	{
		objects.push_back(obj);
	}

	template <typename T = std::string>
	std::optional<T> next()
	{
		while (true)
		{
			if (objects.empty())
				return {};
			if (!current_iter)
				current_iter = objects.front().items().begin();

			while (true)
			{
				if (iter() == objects.front().items().end())
				{
					objects.erase(objects.begin());
					current_iter = {};
					break;
				}
				auto value = iter().value();
				if (value.is_array() || value.is_object())
				{
					const auto distance = std::distance(objects.front().items().begin(), iter());
					objects.push_back(value);
					current_iter = objects.front().items().begin();
					std::advance(*current_iter, distance + 1);
					continue;
				}
				if (iter().key() != search_tag)
				{
					++*current_iter;
					continue;
				}
				auto str = iter().value().get<T>();
				++*current_iter;
				return str;
			}
		}
	}

private:
	nlohmann::detail::iteration_proxy_value<json::iterator>& iter()
	{
		return *current_iter;
	}

	std::vector<json> objects;
	std::optional<nlohmann::detail::iteration_proxy_value<json::iterator>> current_iter;
	std::string search_tag;
};

asset_loader_t::asset_loader_t(std::string name): db{name + ".assets"}, name{std::move(name)}
{}

std::optional<load_failure_t> asset_loader_t::load_assets(const std::string& asset_folder, const std::optional<std::string>& data_folder)
{
	if (!std::filesystem::exists(asset_folder))
		return load_failure_t::ASSET_FOLDER_NOT_FOUND;

	std::optional<std::filesystem::path> model_folder;
	std::optional<std::filesystem::path> texture_folder;
	std::optional<std::filesystem::path> tags_folder;
	std::optional<std::filesystem::path> biomes_folder;
	std::optional<std::filesystem::path> blockstate_folder;

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
		if (data_folder && entry.path().filename().compare("blockstates") == 0)
		{
			blockstate_folder = entry.path();
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
			if (entry.path().filename().compare("tags") == 0)
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

	if (data_folder && !blockstate_folder)
		return load_failure_t::TAGS_BLOCKSTATES_NOT_FOUND;

	if (data_folder)
	{
		data.json_data[namespace_name.string()].tag_folder = tags_folder->string();
		data.json_data[namespace_name.string()].data_namespace_folder = tags_folder->parent_path().string();

		biomes_folder = tags_folder->parent_path() / "worldgen" / "biome";
	}

	data.json_data[namespace_name.string()].asset_namespace_folder = model_folder->parent_path().string();
	data.json_data[namespace_name.string()].model_folder = model_folder->string();
	data.json_data[namespace_name.string()].texture_folder = texture_folder->string();

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
	BLT_INFO("Found {} models in namespace {}", data.json_data[namespace_name.string()].models.size(), namespace_name.string());

	for (const auto& entry : std::filesystem::recursive_directory_iterator(*texture_folder / "block"))
	{
		if (!entry.path().has_extension())
			continue;
		if (!entry.is_regular_file())
			continue;
		if (entry.path().extension().compare(".png") != 0 && entry.path().extension().compare(".jpg") != 0 && entry.path().extension().
																													compare(".jpeg") != 0 && entry.
																																			path().
																																			extension()
																																			.compare(
																																				".bmp")
			!= 0)
			continue;
		std::filesystem::path relative_path;
		{
			auto p_begin = entry.path().begin();
			auto p_end = entry.path().end();
			auto m_begin = texture_folder->begin();
			while (p_begin != p_end && m_begin != texture_folder->end() && *p_begin == *m_begin)
			{
				++p_begin;
				++m_begin;
			}
			for (; p_begin != p_end; ++p_begin)
				relative_path /= p_begin->stem();
		}
		data.json_data[namespace_name.string()].textures[relative_path.string()] = entry.path().string();
	}
	BLT_INFO("Found {} textures in namespace {}", data.json_data[namespace_name.string()].textures.size(), namespace_name.string());

	auto& map = data.json_data[namespace_name.string()];

	if (data_folder)
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator{*tags_folder / "block"})
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
				auto m_begin = tags_folder->begin();
				while (p_begin != p_end && m_begin != tags_folder->end() && *p_begin == *m_begin)
				{
					++p_begin;
					++m_begin;
				}
				for (; p_begin != p_end; ++p_begin)
					relative_path /= p_begin->stem();
			}
			std::ifstream json_file{entry.path()};
			json jdata = json::parse(json_file);
			if (!jdata.contains("values"))
				return load_failure_t{load_failure_t::INCORRECT_TAG_FILE, "Failed at file: " + entry.path().string()};

			auto& tag_value_list = data.json_data[namespace_name.string()].tags[relative_path.string()].list;

			for (const auto& v : jdata["values"])
				tag_value_list.insert(v.get<std::string>());
		}

		// blockstate folder consists of only json files
		for (const auto& entry : std::filesystem::recursive_directory_iterator{*blockstate_folder})
		{
			if (!entry.is_regular_file())
				continue;
			if (entry.path().has_extension() && entry.path().extension().compare(".json") != 0)
				continue;
			auto block_name = entry.path().stem().string();

			std::ifstream json_file(entry.path());
			json jdata = json::parse(json_file);

			search_for_t search{jdata, "model"};
			while (auto next = search.next())
			{
				const auto& model = *next;
				const auto parts = blt::string::split(model, ':');
				auto namespace_str = namespace_name.string();
				auto model_str = parts[0];
				if (parts.size() > 1)
				{
					namespace_str = parts[0];
					model_str = parts[1];
				}
				data.json_data[namespace_name.string()].block_states[block_name].models[namespace_str].insert(model_str);
			}
		}

		struct color
		{
			static blt::vec3 cvtColor(const int decimal)
			{
				std::stringstream ss;
				ss << std::hex << decimal;
				auto hex = ss.str();
				while (hex.size() < 6)
					hex = std::string("0").append(hex);
				blt::vec3 color;
				color[0] = static_cast<float>(std::stoi(hex.substr(0, 2), nullptr, 16)) / 255;
				color[1] = static_cast<float>(std::stoi(hex.substr(2, 2), nullptr, 16)) / 255;
				color[2] = static_cast<float>(std::stoi(hex.substr(4, 2), nullptr, 16)) / 255;
				return color;
			}
		};

		for (const auto& entry : std::filesystem::recursive_directory_iterator{*biomes_folder})
		{
			if (!entry.is_regular_file())
				continue;
			if (entry.path().has_extension() && entry.path().extension().compare(".json") != 0)
				continue;
			auto biome_name = entry.path().stem().string();

			std::ifstream json_file(entry.path());
			json jdata = json::parse(json_file);

			blt::vec3 grass_color{0.48627450980392156, 0.7411764705882353, 0.4196078431372549};
			search_for_t search_grass{jdata, "grass_color"};
			if (auto value = search_grass.next<int>())
			{
				grass_color = color::cvtColor(*value);
			} else
			{
				search_for_t search_grass2{jdata, "grass_color_modifier"};
				if (auto value = search_grass2.next()) // NOLINT
				{
					if (*value == "dark_forest")
						grass_color = {0.3137254901960784, 0.47843137254901963, 0.19607843137254902};
					else if (*value == "swamp")
						grass_color = {0.41568627450980394, 0.4392156862745098, 0.2235294117647059};
					else
						BLT_WARN("Unknown grass type {}", *value);
				}
			}

			blt::vec3 foliage_color{0.2823529411764706, 0.7098039215686275, 0.09411764705882353};
			search_for_t search_leaves{jdata, "foliage_color"};

			if (auto value = search_leaves.next<int>())
				foliage_color = color::cvtColor(*value);

			data.json_data[namespace_name.string()].biome_colors[biome_name] = {grass_color, foliage_color};
		}
	}

	std::vector<namespaced_object> textures_to_load;

	static blt::hashset_t<std::string> solid_parents{
		"minecraft:block/cube_column",
		"minecraft:block/cube_column_uv_locked_x",
		"minecraft:block/cube_column_uv_locked_y",
		"minecraft:block/cube_column_uv_locked_z",
		"minecraft:block/cube",
		"minecraft:block/leaves"
	};

	static blt::hashset_t<std::string> solid_blocks{"minecraft:block/honey_block"};

	for (auto& [name, model] : map.models)
	{
		auto parents = data.resolve_parents(model.parent.value_or(empty_object));
		bool solid = false;
		for (const auto& [namespace_str, key_str] : parents)
		{
			if (solid_parents.contains(namespace_str + ':' + key_str))
			{
				solid = true;
				break;
			}
		}
		if (solid || solid_blocks.contains(namespace_name.string() + ':' + name))
		{
			if (model.textures)
			{
				for (auto& texture : *model.textures)
					data.solid_textures_to_load[texture.namespace_str].insert(texture.key_str);
			}
		} else
		{
			if (model.textures)
			{
				for (auto& texture : *model.textures)
					data.non_solid_textures_to_load[texture.namespace_str].insert(texture.key_str);
			}
		}
	}
	for (auto& [namespace_str, set] : data.non_solid_textures_to_load)
	{
		auto set_iter = data.solid_textures_to_load.find(namespace_str);
		if (set_iter == data.solid_textures_to_load.end())
			continue;
		const auto& [_, solid_textures] = *set_iter;
		erase_if(set, [this, &solid_textures](const auto& str) {
			return solid_textures.contains(str);
		});
	}

	return {};
}

database_t& asset_loader_t::load_textures()
{
	BLT_INFO("[Phase 2] Loading Textures");
	auto texture_table = db.builder().create_table("solid_textures");
	texture_table.with_column<std::string>("namespace").primary_key();
	texture_table.with_column<std::string>("name").primary_key();
	texture_table.with_column<blt::i32>("width").not_null();
	texture_table.with_column<blt::i32>("height").not_null();
	texture_table.with_column<const std::byte*>("data").not_null();
	texture_table.build().execute();

	auto non_texture_table = db.builder().create_table("non_solid_textures");
	non_texture_table.with_column<std::string>("namespace").primary_key();
	non_texture_table.with_column<std::string>("name").primary_key();
	non_texture_table.with_column<blt::i32>("width").not_null();
	non_texture_table.with_column<blt::i32>("height").not_null();
	non_texture_table.with_column<const std::byte*>("data").not_null();
	non_texture_table.build().execute();

	const static auto insert_solid_sql = "INSERT INTO solid_textures VALUES (?, ?, ?, ?, ?)";
	const static auto insert_non_solid_sql = "INSERT INTO non_solid_textures VALUES (?, ?, ?, ?, ?)";
	const auto insert_solid_stmt = db.prepare(insert_solid_sql);
	const auto insert_non_solid_stmt = db.prepare(insert_non_solid_sql);

	for (const auto& [namespace_str, textures] : data.solid_textures_to_load)
	{
		for (const auto& texture : textures)
			process_texture(insert_solid_stmt, namespace_str, texture);
		BLT_INFO("[Phase 2] Loaded {} solid textures for namespace {}", textures.size(), namespace_str);
	}

	for (const auto& [namespace_str, textures] : data.non_solid_textures_to_load)
	{
		for (const auto& texture : textures)
			process_texture(insert_non_solid_stmt, namespace_str, texture);
		BLT_INFO("[Phase 2] Loaded {} non-solid textures for namespace {}", textures.size(), namespace_str);
	}

	auto model_table = db.builder().create_table("models");
	model_table.with_column<std::string>("namespace").primary_key();
	model_table.with_column<std::string>("model").primary_key();
	model_table.with_column<std::string>("texture_namespace").primary_key();
	model_table.with_column<std::string>("texture").primary_key();
	model_table.build().execute();

	auto tag_table = db.builder().create_table("tags");
	tag_table.with_column<std::string>("namespace").primary_key();
	tag_table.with_column<std::string>("tag").primary_key();
	tag_table.with_column<std::string>("block").primary_key();
	tag_table.build().execute();

	auto tag_models = db.builder().create_table("block_names");
	tag_models.with_column<std::string>("namespace").primary_key();
	tag_models.with_column<std::string>("block_name").primary_key();
	tag_models.with_column<std::string>("model_namespace").primary_key();
	tag_models.with_column<std::string>("model").primary_key();
	tag_models.build().execute();

	const static auto insert_tag_sql = "INSERT INTO tags VALUES (?, ?, ?)";
	const static auto insert_block_name_sql = "INSERT INTO block_names VALUES (?, ?, ?, ?)";
	const static auto insert_models_sql = "INSERT INTO models VALUES (?, ?, ?, ?)";
	const auto insert_tag_stmt = db.prepare(insert_tag_sql);
	const auto insert_block_name_stmt = db.prepare(insert_block_name_sql);
	const auto insert_models_stmt = db.prepare(insert_models_sql);

	BLT_DEBUG("[Phase 2] Begin tag storage");
	size_t tag_list_count = 0;
	size_t tag_model_count = 0;
	for (const auto& [namespace_str, jdata] : data.json_data)
	{
		for (const auto& [tag_name, tag_data] : jdata.tags)
		{
			if (tag_data.list.empty())
				continue;
			for (const auto& block_tag : tag_data.list)
			{
				++tag_list_count;
				insert_tag_stmt.bind().bind_all(namespace_str, tag_name, block_tag);
				if (!insert_tag_stmt.execute())
					BLT_WARN("[Tag List] Unable to insert {} into {}:{} reason '{}'", block_tag, namespace_str, tag_name, db.get_error());
			}
			BLT_DEBUG("[Phase 2] Loaded {} blocks to tag {}:{}", tag_data.list.size(), namespace_str, tag_name);
		}
		for (const auto& [block_name, bstate] : jdata.block_states)
		{
			for (const auto& [model_namespace, model_list] : bstate.models)
			{
				for (const auto& model : model_list)
				{
					++tag_model_count;
					insert_block_name_stmt.bind().bind_all(namespace_str, block_name, model_namespace, model);
					if (!insert_block_name_stmt.execute())
						BLT_WARN("[Block Names] Unable to insert {}:{} into {}:{} reason '{}'", model_namespace, model, namespace_str, block_name,
							db.get_error());
				}
			}
		}
	}
	BLT_INFO("[Phase 2] Loaded {} blocks to tags.", tag_list_count);
	BLT_INFO("[Phase 2] Loaded {} models to tags.", tag_model_count);
	BLT_INFO("[Phase 2] Saving models texture data.");
	for (const auto& [namespace_str, jdata] : data.json_data)
	{
		for (const auto& [model_name, model] : jdata.models)
		{
			if (model.textures)
			{
				blt::hashset_t<std::string> declassed_textures;
				for (const auto& texture : *model.textures)
					declassed_textures.insert(texture.namespace_str + ':' + texture.key_str);

				for (const auto& texture_str : declassed_textures)
				{
					auto parts = blt::string::split(texture_str, ':');
					if (parts.size() == 1)
						insert_models_stmt.bind().bind_all(namespace_str, model_name, namespace_str, parts[0]);
					else
						insert_models_stmt.bind().bind_all(namespace_str, model_name, parts[0], parts[1]);
					if (!insert_models_stmt.execute())
						BLT_WARN("[Model Data] Unable to insert {}:{} into textures. Reason '{}'", namespace_str, model_name, db.get_error());
				}
			}
		}
	}
	BLT_INFO("[Phase 2] Saving biome data");

	auto biome_color_table = db.builder().create_table("biome_color");
	biome_color_table.with_column<std::string>("namespace").primary_key();
	biome_color_table.with_column<std::string>("biome").primary_key();
	biome_color_table.with_column<float>("grass_r");
	biome_color_table.with_column<float>("grass_g");
	biome_color_table.with_column<float>("grass_b");
	biome_color_table.with_column<float>("leaves_r");
	biome_color_table.with_column<float>("leaves_g");
	biome_color_table.with_column<float>("leaves_b");
	biome_color_table.build().execute();

	auto insert_all = db.prepare("INSERT INTO biome_color VALUES (?, ?, ?, ?, ?, ?, ?, ?)");

	for (const auto& [namespace_str, data] : data.json_data)
	{
		for (const auto& [biome, colors] : data.biome_colors)
		{
			insert_all.bind().bind_all(namespace_str, biome, colors.grass_color[0], colors.grass_color[1], colors.grass_color[2],
										colors.leaves_color[0], colors.leaves_color[1], colors.leaves_color[2]);
			if (insert_all.execute().has_error())
				BLT_WARN("Unable to insert into {}:{} reason '{}'", namespace_str, biome, db.get_error());
		}
	}

	BLT_INFO("Finished loading assets");

	return db;
}

void asset_loader_t::process_texture(const statement_t& stmt, const std::string& namespace_str, const std::string& texture)
{
	const auto texture_path = data.json_data[namespace_str].textures[texture];
	if (!std::filesystem::exists(texture_path))
		return;
	int width, height, channels;
	const auto ptr = stbi_loadf(texture_path.c_str(), &width, &height, &channels, 4);
	if (ptr == nullptr)
		return;
	stmt.bind().bind_all(namespace_str, texture, width, height, blt::span{
							reinterpret_cast<char*>(ptr),
							static_cast<blt::size_t>(width * height * 4) * sizeof(float)
						});
	if (!stmt.execute())
	{
		BLT_WARN("Failed to insert texture '{}:{}' into database. Error: '{}'", namespace_str, texture, db.get_error());
	}
	stbi_image_free(ptr);
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

std::string block_pretty_name(std::string block_name)
{
	if (block_name.empty())
		return block_name;
	size_t pos;
	if ((pos = block_name.find(':')) != std::string::npos)
		block_name = block_name.substr(pos + 1);
	if ((pos = block_name.find('/')) != std::string::npos)
		block_name = block_name.substr(pos + 1);
	while ((pos = block_name.find('_')) != std::string::npos)
	{
		block_name[pos] = ' ';
		if (pos + 1 < block_name.size())
			block_name[pos + 1] = static_cast<char>(std::toupper(block_name[pos + 1]));
	}
	block_name[0] = static_cast<char>(std::toupper(block_name[0]));
	return block_name;
}
