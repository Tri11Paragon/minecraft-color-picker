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
#include <blt/math/log_util.h>
#include <data_loader.h>
#include <blt/logging/logging.h>

sample_sequence_t make_sequence(std::initializer_list<blt::vec3> list)
{
    sequence_t sequence{*list.begin()};
    for (const auto& [i, value] : blt::enumerate(list).skip(1))
        sequence = sequence_t{value, std::move(sequence)};
    return sequence;
}

database_t load_database(const std::filesystem::path& path)
{
    database_t db{path.string()};
    return db;
}

assets_t data_loader_t::load()
{
    constexpr auto SQL = "SELECT * FROM solid_textures";
    auto stmt = db.prepare(SQL);
    
    assets_t assets{db};
    
    while (stmt.execute().has_row()) {
        auto column = stmt.fetch();
        
        const auto [namespace_str, name, width, height, ptr] = column.get<std::string, std::string, blt::i32, blt::i32, const std::byte*>();
        
        const auto size_floats = width * height * 4;
        
        auto& namespace_hash = assets.assets[namespace_str].images;
        auto& image = namespace_hash[name];
        image.width = width;
        image.height = height;
        image.data.resize(size_floats);
        std::memcpy(image.data.data(), ptr, size_floats * sizeof(float));
    }
    
    stmt = db.prepare("SELECT * FROM non_solid_textures");
    stmt.bind();
    while (stmt.execute().has_row()) {
        auto column = stmt.fetch();
        
        const auto [namespace_str, name, width, height, ptr] = column.get<std::string, std::string, blt::i32, blt::i32, const std::byte*>();
        
        const auto size_floats = width * height * 4;
        
        auto& namespace_hash = assets.assets[namespace_str].non_solid_images;
        auto& image = namespace_hash[name];
        image.width = width;
        image.height = height;
        image.data.resize(size_floats);
        std::memcpy(image.data.data(), ptr, size_floats * sizeof(float));
    }
    
    stmt = db.prepare("SELECT * FROM biome_color");
    stmt.bind();
    while (stmt.execute().has_row()) {
        auto column = stmt.fetch();
        const auto [namespace_str, biome, grass_r, grass_g, grass_b, leaves_r, leaves_g, leaves_b] = column.get<
                std::string, std::string, float, float, float, float, float, float>();
        
        const blt::vec3 grass{grass_r, grass_g, grass_b};
        const blt::vec3 leaves{leaves_r, leaves_g, leaves_b};
        
        assets.assets[namespace_str].biome_colors[biome] = {grass, leaves};
    }
    
    return assets;
}

struct sample_pair {
    sample_pair(sample_sequence_t s1, sample_sequence_t s2): sample1{std::move(s1)}, sample2{std::move(s2)}
    {}
    
    [[nodiscard]] bool has_value() const
    {
        return sample1.has_value() && sample2.has_value();
    }
    
    explicit operator bool() const
    {
        return has_value();
    }
    
    std::pair<blt::vec3, blt::vec3> next()
    {
        auto pair = std::pair{sample1->value(), sample2->value()};
        sample1 = sample1->next();
        sample2 = sample2->next();
        return pair;
    }
    
    std::optional<sample_sequence_t> sample1;
    std::optional<sample_sequence_t> sample2;
};

sampler_one_point_t::sampler_one_point_t(const image_t& image)
{
    float alpha = 0;
    average = {};
    for (blt::i32 y = 0; y < image.height; y++) {
        for (blt::i32 x = 0; x < image.width; x++) {
            const auto a = image.data[(y * image.width + x) * 4 + 3];
            average[0] += image.data[(y * image.width + x) * 4 + 0] * a;
            average[1] += image.data[(y * image.width + x) * 4 + 1] * a;
            average[2] += image.data[(y * image.width + x) * 4 + 2] * a;
            alpha += a;
        }
    }
    if (alpha != 0)
        average = average / alpha;
}

blt::vec3 comparator_euclidean_t::compare(sampler_interface_t& s1, sampler_interface_t& s2)
{
    sample_pair seq{s1.get_sampler(), s2.get_sampler()};
    blt::vec3 total{};
    while (seq) {
        auto [sample1, sample2] = seq.next();
        const auto dif = sample1 - sample2;
        total += dif * dif;
    }
    return {std::sqrt(total[0]), std::sqrt(total[1]), std::sqrt(total[2])};
}

std::vector<std::tuple<std::string, std::string>>& assets_t::get_biomes()
{
    static std::vector<std::tuple<std::string, std::string>> ret;
    if (ret.empty()) {
        for (const auto& [namespace_str, data] : assets) {
            for (const auto& [biome_str, biome_color] : data.biome_colors) {
                ret.emplace_back(namespace_str, biome_str);
            }
        }
        std::sort(ret.begin(), ret.end(), [](const auto& a, const auto& b) {
            auto [ns1, biome1] = a;
            auto [ns2, biome2] = b;
            auto combined1 = ns1 + ':' + biome1;
            auto combined2 = ns2 + ':' + biome2;
            return combined1.compare(combined2) < 0;
        });
    }
    return ret;
}
