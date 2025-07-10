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

#ifndef SQL_H
#define SQL_H

#include <cstring>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <blt/iterator/enumerate.h>

namespace detail
{
	template <typename T>
	std::string sql_name()
	{
		using Decay = std::decay_t<T>;
		if constexpr (std::is_same_v<Decay, nullptr_t>)
			return "NULL";
		else if constexpr (std::is_integral_v<Decay>)
			return "INTEGER";
		else if constexpr (std::is_floating_point_v<Decay>)
			return "REAL";
		else if constexpr (std::is_same_v<Decay, std::string> || std::is_same_v<Decay, std::string_view> || std::is_same_v<Decay, char*>)
			return "TEXT";
		else if constexpr (std::is_same_v<Decay, bool>)
			return "BOOLEAN";
		else
			return "BLOB";
	}

	struct foreign_key_t
	{
		std::string local_name;
		std::string foreign_table;
		std::string foreign_name;
	};
}

class column_t
{
public:
	explicit column_t(sqlite3_stmt* statement): statement{statement}
	{}

	template <typename T>
	auto get(const int col) const -> decltype(auto)
	{
		using Decay = std::decay_t<T>;
		if constexpr (std::is_integral_v<Decay>)
		{
			if constexpr (sizeof(Decay) == 8)
			{
				return sqlite3_column_int64(statement, col);
			} else
			{
				return sqlite3_column_int(statement, col);
			}
		} else if constexpr (std::is_same_v<Decay, double>)
		{
			return sqlite3_column_double(statement, col);
		} else if constexpr (std::is_same_v<Decay, float>)
		{
			return static_cast<float>(sqlite3_column_double(statement, col));
		} else if constexpr (std::is_same_v<Decay, std::string> || std::is_same_v<Decay, std::string_view> || std::is_same_v<Decay, char*>)
		{
			return T(sqlite3_column_text(statement, col));
		} else
		{
			return static_cast<T>(sqlite3_column_blob(statement, col));
		}
	}

	template <typename... Types>
	[[nodiscard]] std::tuple<Types...> get() const
	{
		return get_internal<Types...>(std::index_sequence_for<Types...>());
	}

	[[nodiscard]] size_t size(const int col) const
	{
		return static_cast<size_t>(sqlite3_column_bytes(statement, col));
	}

private:
	template <typename... Types, size_t... Indices>
	[[nodiscard]] std::tuple<Types...> get_internal(std::index_sequence<Indices...>) const
	{
		return std::tuple<Types...>{get<Types>(Indices)...};
	}

	sqlite3_stmt* statement;
};

class column_binder_t
{
public:
	explicit column_binder_t(sqlite3_stmt* statement): statement{statement}
	{}

	template <typename T>
	int bind(const T& type, int col)
	{
		static_assert(!std::is_rvalue_reference_v<T>, "Lifetime of object must outlive its usage!");
		using Decay = std::decay_t<T>;
		if constexpr (std::is_same_v<Decay, bool> || std::is_integral_v<Decay>)
		{
			if constexpr (sizeof(Decay) == 8)
				return sqlite3_bind_int64(statement, col, type);
			else
				return sqlite3_bind_int(statement, col, type);
		} else if constexpr (std::is_floating_point_v<Decay>)
		{
			return sqlite3_bind_double(statement, col, type);
		} else if constexpr (std::is_same_v<Decay, nullptr_t>)
		{
			return sqlite3_bind_null(statement, col);
		} else if constexpr (std::is_same_v<Decay, std::string> || std::is_same_v<Decay, std::string_view>)
		{
			auto str_copy = new char[type.size()];
			std::memcpy(str_copy, type.data(), type.size());
			return sqlite3_bind_text(statement, col, str_copy, type.size(), [](void* ptr) {
				delete[] static_cast<char*>(ptr);
			});
		} else if constexpr (std::is_same_v<Decay, char*> || std::is_same_v<Decay, const char*>)
		{
			return sqlite3_bind_text(statement, col, type, -1, nullptr);
		} else
		{
			return sqlite3_bind_blob64(statement, col, type.data(), type.data(), nullptr);
		}
	}

private:
	sqlite3_stmt* statement;
};

class binder_t
{
public:
	explicit binder_t(sqlite3_stmt* statement): statement(statement)
	{}

	/**
	 * Indexes start at 1 for the left most template parameter
	 */
	template <typename T>
	int bind(const T& type, int col)
	{
		return column_binder_t{statement}.bind(type, col);
	}

	template <typename T>
	int bind(const T& type, const std::string& name)
	{
		auto index = sqlite3_bind_parameter_index(statement, name.c_str());
		return column_binder_t{statement}.bind(type, index);
	}

	template <typename... Types>
	int bind_all(const Types&... types)
	{
		return bind_internal<Types...>(std::index_sequence_for<Types...>(), types...);
	}

private:
	template <typename... Types, size_t... Indices>
	int bind_internal(std::index_sequence<Indices...>, const Types&... types)
	{
		return ((bind<Types>(types, Indices + 1) != SQLITE_OK) | ...);
	}

	sqlite3_stmt* statement;
};

class statement_t
{
public:
	explicit statement_t(sqlite3* db, const std::string& stmt);

