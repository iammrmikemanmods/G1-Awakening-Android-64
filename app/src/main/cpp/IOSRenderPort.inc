// Platform translation of Build017 NativeApp.mm scene draw orchestration.
// Caller binds the scene framebuffer/viewport; presentation receipts are only
// delivered by AndroidGame::presented after successful EGL buffer swapping.
void AndroidGame::Impl::renderScene(NativePresentedFrame &frame, Size renderSize) {
    if (!_loaded || _failed || !_scene || !_session)
        return;
    if (renderSize.width <= 0 || renderSize.height <= 0)
        throw FormatError("Scene render target has no extent");
    if (_session->battle())
        prepareBattle();
    frame.content_generation = _contentGeneration;
    frame.scene_revision = _sceneRevision;
    frame.presentation_epoch = _presentationEpoch.current();
    const bool support =
        _session->battle() && _supportPresentation && !_supportPresentation->status().complete;
    if (!_session->battle() || support) {
        renderTactical(frame, renderSize, support);
        return;
    }
    if (!_battlePresentation)
        throw FormatError("Pending battle has no render presentation");
    std::vector<const M3GRenderAsset *> uploaded;
    for (const auto &model : _battleModels)
        uploaded.push_back(&model->asset());
    // Preserve source vertical FOV/parallel height; adapt only drawable aspect.
    const auto output = _battlePresentation->prepare_frame(
        {{static_cast<float>(renderSize.width), static_cast<float>(renderSize.height)}}, uploaded,
        _simulationClock.interpolation(), static_cast<float>(renderSize.width / renderSize.height));
    const auto status = _battlePresentation->status();
    if (_uiState.battle && _uiState.battle->status.token == output.token &&
        _uiState.battle->status.revision == output.revision) {
        _uiState.battle->damage_anchor.reset();
        if (status.damage_text) {
            const std::size_t target = status.damage_on_left ? 1U : 2U;
            for (const auto &submission : output.submissions)
                if (submission.asset_index == target) {
                    if (const auto anchor = projected_actor_top(submission.frame)) {
                        const auto safe = _ui->layout().safe;
                        if (safe.width > 0 && safe.height > 0) {
                            // AndroidUI expects normalized safe-canvas coordinates,
                            // while the actor projection covers the full drawable.
                            _uiState.battle->damage_anchor = std::array<float, 2>{
                                {float(((*anchor)[0] * _width - safe.x) / safe.width),
                                 float(((*anchor)[1] * _height - safe.y) / safe.height)}};
                        }
                    }
                }
        }
    }
    GLbitfield clearMask = 0;
    if (output.clear_color) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(output.background_color[0], output.background_color[1],
                     output.background_color[2], output.background_color[3]);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    if (output.clear_depth) {
        glDepthMask(GL_TRUE);
        glClearDepthf(1);
        clearMask |= GL_DEPTH_BUFFER_BIT;
    }
    if (clearMask) {
        glDisable(GL_SCISSOR_TEST);
        glClear(clearMask);
    }
    std::size_t queued = 0;
    for (const auto &submission : output.submissions) {
        // Each source node call flushes its own queue: world, actors, effects.
        _m3gRenderer->encode_frame({_battleModels.at(submission.asset_index)}, submission.frame);
        queued += submission.frame.draws.size();
    }
    frame.battle =
        NativePresentedFrame::Battle{output.token, output.revision, output.complete, queued};
}

