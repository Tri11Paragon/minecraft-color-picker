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
#include <block_picker.h>
#include <render.h>

gpu_asset_manager::gpu_asset_manager(assets_t assets)
{
	for (auto& [namespace_str, data] : assets.assets)
	{
		for (auto& [image_name, image] : data.images)
		{
			auto texture = std::make_unique<blt::gfx::texture_gl2D>(image.width, image.height);
			texture->bind();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			for (auto& f : image.data)
				f = std::pow(f, 1.0f / 2.2f);
			texture->upload(image.data.data(), image.width, image.height, GL_RGBA, GL_FLOAT);

			resources[namespace_str][image_name] = gpu_image_t{std::move(image), std::move(texture)};
		}
	}
}

std::vector<block_picker_data_t> gpu_asset_manager::get_icon_render_list()
{
	std::vector<block_picker_data_t> ret;
	for (const auto& [namespace_str, map] : resources)
	{
		for (const auto& [texture_str, gpu_image] : map)
			ret.emplace_back(texture_str, &gpu_image);
	}
	return ret;
}
