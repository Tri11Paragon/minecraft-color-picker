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

#ifndef TABS_H
#define TABS_H

#include <blt/std/variant.h>

void init_tabs();

void render_tabs();

template <typename T>
concept TabType = requires(T t)
{
	requires (requires { { t.render() } -> std::same_as<void>; }) ||
			 (requires { { t.render() } -> std::same_as<std::optional<size_t>>; });
	{ t.name() } -> std::same_as<std::string&>;
	{ T::button_name } -> std::same_as<std::string>;
};

template <TabType... Tabs>
struct tab_selector_t
{
	constexpr static float padding_left       = 20;
	constexpr static float padding_right      = 20;
	constexpr static float padding_top        = 20;
	constexpr static float padding_bottom     = 20;
	constexpr static float padding_horizontal = 10;
	constexpr static float padding_vertical   = 10;

	std::optional<size_t> render()
	{
		return handle_buttons(std::index_sequence_for<Tabs...>{});
	}

private:
	static auto constexpr Columns     = std::min(static_cast<blt::size_t>(std::log(sizeof...(Tabs))), 4ul);
	static auto constexpr Per_Columns = sizeof...(Tabs) / Columns;

	template <size_t... Indexes>
	std::optional<size_t> handle_buttons(std::index_sequence<Indexes...>)
	{
		auto avail = ImGui::GetContentRegionAvail();
		avail.x -= padding_left + padding_right;
		avail.y -= padding_top + padding_bottom;
		avail.x -= (Columns - 1) * padding_horizontal;
		avail.y -= (Per_Columns - 1) * padding_vertical;

		const float button_width  = avail.x / Columns;
		const float button_height = std::max(avail.y / Per_Columns, 0.0f);

		return (render_button<Tabs, Indexes>(button_width, button_height) || ...);
	}

	template <TabType T, size_t I>
	[[nodiscard]] std::optional<size_t> render_button(const float width, const float height) const
	{
		if constexpr (I % Columns != 0 || I % Columns != Columns - 1)
			ImGui::SameLine();
		if (ImGui::Button(T::button_name.c_str(), ImVec2(width, height)))
			return I;

		return {};
	}
};


template <TabType... Tabs>
struct tab_manager_t
{
	void render()
	{
		if (open_tabs.empty())
			open_tabs.emplace_back();
		if (ImGui::BeginTabBar(
			"Color Views",
			ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
		{
			if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
				open_tabs.emplace_back();

			for (size_t n = 0; n < open_tabs.size();)
			{
				bool open = true;
				if (ImGui::BeginTabItem(open_tabs[n].tab_name.c_str(), &open, ImGuiTabItemFlags_None))
				{
					open_tabs[n].visit([this, &n](tab_selector_t<Tabs...>& selector) {
										   if (auto idx = selector.render())
										   {
											   set(open_tabs[n], *idx, std::make_index_sequence<sizeof...(Tabs)>{});
										   }
									   },
									   [](auto& tab) {
										   tab.render();
									   });
					ImGui::EndTabItem();
				}

				if (!open)
					open_tabs.erase(open_tabs.begin() + static_cast<blt::ptrdiff_t>(n));
				else
					n++;
			}

			ImGui::EndTabBar();
		}
	}

private:
	using tab_t = blt::variant_t<tab_selector_t<Tabs...>, Tabs...>;

	template <size_t... Indexes>
	void set(tab_t& tab, const size_t index, std::index_sequence<Indexes...>)
	{
		(create<Indexes>(tab, index) || ...);
	}

	template <size_t Index>
	static bool create(tab_t& tab, const size_t index)
	{
		if (index == Index)
		{
			tab.template emplace<Index>();
			return true;
		}
		return false;
	}

	std::vector<tab_t> open_tabs;
};

#endif //TABS_H
