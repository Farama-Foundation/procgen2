#include "common_systems.h"

#include "tilemap.h"

#include "helpers.h"

void System_Sprite_Render::update(float dt) {
    if (render_entities.size() != entities.size())
        render_entities.resize(entities.size());

    int index = 0;

    for (auto const &e : entities) {
        auto &sprite = c.get_component<Component_Sprite>(e);

        // If also has animation
        if (c.entity_manager.get_signature(e)[c.component_manager.get_component_type<Component_Animation>()]) {
            // Has animation component
            auto &animation = c.get_component<Component_Animation>(e);

            animation.t += dt;

            while (animation.t >= animation.rate) {
                animation.t -= animation.rate;
                
                animation.frame_index = (animation.frame_index + 1) % animation.frames.size();
            }

            sprite.texture = animation.frames[animation.frame_index];
        }

        render_entities[index] = std::make_pair(sprite.z, e);
        index++;
    }
    
    // Sort sprites
    std::sort(render_entities.begin(), render_entities.end(), [](const std::pair<float, Entity> &left, const std::pair<float, Entity> &right) {
        return left.first < right.first;
    });
}

void System_Sprite_Render::render(const Rectangle &camera_aabb, Sprite_Render_Mode mode) {
    // Render
    for (size_t i = 0; i < render_entities.size(); i++) {
        Entity e = render_entities[i].second;

        auto const &sprite = c.get_component<Component_Sprite>(e);
        auto const &transform = c.get_component<Component_Transform>(e);

        // Sorting relative to tile map system - negative is behind, positive in front
        if (mode == positive_z && sprite.z < 0.0f)
            continue;
        else if (mode == negative_z && sprite.z >= 0.0f)
            break;

        // Relative
        float cos_rot = std::cos(transform.rotation);
        float sin_rot = std::sin(transform.rotation);

        Vector2 offset{ cos_rot * sprite.position.x - sin_rot * sprite.position.y, sin_rot * sprite.position.x + cos_rot * sprite.position.y };

        Vector2 position = (Vector2){ transform.position.x + offset.x, transform.position.y + offset.y };
        float rotation = transform.rotation + sprite.rotation;
        float scale = transform.scale * sprite.scale;

        // Find sprite AABB rectangle
        Rectangle aabb = (Rectangle){ position.x, position.y, sprite.texture.width * pixels_to_unit, sprite.texture.height * pixels_to_unit };
        aabb = rotated_scaled_AABB(aabb, rotation, scale);

        // If visible
        if (CheckCollisionRecs(aabb, camera_aabb))
            DrawTextureEx(sprite.texture, (Vector2){ position.x * unit_to_pixels, position.y * unit_to_pixels }, rotation, scale * unit_to_pixels / sprite.texture.width, sprite.tint);
    }
}

void System_Mob_AI::update(float dt) {
    // Get tile map system
    std::shared_ptr<System_Tilemap> tilemap = c.system_manager.get_system<System_Tilemap>();

    for (auto const &e : entities) {
        auto &mob_ai = c.get_component<Component_Mob_AI>(e);

        auto &transform = c.get_component<Component_Transform>(e);

        // Move
        transform.position.x += mob_ai.velocity_x * dt;

        Rectangle wall_sensor{ transform.position.x + 0.5f, transform.position.y - 1.1f, 1.0f, 0.5f };
        Rectangle floor_sensor_left{ transform.position.x - 0.5f, transform.position.y + 0.1f, 1.0f, 0.8f };
        Rectangle floor_sensor_right{ transform.position.x + 1.5f, transform.position.y + 0.1f, 1.0f, 0.8f };

        Vector2 wall_offset = tilemap->get_collision_offset(wall_sensor, [](Tile_ID id) -> Collision_Type {
            return (id == wall_mid || id == wall_top ? full : none);
        });

        Vector2 floor_offset_left = tilemap->get_collision_offset(floor_sensor_left, [](Tile_ID id) -> Collision_Type {
            return (id == empty ? full : none);
        });

        Vector2 floor_offset_right = tilemap->get_collision_offset(floor_sensor_right, [](Tile_ID id) -> Collision_Type {
            return (id == empty ? full : none);
        });

        float delta_x = wall_offset.x + floor_offset_left.x + floor_offset_right.x;

        transform.position.x += delta_x;

        if ((delta_x > 0.0f) != (mob_ai.velocity_x > 0.0f))
            mob_ai.velocity_x *= -1.0f; // Rebound
    }
}

