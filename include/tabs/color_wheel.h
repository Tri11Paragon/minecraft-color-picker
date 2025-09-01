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

#ifndef MINECRAFT_COLOR_PICKER_COLOR_WHEEL_H
#define MINECRAFT_COLOR_PICKER_COLOR_WHEEL_H

#include <string>
#include <string_view>
#include <tabs/base.h>

struct color_wheel_t : tab_base_t
{
	static constexpr std::string_view button_name = "Color Wheel";

	void render();
};

#endif //MINECRAFT_COLOR_PICKER_COLOR_WHEEL_H