void AndroidGame::Impl::renderTactical(NativePresentedFrame &frame, Size renderSize, bool support) {
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(0.025F, 0.04F, 0.065F, 1);
    glClearDepthf(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto camera = _camera.matrix();
    _tacticalDrawn = 0;
    _tacticalFallback = 0;
    _tacticalHitBounds.clear();
    std::vector<std::pair<std::shared_ptr<GLESModel>, M3GPreparedFrame>> tacticalFrames;
    std::vector<GLESDraw> draws{
        {_terrain, spriteTexture(_scene->terrain.atlas_resource_id), camera, {{1, 1, 1, 1}}, true}};
    if (_scene->terrain.animated_surface && !_session->terrain_state())
        throw FormatError("Animated terrain has no native clock state");
    const float terrainOffset =
        _session->terrain_state() ? terrain_animation_u_offset(_session->terrain_state()->animation)
                                  : 0;
    if (_decorationTerrain && _scene->terrain.animated_surface)
        draws.push_back({_decorationTerrain,
                         _textures.at(_scene->terrain.animated_surface->atlas_resource_id),
                         camera,
                         {{1, 1, 1, 1}},
                         true,
                         {{terrainOffset, 0}}});
    if (_animatedTerrain && _scene->terrain.animated_surface)
        draws.push_back({_animatedTerrain,
                         _textures.at(_scene->terrain.animated_surface->atlas_resource_id),
                         camera,
                         {{1, 1, 1, 1}},
                         true,
                         {{terrainOffset, 0}}});
    for (const auto &decoration : _terrainSpriteDraws)
        draws.push_back(
            {_terrainSpriteMeshes.at(decoration.resource.descriptor_id).at(decoration.frame_index),
             spriteTexture(decoration.resource.image_id),
             multiply_matrix(camera, decoration.model),
             {{1, 1, 1, 1}},
             true,
             {},
             false,
             false});
    for (const auto &overlay : _worldOverlays)
        draws.push_back({overlay.mesh, _white, camera, overlay.tint, false});
    if (!support) {
        if (_range)
            draws.push_back({_range, _white, camera, {{0.12F, 0.9F, 0.85F, 0.36F}}, false});
        if (_cursor)
            draws.push_back({_cursor, _white, camera, {{1, 0.82F, 0.12F, 0.6F}}, false});
        if (_attackRange)
            draws.push_back({_attackRange, _white, camera, {{1, 0.18F, 0.14F, 0.48F}}, false});
        if (_healRange)
            draws.push_back({_healRange, _white, camera, {{0.20F, 1, 0.35F, 0.48F}}, false});
        if (_productionRange)
            draws.push_back({_productionRange, _white, camera, {{0.25F, 0.85F, 1, 0.48F}}, false});
    } else if (_supportCursor)
        draws.push_back({_supportCursor, _white, camera, {{1, 0.82F, 0.12F, 0.6F}}, false});
    std::vector<std::size_t> order;
    if (support) {
        auto snapshots = _supportUnitDraws;
        for (auto &draw : snapshots)
            draw.matrix = multiply_matrix(camera, draw.matrix);
        std::stable_sort(snapshots.begin(), snapshots.end(), [](const auto &a, const auto &b) {
            return transform(a.matrix, {{0, 0, 0, 1}})[2] > transform(b.matrix, {{0, 0, 0, 1}})[2];
        });
        draws.insert(draws.end(), snapshots.begin(), snapshots.end());
        const auto status = _supportPresentation->status();
        if (!status.effect_complete) {
            if (!_supportAssets || !_supportTexture)
                throw FormatError("Support presentation assets are absent");
            const auto &unit = _session->battle()->units_before.at(status.helper_unit.value());
            const auto vertices = support_overlay_vertices(
                _supportAssets->descriptor, status.effect_frame, _supportAssets->image.width,
                _supportAssets->image.height, camera, unit.tile_x, unit.tile_y,
                static_cast<float>(renderSize.width), static_cast<float>(renderSize.height));
            if (!vertices.empty())
                draws.push_back({_renderer->mesh(vertices),
                                 _supportTexture,
                                 identity_matrix(),
                                 {{1, 1, 1, 1}},
                                 false,
                                 {},
                                 true,
                                 true});
        }
        // Root creates the SupportFrameReceipt after AndroidUI renders the
        // matching banner; this function cannot infer a UI draw from a string.
    } else {
        for (std::size_t i = 0; i < _animations.size(); ++i)
            if (_session->units().at(_scene->sprites[i].unit_index).active && _animations[i])
                order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            const auto pa = transform(multiply_matrix(camera, _unitModels[a]), {{0, 0, 0, 1}});
            const auto pb = transform(multiply_matrix(camera, _unitModels[b]), {{0, 0, 0, 1}});
            return pa[2] > pb[2];
        });
        if (_renderQuality.soft_contact_shadows && _contactShadowMesh) {
            for (const auto i : order) {
                const auto &unit = _session->units().at(_scene->sprites[i].unit_index);
                if (!unit.active || unit.tile_x >= _session->world().grid.width ||
                    unit.tile_y >= _session->world().grid.height)
                    continue;
                const auto cell = _session->world().grid.tile_at(unit.tile_x, unit.tile_y);
                if (cell >= 32)
                    continue;
                auto model = identity_matrix();
                model[12] = _unitModels[i][12];
                model[14] = _unitModels[i][14];
                model[13] =
                    _terrainTables.at(_session->world().style).at(cell).height_steps * 0.32F +
                    0.002F;
                draws.push_back({_contactShadowMesh,
                                 _contactShadowTexture,
                                 multiply_matrix(camera, model),
                                 {{1, 1, 1, 1}},
                                 false});
            }
        }
        for (const auto i : order) {
            const auto index = _scene->sprites[i].unit_index;
            const auto &unit = _session->units().at(index);
            const auto type = _session->world().units.at(index).type;
            const bool moving =
                unit.offset_x != 0 || unit.offset_y != 0 ||
                (_session->moving() && (!_session->selected() || *_session->selected() == index));
            const bool transforming = _session->unit_transform_waiting(index) ||
                                      _session->transform_animation_pending(index) != 0;
            bool replaced = false;
            if (_graphicsModern && unit.active && !_renderMemoryPressure &&
                !_tacticalErrors.count(type)) {
                try {
                    if (!_tacticalModels.count(type)) {
                        auto model = std::make_unique<TacticalModel>(*_archive, type);
                        auto gpu = _m3gRenderer->upload_modern(model->asset(),
                                                               MaterialEnhancement::character);
                        _tacticalGpu.emplace(type, std::move(gpu));
                        _tacticalModels.emplace(type, std::move(model));
                    }
                    const auto &model = *_tacticalModels.at(type);
                    const auto &world = _session->world();
                    const auto cell = world.grid.tile_at(unit.tile_x, unit.tile_y);
                    if (cell < 32 && model.supports(unit.form) &&
                        (!moving || transforming || model.walking_supported(unit.form)) &&
                        (!transforming || (model.supports(0) && model.supports(1)))) {
                        const float height =
                            _terrainTables.at(world.style).at(cell).height_steps * 0.32F;
                        float x = (unit.tile_x + 0.5F) * 0.32F + unit.offset_x * 0.01F,
                              z = (unit.tile_y + 0.5F) * 0.32F + unit.offset_y * 0.01F;
                        if (index < _previousUnits.size() && _previousUnits[index].active &&
                            _previousUnits[index].form == unit.form) {
                            const auto &previous = _previousUnits[index];
                            const float px = (previous.tile_x + 0.5F) * 0.32F +
                                             previous.offset_x * 0.01F,
                                        pz = (previous.tile_y + 0.5F) * 0.32F +
                                             previous.offset_y * 0.01F;
                            if (std::abs(x - px) <= 0.64F && std::abs(z - pz) <= 0.64F) {
                                const float a = _simulationClock.interpolation();
                                x = px + (x - px) * a;
                                z = pz + (z - pz) * a;
                            }
                        }
                        TacticalMotion motion;
                        motion.walking = moving && !transforming;
                        motion.transforming = transforming;
                        motion.stride_phase = tactical_stride(x, z, unit.facing);
                        motion.vehicle_fraction = unit.form;
                        if (transforming) {
                            const auto raw = _animationTable.at(type)[8];
                            const int special = raw < 128 ? int(raw) : int(raw) - 256;
                            if (_animationIds[i] == special && special >= 0)
                                motion.vehicle_fraction = transformation_fraction(
                                    _scene->sprites[i].descriptor.animations().at(special),
                                    _animations[i]->state());
                            else if (_session->transform_animation_pending(index))
                                motion.vehicle_fraction = unit.form == 0 ? 1.0F : 0.0F;
                        }
                        auto gpu = _tacticalGpu.at(type);
                        auto prepared = model.prepare(
                            gpu->asset(), unit.form, unit.facing, {{x, height + 0.002F, z}}, camera,
                            {{static_cast<std::uint32_t>(renderSize.width),
                              static_cast<std::uint32_t>(renderSize.height)}},
                            motion);
                        Bounds hit;
                        for (const auto &draw : prepared.draws)
                            for (const auto &vertex :
                                 gpu->asset().geometry.surfaces.at(draw.geometry_index).vertices) {
                                const auto p = transform(draw.metal_matrix, vertex.position);
                                if (p[3] <= 0.0001F || p[2] < 0 || p[2] > p[3])
                                    continue;
                                hit.add((p[0] / p[3] * 0.5 + 0.5) * _width,
                                        (0.5 - p[1] / p[3] * 0.5) * _height);
                            }
                        if (!prepared.draws.empty()) {
                            if (hit.valid)
                                _tacticalHitBounds[i] = hit;
                            tacticalFrames.emplace_back(std::move(gpu), std::move(prepared));
                            replaced = true;
                            ++_tacticalDrawn;
                        }
                    }
                } catch (const std::exception &error) {
                    _tacticalErrors[type] = error.what();
                }
            }
            if (!replaced) {
                ++_tacticalFallback;
                draws.push_back({_spriteMeshes[i][_animations[i]->current_frame()],
                                 spriteTexture(_scene->sprites[i].resource.image_id),
                                 multiply_matrix(camera, _unitModels[i]),
                                 {{1, 1, 1, 1}},
                                 true,
                                 {},
                                 false,
                                 false});
            }
        }
    }
    _renderer->encode(draws);
    for (const auto &item : tacticalFrames)
        _m3gRenderer->encode_frame({item.first}, item.second);
    if (!support && _panelPurpose == NativePanel::none && _preparedWorldToken &&
        _session->world_presentation() &&
        _session->world_presentation()->token == *_preparedWorldToken) {
        bool rosterReady = true;
        for (std::size_t i = 0; i < _scene->sprites.size(); ++i) {
            const auto index = _scene->sprites[i].unit_index;
            if (_session->units().at(index).active &&
                (!_animations[i] || !_renderedActive[i] ||
                 _playbackSpawnGenerations[i] != _session->unit_spawn_generation(index)))
                rosterReady = false;
        }
        // Root also checks matching visible UI receipt after drawing the HUD.
        if (rosterReady)
            frame.world = *_preparedWorldToken;
    }
    if (!support && _cameraPan && _cameraPan->reached) {
        if (_cameraPan->session_focus && _session->focus_request() &&
            _session->focus_request()->token == _cameraPan->token)
            frame.session_focus = _cameraPan->token;
        else if (!_cameraPan->session_focus && _session->mission().gate == MissionGate::camera &&
                 _session->mission().command_index == _cameraPan->token)
            frame.mission_camera = static_cast<std::size_t>(_cameraPan->token);
    }
    if (!support)
        for (const auto i : order) {
            const auto &pending = _pendingTransformCompletions[i];
            if (pending && _session->unit_transform_waiting(pending->unit_index) &&
                _session->unit_transform_generation(pending->unit_index) ==
                    pending->transform_generation &&
                _session->unit_spawn_generation(pending->unit_index) == pending->spawn_generation)
                frame.transforms.push_back(*pending);
        }
}
