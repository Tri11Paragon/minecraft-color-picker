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
#include <sql.h>
#include <blt/logging/logging.h>

statement_t::statement_t(sqlite3* db, const std::string& stmt): db{db}
{
	if (sqlite3_prepare_v2(db, stmt.c_str(), stmt.size(), &statement, nullptr) != SQLITE_OK)
		BLT_ERROR("Failed to create statement object '{}' cause '{}'", stmt, sqlite3_errmsg(db));
}

statement_result_t statement_t::execute() const
{
	const auto v = sqlite3_step(statement);
	return statement_result_t{v};
}

statement_t::~statement_t()
{
	sqlite3_finalize(statement);
}

statement_t table_builder_t::build()
{
	std::string sql = "CREATE TABLE IF NOT EXISTS ";
	sql += name;
	sql += " (";
	for (const auto& [i, column] : blt::enumerate(columns))
	{
		sql += column;
		if (i != columns.size() - 1)
			sql += ", ";
	}
	if (!primary_keys.empty())
	{
		sql += ", PRIMARY KEY (";
		for (const auto& [i, key] : blt::enumerate(primary_keys))
		{
			sql += key;
			if (i != primary_keys.size() - 1)
				sql += ", ";
		}
		sql += ")";
	}
	if (!foreign_keys.empty())
	{
		for (const auto& [i, key] : blt::enumerate(foreign_keys))
		{
			sql += ", FOREIGN KEY (";
			sql += key.local_name + ") REFERENCES " + key.foreign_table + "(" + key.foreign_name + ")";
		}
	}
	sql += ");";
	return statement_t{db, sql};
}

database_t::database_t(const std::string& file)
{
	if (sqlite3_open(file.c_str(), &db) != SQLITE_OK)
		BLT_ERROR("Failed to open database '{}' got error message '{}'.", file, sqlite3_errmsg(db));
	else
		BLT_DEBUG("Opened database '{}' successfully.", file);
}

database_t::~database_t()
{
	sqlite3_close(db);
}