const std::vector<std::string> agent_themes = { "Beige", "Blue", "Green", "Pink", "Yellow" };

void System_Agent::init() {
    stand_textures.resize(agent_themes.size());
    jump_textures.resize(agent_themes.size());
    walk1_textures.resize(agent_themes.size());
    walk2_textures.resize(agent_themes.size());

    for (int i = 0; i < agent_themes.size(); i++) {
        stand_textures[i].load("assets/kenney/Players/128x256/" + agent_themes[i] + "/alien" + agent_themes[i] + "_stand.png");
        jump_textures[i].load("assets/kenney/Players/128x256/" + agent_themes[i] + "/alien" + agent_themes[i] + "_jump.png");
        walk1_textures[i].load("assets/kenney/Players/128x256/" + agent_themes[i] + "/alien" + agent_themes[i] + "_walk1.png");
        walk2_textures[i].load("assets/kenney/Players/128x256/" + agent_themes[i] + "/alien" + agent_themes[i] + "_walk2.png");
    }
}

void System_Agent::update(float dt, Camera2D &camera) {
    // Parameters
    const float max_jump = 1.5f;
    const float gravity = 0.2f;
    const float max_speed = 0.5f;
    const float mix = 0.2f;
    const float air_control = 0.15f;

    // Get tile map system
    std::shared_ptr<System_Tilemap> tilemap = c.system_manager.get_system<System_Tilemap>();

    assert(entities.size() == 1); // Only one player

    for (auto const &e : entities) {
        auto &agent = c.get_component<Component_Agent>(e);

        auto &transform = c.get_component<Component_Transform>(e);
        auto &dynamics = c.get_component<Component_Dynamics>(e);

        const auto &collision = c.get_component<Component_Collision>(e);

        // World space collision
        Rectangle world_collision{ transform.position.x + collision.bounds.x, transform.position.y + collision.bounds.y, collision.bounds.width, collision.bounds.height };

        float movement_x = (agent.action == 0 || agent.action == 1) - (agent.action == 6 || agent.action == 7);
        bool jump = (agent.action == 2 || agent.action == 5 || agent.action == 8);
        bool fallthrough = (agent.action == 0 || agent.action == 3 || agent.action == 6);

        // Velocity control
        float mix_x = agent.on_ground ? mix : (mix * air_control);

        dynamics.velocity.x += mix_x * (max_speed * movement_x - dynamics.velocity.x);

        if (std::abs(dynamics.velocity.x) < mix_x * max_speed)
            dynamics.velocity.x = 0.0f;

        if (jump)
            dynamics.velocity.y = -max_jump;
        else if (fallthrough)
            tilemap->set_no_collide(transform.position.x, transform.position.y);

        if (!agent.on_ground)
            dynamics.velocity.y -= gravity * dt;
        
        // Max fall speed is jump speed
        if (std::abs(dynamics.velocity.y) > max_jump)
            dynamics.velocity.y = (dynamics.velocity.y > 0.0f ? 1.0f : -1.0f) * max_jump;

        // Update no collide mask (for fallthrough platform logic)
        tilemap->update_no_collide(world_collision, Rectangle{ transform.position.x - 8.0f, transform.position.x - 8.0f, 16.0f, 16.0f });

        // Move
        transform.position.x += dynamics.velocity.x * dt;
        transform.position.y += dynamics.velocity.y * dt;

        Vector2 offset = tilemap->get_collision_offset(collision.bounds, [](Tile_ID id) -> Collision_Type {
            return (id == wall_mid || id == wall_top ? full : (id == crate ? down_only : none));
        });

        // If moved up, on ground
        agent.on_ground = offset.y >= 0.0f;

        // Correct position
        transform.position.x += offset.x;
        transform.position.y += offset.y;

        // Camera follows the agent
        camera.target = transform.position;
    }
}

void System_Agent::render(int theme) {
    assert(entities.size() == 1); // Only one player

    for (auto const &e : entities) {
        auto const &agent = c.get_component<Component_Agent>(e);
        auto const &transform = c.get_component<Component_Transform>(e);
        auto const &dynamics = c.get_component<Component_Dynamics>(e);

        Texture2D texture;

        if (dynamics.velocity.x == 0.0f)
            texture = stand_textures[theme].texture;

        Vector2 position{ transform.position.x - 0.5f, transform.position.y - 1.0f };

        DrawTextureEx(texture, (Vector2){ position.x * unit_to_pixels, position.y * unit_to_pixels }, 0.0f, unit_to_pixels / texture.width, WHITE);
    }
}
