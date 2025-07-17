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

std::optional<std::string> show_block_picker(const blt::vec2& pos, const std::vector<std::pair<std::string, const gpu_image_t*>>& block_textures, int icons_per_row = 8, const blt::vec2& icon_size = {32, 32}, float window_size = 32 * 12 + 48);

#endif //BLOCK_PICKER_H
