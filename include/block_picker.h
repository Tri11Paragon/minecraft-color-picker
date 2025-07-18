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

#ifndef BLOCK_PICKER_H
#define BLOCK_PICKER_H

#include <string>
#include <optional>
#include <blt/math/vectors.h>
#include <render.h>

struct block_picker_data_t
{
	std::string block_name;
	const gpu_image_t* texture;

	block_picker_data_t(std::string block_name, const gpu_image_t* texture): block_name{std::move(block_name)}, texture{texture}
	{}
};

std::optional<size_t> draw_block_list(const std::vector<block_picker_data_t>& block_textures, bool selectable,
									const std::optional<std::string>& filter, int icons_per_row = 8, const blt::vec2& icon_size = {32, 32});

std::optional<std::string> show_block_picker(const blt::vec2& pos, const std::vector<block_picker_data_t>& block_textures, int icons_per_row = 8,
											const blt::vec2& icon_size = {32, 32}, float window_size = 32 * 12 + 48);

#endif //BLOCK_PICKER_H
