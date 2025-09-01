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
#include <tabs/base.h>
#include <imgui.h>
#include <blt/iterator/enumerate.h>

static void HelpMarker(const std::string& desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::BeginItemTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

bool tab_base_t::draw_color_picker(const std::span<color_mode_t> input_modes, blt::color_t& color_data)
{
	ImGui::BeginGroup();
	auto v = color_data.as_linear_rgb().unpack();
	const bool s = ImGui::ColorPicker3("##SelectColors",
									   v.data(),
									   ImGuiColorEditFlags_NoInputs |
									   ImGuiColorEditFlags_PickerHueBar);
	blt::color_t color{blt::color::linear_rgb_t{blt::vec3{v}}};
	for (const auto& [i, mode] : blt::enumerate(input_modes))
	{
		switch (mode.get_mode())
		{
			case color_mode_t::sRGB:
			{
				v = color.as_srgb().unpack();

				ImGui::Text("R");
				ImGui::SameLine();
				ImGui::DragFloat(("##R" + std::to_string(i)).c_str(),
								 &v[0],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("G");
				ImGui::SameLine();
				ImGui::DragFloat(("##G" + std::to_string(i)).c_str(),
								 &v[1],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("B");
				ImGui::SameLine();
				ImGui::DragFloat(("##B" + std::to_string(i)).c_str(),
								 &v[2],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);

				color = blt::color_t{blt::color::srgb_t{blt::vec3{v}}};
				break;
			}
			case color_mode_t::RGB:
			{
				ImGui::Text("R");
				ImGui::SameLine();
				ImGui::DragFloat(("##R" + std::to_string(i)).c_str(),
								 &v[0],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("G");
				ImGui::SameLine();
				ImGui::DragFloat(("##G" + std::to_string(i)).c_str(),
								 &v[1],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("B");
				ImGui::SameLine();
				ImGui::DragFloat(("##B" + std::to_string(i)).c_str(),
								 &v[2],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);

				color = blt::color_t{blt::color::linear_rgb_t{blt::vec3{v}}};
				break;
			}
			case color_mode_t::OkLab:
			{
				v = color.as_oklab().unpack();

				ImGui::Text("L");
				ImGui::SameLine();
				HelpMarker("Perceptual Lightness, zero is black, one is white.");
				ImGui::SameLine();
				ImGui::DragFloat(("##L" + std::to_string(i)).c_str(),
								 &v[0],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("a");
				ImGui::SameLine();
				HelpMarker("Negative is for green, positive is for red.");
				ImGui::SameLine();
				ImGui::DragFloat(("##a" + std::to_string(i)).c_str(),
								 &v[1],
								 0.01f,
								 -.5f,
								 .5f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("b");
				ImGui::SameLine();
				HelpMarker("Negative is blue, positive is yellow.");
				ImGui::SameLine();
				ImGui::DragFloat(("##b" + std::to_string(i)).c_str(),
								 &v[2],
								 0.01f,
								 -.5f,
								 .5f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);

				color = blt::color_t{blt::color::oklab_t{blt::vec3{v}}};
				break;
			}
			case color_mode_t::OkLch:
			{
				v = color.as_oklch().unpack();

				ImGui::Text("L");
				ImGui::SameLine();
				HelpMarker("Perceptual Lightness, zero is black, one is white.");
				ImGui::SameLine();
				ImGui::DragFloat(("##L" + std::to_string(i)).c_str(),
								 &v[0],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("C");
				ImGui::SameLine();
				HelpMarker("Chroma (representing chromatic intensity)");
				ImGui::SameLine();
				ImGui::DragFloat(("##C" + std::to_string(i)).c_str(),
								 &v[1],
								 0.01f,
								 0.f,
								 0.5f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("h");
				ImGui::SameLine();
				HelpMarker("Hue Angle");
				ImGui::SameLine();
				ImGui::DragFloat(("##h" + std::to_string(i)).c_str(),
								 &v[2],
								 0.01f,
								 0,
								 360,
								 "%.4f",
								 ImGuiSliderFlags_WrapAround);

				color = blt::color_t{blt::color::oklch_t{blt::vec3{v}}};
				break;
			}
			case color_mode_t::HSV:
				v = color.as_hsv().unpack();

				ImGui::Text("H");
				ImGui::SameLine();
				ImGui::DragFloat(("##H" + std::to_string(i)).c_str(),
								 &v[0],
								 0.01f,
								 0.0f,
								 360.f,
								 "%.4f",
								 ImGuiSliderFlags_WrapAround);
				ImGui::SameLine();
				ImGui::Text("S");
				ImGui::SameLine();
				ImGui::DragFloat(("##S" + std::to_string(i)).c_str(),
								 &v[1],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);
				ImGui::SameLine();
				ImGui::Text("V");
				ImGui::SameLine();
				ImGui::DragFloat(("##V" + std::to_string(i)).c_str(),
								 &v[2],
								 0.01f,
								 0.0f,
								 1.f,
								 "%.4f",
								 ImGuiSliderFlags_AlwaysClamp);

				color = blt::color_t{blt::color::hsv_t{blt::vec3{v}}};
				break;
		}
	}
	ImGui::EndGroup();
	color_data = color;
	return s;
}
