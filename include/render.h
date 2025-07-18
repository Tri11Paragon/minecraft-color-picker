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

#ifndef RENDER_H
#define RENDER_H

#include <data_loader.h>
#include <blt/gfx/texture.h>

struct block_picker_data_t;

struct gpu_image_t
{
	gpu_image_t() = default;

	gpu_image_t(image_t image, std::unique_ptr<blt::gfx::texture_gl2D> texture): image(std::move(image)), texture(std::move(texture))
	{

	}

	image_t image;
	std::unique_ptr<blt::gfx::texture_gl2D> texture;
};

class gpu_asset_manager
{
public:
	explicit gpu_asset_manager(assets_t assets);


	blt::hashmap_t<std::string, blt::hashmap_t<std::string, gpu_image_t>> resources;

	std::vector<block_picker_data_t> get_icon_render_list();
};

#endif //RENDER_H
