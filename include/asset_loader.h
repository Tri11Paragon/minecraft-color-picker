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

#ifndef ASSET_LOADER_H
#define ASSET_LOADER_H

#include <sql.h>

class asset_loader_t
{
public:
	asset_loader_t(std::string folder, std::string name);

	[[nodiscard]] const std::string& get_name() const
	{
		return name;
	}
private:
	database_t db;
	std::string folder;
	std::string name;
};

#endif //ASSET_LOADER_H
