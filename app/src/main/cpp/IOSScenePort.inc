// Scene, animation and input-range host code translated from iOS commit be203a9.
// Original algorithm order and fixed-step animation semantics are preserved.

void AndroidGame::Impl::rebuildScene(bool resetPlayback) {
    const auto &world = _session->world();
    const bool mapChanged = !_scene || !_scene->matches_map(world);
    auto next = std::make_unique<g1::MissionScene>(*_archive, _terrainTableBytes, world);
    auto terrain = mapChanged ? _renderer->mesh(g1::terrain_vertices(next->terrain)) : _terrain;
    auto animated =
        mapChanged
            ? (next->terrain.animated_surface
                   ? _renderer->mesh(g1::terrain_vertices(next->terrain.animated_surface->quads,
                                                          next->terrain.position_scale))
                   : std::shared_ptr<g1::GLESMesh>{})
            : _animatedTerrain;
    auto controls = _controlQuads;
    if (mapChanged) {
        controls.clear();
        for (std::uint16_t y = 0; y < world.grid.height; ++y)
            for (std::uint16_t x = 0; x < world.grid.width; ++x) {
                const auto id = world.grid.tile_at(x, y);
                if (id >= 32)
                    throw g1::FormatError("Native input grid contains an unsupported cell");
                const auto height =
                    std::int16_t(_terrainTables.at(world.style).at(id).height_steps * 32);
                const auto left = std::int16_t(x * 32), right = std::int16_t((x + 1) * 32);
                const auto top = std::int16_t(y * 32), bottom = std::int16_t((y + 1) * 32);
                g1::TerrainQuad quad{};
                quad.tile_x = x;
                quad.tile_y = y;
                quad.face = g1::TerrainFace::top;
                quad.positions = {{{{left, height, top}},
                                   {{right, height, top}},
                                   {{left, height, bottom}},
                                   {{right, height, bottom}}}};
                controls.push_back(quad);
            }
    }
    for (const auto &image : next->textures) {
        if (!_textures[image.first])
            _textures[image.first] = _renderer->texture(image.second);
        if (_graphicsModern && !_modernTextures[image.first]) {
            // Persisted Modern mode must load the same terrain derivative at
            // first launch/map change as the live graphics-toggle path.
            if (image.first == next->terrain.atlas_resource_id) {
                _modernTextures[image.first] =
                    _renderer->texture(cleanTerrain(image.second, _assets));
                continue;
            }
            const bool sprite =
                std::any_of(next->sprites.begin(), next->sprites.end(), [&](const auto &item) {
                    return item.resource.image_id == image.first;
                });
            if (sprite && image.second.width == 512 && image.second.height == 512)
                _modernTextures[image.first] =
                    _renderer->texture(cleanSprite(image.second, _assets));
        }
    }
    if (!_white)
        _white = _renderer->texture({1, 1, {255, 255, 255, 255}});

    std::map<std::uint8_t, std::size_t> oldByType;
    if (_scene)
        for (std::size_t i = 0; i < _scene->sprites.size(); ++i)
            oldByType.emplace(_scene->world.units.at(_scene->sprites[i].unit_index).type, i);
    decltype(_spriteVertices) vertices;
    decltype(_spriteMeshes) meshes;
    decltype(_animations) animations;
    decltype(_animationIds) animationIds;
    decltype(_unitModels) models;
    decltype(_renderedActive) active;
    decltype(_playbackSpawnGenerations) spawnGenerations, transformGenerations;
    decltype(_pendingTransformCompletions) completions;
    std::vector<std::size_t> resetIndices;
    for (const auto &sprite : next->sprites) {
        const auto index = sprite.unit_index;
        const auto type = world.units.at(index).type;
        const auto found = oldByType.find(type);
        const auto &unit = _session->units().at(index);
        const bool reuse = !resetPlayback && !mapChanged && found != oldByType.end();
        if (reuse) {
            const auto old = found->second;
            vertices.push_back(_spriteVertices.at(old));
            meshes.push_back(_spriteMeshes.at(old));
            animations.push_back(_animations.at(old));
            animationIds.push_back(_animationIds.at(old));
            active.push_back(_renderedActive.at(old));
            models.push_back(_unitModels.at(old));
            spawnGenerations.push_back(_playbackSpawnGenerations.at(old));
            transformGenerations.push_back(_playbackTransformGenerations.at(old));
            completions.push_back(_pendingTransformCompletions.at(old));
        } else {
            std::vector<std::vector<g1::SceneVertex>> frames;
            std::vector<std::shared_ptr<g1::GLESMesh>> frameMeshes;
            for (std::size_t frame = 0; frame < sprite.descriptor.frames().size(); ++frame) {
                frames.push_back(g1::sprite_vertices(sprite.descriptor, frame));
                frameMeshes.push_back(_renderer->mesh(frames.back()));
            }
            vertices.push_back(std::move(frames));
            meshes.push_back(std::move(frameMeshes));
            animations.push_back(std::nullopt);
            animationIds.push_back(-1);
            active.push_back(false);
            models.push_back(g1::identity_matrix());
            spawnGenerations.push_back(0);
            transformGenerations.push_back(0);
            completions.push_back(std::nullopt);
        }
        if (unit.active && (!animations.back() || !active.back() ||
                            spawnGenerations.back() != _session->unit_spawn_generation(index))) {
            if (unit.form > 1 || unit.facing > 3)
                throw g1::FormatError("New unit has unsupported sprite form/facing");
            const auto value = _animationTable.at(type).at(unit.form * 4 + unit.facing);
            const int animation = value < 128 ? int(value) : int(value) - 256;
            if (animation < 0 ||
                static_cast<std::size_t>(animation) >= sprite.descriptor.animations().size())
                throw g1::FormatError(
                    "New unit's initial animation is not present in its descriptor");
            animations.back().emplace(sprite.descriptor.animations()[animation], -1, false);
            animationIds.back() = animation;
            models.back() = unit_model(world, unit);
            active.back() = true;
            spawnGenerations.back() = _session->unit_spawn_generation(index);
            transformGenerations.back() = 0;
            completions.back().reset();
            resetIndices.push_back(index);
            ++_spawnPlaybackResets;
        } else if (!unit.active) {
            animations.back().reset();
            animationIds.back() = -1;
            active.back() = false;
            transformGenerations.back() = 0;
            completions.back().reset();
        }
    }
    _scene = std::move(next);
    _terrain = std::move(terrain);
    _animatedTerrain = std::move(animated);
    _controlQuads = std::move(controls);
    _spriteVertices = std::move(vertices);
    _spriteMeshes = std::move(meshes);
    _animations = std::move(animations);
    _animationIds = std::move(animationIds);
    _unitModels = std::move(models);
    _renderedActive = std::move(active);
    _playbackSpawnGenerations = std::move(spawnGenerations);
    _playbackTransformGenerations = std::move(transformGenerations);
    _pendingTransformCompletions = std::move(completions);
    if (mapChanged || resetPlayback)
        _previousUnits = _session->units();
    else
        while (_previousUnits.size() < _session->units().size())
            _previousUnits.push_back(_session->units()[_previousUnits.size()]);
    for (const auto index : resetIndices)
        _previousUnits.at(index) = _session->units().at(index);
    _range.reset();
    _cursor.reset();
    _attackRange.reset();
    _healRange.reset();
    _rangeDirty = true;
    _moveMode = false;
    _attackMode = false;
    _healMode = false;
    _pendingDestination.reset();
    _previewRange.reset();
    _attackTargets.clear();
    _healTargets.clear();
    _lastSelection.reset();
    _productionMode.reset();
    _productionType.reset();
    _productionPlacement.reset();
    _productionRange.reset();
    _returnCursor.reset();
    _typeListOpen = false;
    _inputEdges = 0;
    _dialogueCommand = std::numeric_limits<std::size_t>::max();
    if (mapChanged) {
        _cameraPan.reset();
        _camera.set_modern(_graphicsModern);
        _camera.fit(*_scene, Size{double(_width), double(_height)});
        _decorationTerrain.reset();
        _renderedTerrainGeneration = 0;
        _renderedTerrainWorld = 0;
        _terrainSpriteDraws.clear();
        _worldOverlaySignature.clear();
    }
    ++_sceneRevision;
    ++_sceneRebuilds;
    ++_contentGeneration;
}