	statement_t(const statement_t& copy) = delete;

	statement_t(statement_t& move) noexcept: statement{std::exchange(move.statement, nullptr)}, db{move.db}
	{}

	statement_t& operator=(const statement_t&) = delete;

	statement_t& operator=(statement_t&& move) noexcept
	{
		statement = std::exchange(move.statement, statement);
		db = std::exchange(move.db, db);
		return *this;
	}

	binder_t bind() const // NOLINT
	{
		sqlite3_reset(statement);
		return binder_t{statement};
	}

	// returns true if the statement has a row. false otherwise. optional is empty if there is an error
	[[jetbrains::has_side_effects]] std::optional<bool> execute() const; // NOLINT

	[[nodiscard]] column_t fetch() const
	{
		return column_t{statement};
	}

	~statement_t();

private:
	sqlite3_stmt* statement = nullptr;
	sqlite3* db;
};

class table_builder_t;

class table_column_builder_t
{
public:
	table_column_builder_t(table_builder_t& parent, std::vector<std::string>& columns, std::vector<std::string>& primary_keys,
							std::vector<detail::foreign_key_t>& foreign_keys, std::string type, std::string name): parent{parent}, columns{columns},
																													primary_keys{primary_keys},
																													foreign_keys{foreign_keys},
																													type{std::move(type)},
																													name{std::move(name)}
	{}

	table_column_builder_t& primary_key()
	{
		primary_keys.push_back(name);
		return *this;
	}

	table_column_builder_t& unique()
	{
		attributes.emplace_back("UNIQUE");
		return *this;
	}

	table_column_builder_t& with_default(const std::string& value)
	{
		if (type == "TEXT")
			attributes.push_back("DEFAULT \"" + value + '"');
		else
			attributes.push_back("DEFAULT " + value);
		return *this;
	}

	table_column_builder_t& not_null()
	{
		attributes.emplace_back("NOT NULL");
		return *this;
	}

	table_column_builder_t& foreign_key(const std::string& table, const std::string& column)
	{
		foreign_keys.emplace_back(detail::foreign_key_t{name, table, column});
		return *this;
	}

	table_builder_t& finish()
	{
		if (!created)
		{
			columns.push_back(name + ' ' + type + ' ' + join());
			created = true;
		}
		return parent;
	}

	~table_column_builder_t()
	{
		finish();
	}

private:
	[[nodiscard]] std::string join() const
	{
		std::string out;
		for (const auto& str : attributes)
		{
			out += str;
			out += ' ';
		}
		return out;
	}

	bool created = false;
	table_builder_t& parent;
	std::vector<std::string>& columns;
	std::vector<std::string>& primary_keys;
	std::vector<detail::foreign_key_t>& foreign_keys;
	std::vector<std::string> attributes;
	std::string type;
	std::string name;
};

class table_builder_t
{
public:
	template <typename>
	struct required_string_t
	{
		std::string name;

		required_string_t(std::string name): name{std::move(name)} // NOLINT
		{}

		operator std::string() // NOLINT
		{
			return name;
		}
	};

	explicit table_builder_t(sqlite3* db, std::string name): db{db}, name{std::move(name)}
	{}

	template <typename T>
	table_column_builder_t with_column(const std::string& name)
	{
		return table_column_builder_t{*this, columns, primary_keys, foreign_keys, detail::sql_name<T>(), name};
	}

	template <typename... Types>
	table_builder_t& with_columns(required_string_t<Types>... names)
	{
		(with_column<Types>(names.name), ...);
		return *this;
	}

	table_builder_t& with_foreign_key(const std::string& local_name, const std::string& foreign_table, const std::string& foreign_name)
	{
		foreign_keys.emplace_back(detail::foreign_key_t{local_name, foreign_table, foreign_name});
		return *this;
	}

	table_builder_t& with_primary_key(const std::string& name)
	{
		primary_keys.push_back(name);
		return *this;
	}

	statement_t build();

private:
	std::vector<std::string> columns{};
	std::vector<std::string> primary_keys{};
	std::vector<detail::foreign_key_t> foreign_keys{};
	sqlite3* db;
	std::string name{};
};

class statement_builder_t
{
public:
	explicit statement_builder_t(sqlite3* db): db{db}
	{}

	table_builder_t create_table(const std::string& name) const
	{
		return table_builder_t{db, name};
	}

private:
	sqlite3* db;
};

class database_t
{
public:
	explicit database_t(const std::string& file);

	database_t(const database_t& copy) = delete;

	database_t(database_t&& move) noexcept: db{std::exchange(move.db, nullptr)}
	{}

	database_t& operator=(const database_t&) = delete;

	database_t& operator=(database_t&& move) noexcept
	{
		db = std::exchange(move.db, db);
		return *this;
	}

	[[nodiscard]] statement_t prepare(const std::string& stmt) const
	{
		return statement_t{db, stmt};
	}

	[[nodiscard]] statement_builder_t builder() const
	{
		return statement_builder_t{db};
	}

	[[nodiscard]] auto get_error() const
	{
		return sqlite3_errmsg(db);
	}

	~database_t();

private:
	sqlite3* db = nullptr;
};

#endif //SQL_H
