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
#include <asset_loader.h>
#include <filesystem>
#include <blt/gfx/window.h>
#include "blt/gfx/renderer/resource_manager.h"
#include "blt/gfx/renderer/batch_2d_renderer.h"
#include "blt/gfx/renderer/camera.h"
#include <imgui.h>
#include <sql.h>
#include <data_loader.h>
#include <blt/math/log_util.h>

blt::gfx::matrix_state_manager global_matrices;
blt::gfx::resource_manager resources;
blt::gfx::batch_renderer_2d renderer_2d(resources, global_matrices);
blt::gfx::first_person_camera camera;

void init(const blt::gfx::window_data&)
{
    using namespace blt::gfx;


    global_matrices.create_internals();
    resources.load_resources();
    renderer_2d.create();
}

void update(const blt::gfx::window_data& data)
{
    global_matrices.update_perspectives(data.width, data.height, 90, 0.1, 2000);

    camera.update();
    camera.update_view(global_matrices);
    global_matrices.update();

    renderer_2d.render(data.width, data.height);
}

void destroy(const blt::gfx::window_data&)
{
    global_matrices.cleanup();
    resources.cleanup();
    renderer_2d.cleanup();
    blt::gfx::cleanup();
}

void use_database(database_t& db)
{
    const data_loader_t data{db};
    auto assets = data.load();

    const auto i1 = assets.images["minecraft"]["block/redstone_block"];
    const auto i2 = assets.images["minecraft"]["block/red_wool"];

    auto s1 = i1.get_default_sampler();
    auto s2 = i2.get_default_sampler();

    comparator_euclidean_t compare{};

    const auto difference = compare.compare(s1, s2);

    BLT_TRACE("{}", difference);
}

int main()
{
    // std::filesystem::remove("1.21.5.assets");
    // blt::gfx::init(blt::gfx::window_data{"Minecraft Color Picker", init, update, destroy}.setSyncInterval(1));

    if (!std::filesystem::exists("1.21.5.assets"))
    {
        asset_loader_t loader{"1.21.5"};

        if (const auto result = loader.load_assets("../res/assets", "../res/data"))
        {
            BLT_ERROR("Failed to load assets. Reason: {}", result->to_string());
            return 1;
        }

        auto& db = loader.load_textures();
        use_database(db);
    } else {
        auto db = load_database("1.21.5.assets");
        use_database(db);
    }



    return 0;
}