void AndroidGame::Impl::synchronizeScene() {
    if (!_scene || !_scene->matches_map(_session->world()) ||
        !_scene->matches_roster(_session->world().units))
        rebuildScene(false);
}

void AndroidGame::Impl::refreshWorldOverlays() {
    const auto &world = _session->world();
    const auto &network = _session->captures();
    if (world.sites.size() != network.sites.size() ||
        world.sites.size() != network.item_links.size())
        throw g1::FormatError("Visible site ownership differs from the current capture network");
    std::vector<std::int32_t> signature{static_cast<std::int32_t>(world.grid_resource_id),
                                        world.style, static_cast<std::int32_t>(world.sites.size())};
    for (std::size_t i = 0; i < world.sites.size(); ++i) {
        const auto &site = world.sites[i];
        const auto &state = network.sites[i];
        signature.insert(signature.end(),
                         {site.position.x, site.position.y, state.state,
                          static_cast<std::int32_t>(site.coordinate_groups.size())});
        if (site.coordinate_groups.size() != state.item_states.size() ||
            site.coordinate_groups.size() != network.item_links[i].size())
            throw g1::FormatError("Visible site group shape is inconsistent");
        for (std::size_t g = 0; g < site.coordinate_groups.size(); ++g) {
            const auto &group = site.coordinate_groups[g];
            if (group.size() != state.item_states[g].size() ||
                group.size() != network.item_links[i][g].size())
                throw g1::FormatError("Visible capture point shape is inconsistent");
            signature.push_back(static_cast<std::int32_t>(group.size()));
            for (std::size_t j = 0; j < group.size(); ++j)
                signature.insert(signature.end(), {group[j].x, group[j].y, state.item_states[g][j],
                                                   network.item_links[i][g][j]});
        }
    }
    signature.push_back(static_cast<std::int32_t>(world.site_points.size()));
    for (const auto &point : world.site_points)
        signature.insert(signature.end(), {point.point.x, point.point.y, point.point.state});
    for (const auto &marker : {world.marker_254, world.marker_25c})
        signature.insert(signature.end(), {marker[0], marker[1]});
    if (signature == _worldOverlaySignature)
        return;
    std::vector<NativeWorldOverlay> overlays;
    const auto color = [](int owner) -> std::array<float, 4> {
        if (owner == 0)
            return {{0.18F, 0.70F, 1.0F, 0.88F}};
        if (owner == 1)
            return {{1.0F, 0.27F, 0.30F, 0.88F}};
        if (owner == -1)
            return {{0.85F, 0.85F, 0.85F, 0.84F}};
        throw g1::FormatError("Unknown displayed site owner");
    };
    const auto add = [&](int x, int y, int shape, std::array<float, 4> tint, bool linked) {
        if (x < 0 || y < 0 || x >= world.grid.width || y >= world.grid.height)
            throw g1::FormatError("Visible world marker is outside its map");
        const auto tile =
            world.grid.tile_at(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
        if (tile >= 32)
            throw g1::FormatError("Visible marker uses an unsupported terrain cell");
        const float px = (float(x) + 0.5F) * 0.32F, pz = (float(y) + 0.5F) * 0.32F;
        const float height =
            float(_terrainTables.at(world.style).at(tile).height_steps) * 0.32F + 0.012F;
        std::vector<g1::SceneVertex> vertices;
        const auto line = [&](float ax, float az, float bx, float bz, float width) {
            const float length = std::hypot(bx - ax, bz - az);
            if (length <= 0)
                return;
            const float dx = (bz - az) * width / (2 * length),
                        dz = (ax - bx) * width / (2 * length);
            const std::array<std::array<float, 2>, 4> p{{{{ax - dx, az - dz}},
                                                         {{bx - dx, bz - dz}},
                                                         {{ax + dx, az + dz}},
                                                         {{bx + dx, bz + dz}}}};
            for (const auto index : g1::sprite_triangle_indices)
                vertices.push_back(
                    {{{px + p[index][0], height, pz + p[index][1], 1}}, {{0, 0}}, {}});
        };
        const float r = shape == 0 ? 0.135F : shape == 1 ? 0.105F : 0.145F;
        if (shape == 0) {
            line(-r, -r, r, -r, 0.025F);
            line(r, -r, r, r, 0.025F);
            line(r, r, -r, r, 0.025F);
            line(-r, r, -r, -r, 0.025F);
        } else if (shape == 1 || shape == 3) {
            line(0, -r, r, 0, 0.025F);
            line(r, 0, 0, r, 0.025F);
            line(0, r, -r, 0, 0.025F);
            line(-r, 0, 0, -r, 0.025F);
        } else {
            line(-r, 0, r, 0, 0.025F);
            line(0, -r, 0, r, 0.025F);
            if (shape == 4) {
                line(-r, -r, r, r, 0.016F);
                line(-r, r, r, -r, 0.016F);
            }
        }
        if (linked) {
            line(-0.035F, 0, 0.035F, 0, 0.025F);
            line(0, -0.035F, 0, 0.035F, 0.025F);
        }
        overlays.push_back({_renderer->mesh(vertices), tint});
    };
    for (std::size_t i = 0; i < world.sites.size(); ++i) {
        const auto &site = world.sites[i];
        const auto &state = network.sites[i];
        add(site.position.x, site.position.y, 0, color(state.state), false);
        for (std::size_t g = 0; g < site.coordinate_groups.size(); ++g)
            for (std::size_t j = 0; j < site.coordinate_groups[g].size(); ++j) {
                const auto p = site.coordinate_groups[g][j];
                add(p.x, p.y, 1, color(state.item_states[g][j]), network.item_links[i][g][j] != 0);
            }
    }
    for (const auto &point : world.site_points)
        if (point.point.state != 0)
            add(point.point.x, point.point.y, 4, {{0.75F, 0.4F, 1, 0.85F}}, false);
    if (world.marker_254[0] >= 0 && world.marker_254[1] >= 0)
        add(world.marker_254[0], world.marker_254[1], 2, {{1, 0.83F, 0.16F, 0.9F}}, false);
    if (world.marker_25c[0] >= 0 && world.marker_25c[1] >= 0)
        add(world.marker_25c[0], world.marker_25c[1], 3, {{0.15F, 1, 0.85F, 0.9F}}, false);
    _worldOverlays = std::move(overlays);
    _worldOverlaySignature = std::move(signature);
    ++_contentGeneration;
}

void AndroidGame::Impl::refreshTerrainDecorations() {
    if (_renderedTerrainGeneration == _session->terrain_generation() &&
        _renderedTerrainWorld == _session->world_generation())
        return;
    const auto *state = _session->terrain_state();
    const auto *layout = _session->terrain_layout();
    if (!_scene->terrain.animated_surface) {
        if (state && !state->slots.empty())
            throw g1::FormatError("Renderable terrain slots lack their material");
        _decorationTerrain.reset();
    } else {
        if (!state || !layout || !_session->terrain_tables())
            throw g1::FormatError("Animated terrain lacks its session-owned runtime");
        const auto quads = g1::terrain_decoration_quads(*layout, *state, _session->world().grid,
                                                        *_session->terrain_tables());
        _decorationTerrain =
            _renderer->mesh(g1::terrain_vertices(quads, _scene->terrain.position_scale));
    }
    std::vector<g1::TerrainSpriteDecorationDraw> sprites;
    if (const auto *spriteState = _session->terrain_sprite_state()) {
        const auto *spriteLayout = _session->terrain_sprite_layout();
        const auto *bank = _session->terrain_sprite_animations();
        if (!spriteLayout || !bank)
            throw g1::FormatError("Terrain sprite state has no decoded layout or animations");
        sprites = g1::terrain_sprite_decoration_draws(*spriteLayout, *bank, *spriteState);
        const auto read = [&](std::uint32_t id, std::uint32_t key) {
            const auto found = _archive->find(id, key);
            if (found.size() != 1)
                throw g1::FormatError("Terrain sprite resource is missing or ambiguous");
            return _archive->read(found.front());
        };
        for (const auto &sprite : sprites) {
            const auto &resource = sprite.resource;
            if (!_textures[resource.image_id]) {
                const auto image = g1::decode_png(read(resource.image_id, resource.resource_key));
                if (image.width != 512 || image.height != 512)
                    throw g1::FormatError(
                        "Terrain sprite atlas dimensions differ from original512x512");
                _textures[resource.image_id] = _renderer->texture(image);
            }
            if (_graphicsModern && !_modernTextures[resource.image_id]) {
                const auto image = g1::decode_png(read(resource.image_id, resource.resource_key));
                if (image.width != 512 || image.height != 512)
                    throw g1::FormatError("Modern decoration source is not a 512x512 atlas");
                _modernTextures[resource.image_id] =
                    _renderer->texture(cleanSprite(image, _assets));
            }
            if (!_terrainSpriteMeshes.count(resource.descriptor_id)) {
                const g1::AtlasDescriptor descriptor(
                    read(resource.descriptor_id, resource.resource_key));
                std::vector<std::shared_ptr<g1::GLESMesh>> frames;
                for (std::size_t i = 0; i < descriptor.frames().size(); ++i)
                    frames.push_back(_renderer->mesh(g1::sprite_vertices(descriptor, i)));
                _terrainSpriteMeshes.emplace(resource.descriptor_id, std::move(frames));
            }
            if (sprite.frame_index >= _terrainSpriteMeshes.at(resource.descriptor_id).size())
                throw g1::FormatError("Terrain sprite frame is outside its actual descriptor");
        }
    }
    _terrainSpriteDraws = std::move(sprites);
    _renderedTerrainGeneration = _session->terrain_generation();
    _renderedTerrainWorld = _session->world_generation();
    ++_contentGeneration;
}

void AndroidGame::Impl::refreshRange() {
    if (!_rangeDirty)
        return;
    _rangeDirty = false;
    _range.reset();
    _cursor.reset();
    _attackRange.reset();
    _healRange.reset();
    _productionRange.reset();
    _previewRange = _session->selected_movement_range();
    _attackTargets = _session->selected_attack_targets();
    _healTargets = _session->selected_heal_targets();
    if (!_previewRange) {
        _moveMode = false;
        _pendingDestination.reset();
    }
    if (_attackTargets.empty())
        _attackMode = false;
    if (_healTargets.empty())
        _healMode = false;
    if (_moveMode && _previewRange) {
        g1::TerrainMesh selected;
        selected.position_scale = _scene->terrain.position_scale;
        g1::TerrainMesh cursor;
        cursor.position_scale = selected.position_scale;
        for (const auto &quad : _controlQuads)
            if (quad.face == g1::TerrainFace::top &&
                _previewRange->at(quad.tile_x, quad.tile_y) >= 0 &&
                _previewRange->at(quad.tile_x, quad.tile_y) != g1::movement_reserved_cell) {
                selected.quads.push_back(quad);
                if (_pendingDestination && _pendingDestination->x == quad.tile_x &&
                    _pendingDestination->y == quad.tile_y)
                    cursor.quads.push_back(quad);
            }
        auto vertices = g1::terrain_vertices(selected);
        for (auto &vertex : vertices) {
            vertex.position[1] += 0.003F;
            vertex.uv = {{0, 0}};
        }
        if (!vertices.empty())
            _range = _renderer->mesh(vertices);
        vertices = g1::terrain_vertices(cursor);
        for (auto &vertex : vertices) {
            vertex.position[1] += 0.006F;
            vertex.uv = {{0, 0}};
        }
        if (!vertices.empty())
            _cursor = _renderer->mesh(vertices);
    }
    if (_attackMode) {
        g1::TerrainMesh targets;
        targets.position_scale = _scene->terrain.position_scale;
        for (const auto &quad : _controlQuads)
            if (quad.face == g1::TerrainFace::top)
                for (const auto index : _attackTargets) {
                    const auto &unit = _session->units().at(index);
                    if (unit.tile_x == quad.tile_x && unit.tile_y == quad.tile_y) {
                        targets.quads.push_back(quad);
                        break;
                    }
                }
        auto vertices = g1::terrain_vertices(targets);
        for (auto &vertex : vertices) {
            vertex.position[1] += 0.006F;
            vertex.uv = {{0, 0}};
        }
        if (!vertices.empty())
            _attackRange = _renderer->mesh(vertices);
    }
    if (_healMode) {
        g1::TerrainMesh targets;
        targets.position_scale = _scene->terrain.position_scale;
        for (const auto &quad : _controlQuads)
            if (quad.face == g1::TerrainFace::top)
                for (const auto index : _healTargets) {
                    const auto &unit = _session->units().at(index);
                    if (unit.tile_x == quad.tile_x && unit.tile_y == quad.tile_y) {
                        targets.quads.push_back(quad);
                        break;
                    }
                }
        auto vertices = g1::terrain_vertices(targets);
        for (auto &vertex : vertices) {
            vertex.position[1] += 0.006F;
            vertex.uv = {{0, 0}};
        }
        if (!vertices.empty())
            _healRange = _renderer->mesh(vertices);
    }
    if (_productionMode && _productionType) {
        _productionPlacement = _session->selected_production_placement(*_productionMode);
        g1::TerrainMesh targets, cursor;
        targets.position_scale = cursor.position_scale = _scene->terrain.position_scale;
        for (const auto &quad : _controlQuads)
            if (productionTileAllowed(
                    g1::WorldCoordinate{static_cast<std::uint8_t>(quad.tile_x),
                                        static_cast<std::uint8_t>(quad.tile_y)})) {
                targets.quads.push_back(quad);
                if (_pendingDestination && _pendingDestination->x == quad.tile_x &&
                    _pendingDestination->y == quad.tile_y)
                    cursor.quads.push_back(quad);
            }
        auto vertices = g1::terrain_vertices(targets);
        for (auto &vertex : vertices) {
            vertex.position[1] += 0.006F;
            vertex.uv = {{0, 0}};
        }
        if (!vertices.empty())
            _productionRange = _renderer->mesh(vertices);
        vertices = g1::terrain_vertices(cursor);
        for (auto &vertex : vertices) {
            vertex.position[1] += 0.009F;
            vertex.uv = {{0, 0}};
        }
        if (!vertices.empty())
            _cursor = _renderer->mesh(vertices);
    } else if (_returnCursor && !_moveMode) {
        g1::TerrainMesh cursor;
        cursor.position_scale = _scene->terrain.position_scale;
        for (const auto &quad : _controlQuads)
            if (quad.tile_x == _returnCursor->x && quad.tile_y == _returnCursor->y)
                cursor.quads.push_back(quad);
        auto vertices = g1::terrain_vertices(cursor);
        for (auto &vertex : vertices) {
            vertex.position[1] += 0.006F;
            vertex.uv = {{0, 0}};
        }
        if (!vertices.empty())
            _cursor = _renderer->mesh(vertices);
    }
    ++_contentGeneration;
}

void AndroidGame::Impl::advanceCamera(std::uint32_t delta) {
    const auto &mission = _session->mission();
    std::uint64_t token = 0;
    bool sessionFocus = false, immediate = false;
    std::int32_t x = 0, y = 0;
    if (mission.gate == g1::MissionGate::camera && mission.camera) {
        token = mission.command_index;
        x = mission.camera->x;
        y = mission.camera->y;
        immediate = mission.camera->immediate;
    } else if (_session->focus_request()) {
        const auto &request = *_session->focus_request();
        token = request.token;
        sessionFocus = true;
        x = request.tile.x;
        y = request.tile.y;
        immediate = request.immediate;
    } else {
        _cameraPan.reset();
        return;
    }
    if (!_cameraPan || _cameraPan->token != token || _cameraPan->session_focus != sessionFocus) {
        if (x < 0 || y < 0 || x >= _session->world().grid.width ||
            y >= _session->world().grid.height)
            throw g1::FormatError("Camera request lies outside the recovered scene");
        _cameraPan = NativeCameraPan{token,
                                     sessionFocus,
                                     _camera.center_x,
                                     _camera.center_z,
                                     (x + 0.5F) * 0.32F,
                                     (y + 0.5F) * 0.32F,
                                     0,
                                     immediate ? 0U : 360U,
                                     false};
    }
    auto &pan = *_cameraPan;
    if (pan.reached)
        return;
    pan.elapsed = std::min(pan.elapsed + delta, pan.duration);
    const float t = pan.duration ? static_cast<float>(pan.elapsed) / pan.duration : 1.0F;
    const float smooth = t * t * (3 - 2 * t);
    _camera.center_x = pan.from_x + (pan.to_x - pan.from_x) * smooth;
    _camera.center_z = pan.from_z + (pan.to_z - pan.from_z) * smooth;
    pan.reached = pan.elapsed == pan.duration;
    ++_contentGeneration;
}

void AndroidGame::Impl::advanceSprites(std::uint32_t delta) {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < _scene->sprites.size(); ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return _session->world().units[_scene->sprites[a].unit_index].type <
               _session->world().units[_scene->sprites[b].unit_index].type;
    });
    // Original6cd20 stores one shared delta for the entire ascending-type loop.
    auto sharedDelta = delta;
    for (const auto i : order) {
        const auto index = _scene->sprites[i].unit_index;
        const auto &unit = _session->units().at(index);
        const auto &worldUnit = _session->world().units.at(index);
        if (!unit.active) {
            _renderedActive[i] = false;
            _animations[i].reset();
            _animationIds[i] = -1;
            _playbackTransformGenerations[i] = 0;
            _pendingTransformCompletions[i].reset();
            continue;
        }
        const auto &row = _animationTable.at(worldUnit.type);
        const auto signedId = [](std::uint8_t value) {
            return value < 128 ? int(value) : int(value) - 256;
        };
        if (!_animations[i] || !_renderedActive[i] ||
            _playbackSpawnGenerations[i] != _session->unit_spawn_generation(index)) {
            if (unit.form > 1 || unit.facing > 3)
                throw g1::FormatError("Activated unit has unsupported sprite form/facing");
            const int initial = signedId(row[unit.form * 4 + unit.facing]);
            if (initial < 0 || static_cast<std::size_t>(initial) >=
                                   _scene->sprites[i].descriptor.animations().size())
                throw g1::FormatError(
                    "Activated unit's initial animation is not present in its descriptor");
            _animations[i].emplace(_scene->sprites[i].descriptor.animations()[initial], -1, false);
            _animationIds[i] = initial;
            _renderedActive[i] = true;
            _playbackSpawnGenerations[i] = _session->unit_spawn_generation(index);
            _playbackTransformGenerations[i] = 0;
            _pendingTransformCompletions[i].reset();
            if (index < _previousUnits.size())
                _previousUnits[index] = unit;
            ++_spawnPlaybackResets;
            ++_contentGeneration;
        }
        const int special = signedId(row[8]);
        bool reverse = false;
        std::int32_t repeats = -1;
        int desired;
        if (_session->transform_animation_pending(index)) {
            desired = special;
            reverse = unit.form == 0;
            repeats = 1;
            _playbackTransformGenerations[i] = _session->unit_transform_generation(index);
            _pendingTransformCompletions[i].reset();
            _session->clear_transform_animation_request(index); // Original6cdc6.
        } else {
            if (unit.form > 1 || unit.facing > 3)
                throw g1::FormatError("Unsupported sprite form/facing");
            desired = signedId(row[unit.form * 4 + unit.facing]);
        }
        const auto beforeFrame = _animations[i]->current_frame();
        bool changed = false;
        if (_animationIds[i] == special && !_animations[i]->state().completed) {
            if (sharedDelta > std::uint32_t(std::numeric_limits<std::int32_t>::max() / 2))
                throw g1::FormatError("Original shared sprite delta exceeded signed range");
            sharedDelta *= 2;
        } else if (desired != _animationIds[i] && desired != -1) {
            if (desired < 0 || static_cast<std::size_t>(desired) >=
                                   _scene->sprites[i].descriptor.animations().size())
                throw g1::FormatError("Sprite animation request exceeds recovered descriptor");
            // Original 6ce42..6ce50 clears global map+28 when the OLD
            // animation equals row8. It does not compare that global to the
            // current unit. Native host completion is deferred until the
            // resulting normal-animation frame has actually been presented.
            if (_animationIds[i] == special && _playbackTransformGenerations[i] != 0)
                _pendingTransformCompletions[i] = NativeTransformCompletion{
                    index, _playbackTransformGenerations[i], _playbackSpawnGenerations[i]};
            _animations[i] = g1::SpriteAnimation(
                _scene->sprites[i].descriptor.animations()[desired], repeats, reverse);
            _animationIds[i] = desired;
            changed = true; // Original new sequence is not advanced in this tick.
        }
        if (!changed) {
            if (unit.flag_2c && worldUnit.owner == _session->current_player() &&
                _animationIds[i] != special)
                _animations[i]->select_step(0);
            else
                _animations[i]->advance_ms(sharedDelta);
        }
        if (changed || beforeFrame != _animations[i]->current_frame())
            ++_contentGeneration;
    }
}
