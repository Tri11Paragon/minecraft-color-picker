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

#ifndef MINECRAFT_COLOR_PICKER_BASE_H
#define MINECRAFT_COLOR_PICKER_BASE_H

#include <span>
#include <string>
#include <blt/math/colors.h>

struct color_mode_t
{
	enum color_t : blt::u32
	{
		sRGB,
		RGB,
		OkLab,
		OkLch,
		HSV,
	};

	color_mode_t(const color_t mode) : mode{mode} // NOLINT
	{}

	[[nodiscard]] color_t get_mode() const
	{
		return mode;
	}

	operator color_t() const // NOLINT
	{
		return mode;
	}

private:
	color_t mode;
};

struct tab_base_t
{
	std::string& name()
	{
		return _name;
	}

	static bool draw_color_picker(std::span<color_mode_t> input_modes, blt::color_t& color_data);
protected:
	std::string _name;
};

#endif //MINECRAFT_COLOR_PICKER_BASE_H