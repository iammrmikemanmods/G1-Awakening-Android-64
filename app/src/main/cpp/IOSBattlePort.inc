// Platform translation of NativeApp.mm at iOS be203a911bc26313615ea450758413e5c9cf6462.
// Support/battle construction and acknowledgements preserve the original host
// gates. Android UI receipts are delivered only after a successful EGL swap.
void AndroidGame::Impl::prepareBattle() {
    if (!_session || !_session->battle())
        return;
    const auto &pending = *_session->battle();
    if (!_supportPresentation || _supportPresentation->status().token != pending.token) {
        if (pending.exchange.support_types.size() != pending.exchange.support_units.size() ||
            pending.units_before.size() != _session->units().size())
            throw FormatError("Pending battle has an inconsistent support snapshot");
        for (std::size_t i = 0; i < pending.exchange.support_units.size(); ++i) {
            const auto index = pending.exchange.support_units[i];
            if (index >= pending.units_before.size() || !pending.units_before[index].active ||
                _session->world().units.at(index).type != pending.exchange.support_types[i])
                throw FormatError("Support helper identity differs from its pre-combat unit");
        }
        if (!pending.exchange.support_units.empty() && !_supportAssets) {
            auto assets =
                std::make_unique<SupportOverlayAssets>(load_support_overlay_assets(*_archive));
            auto texture = _renderer->texture(assets->image);
            _supportAssets = std::move(assets);
            _supportTexture = std::move(texture);
        }
        auto support = std::make_unique<SupportPresentation>(
            pending.token, pending.exchange.support_units,
            _supportAssets ? _supportAssets->descriptor.animations().at(support_effect_animation)
                           : std::vector<AtlasStep>{});
        std::vector<GLESDraw> snapshots;
        if (!support->status().complete) {
            for (std::size_t i = 0; i < _scene->sprites.size(); ++i) {
                const auto &sprite = _scene->sprites[i];
                const auto &unit = pending.units_before.at(sprite.unit_index);
                if (!unit.active)
                    continue;
                std::size_t frame;
                if (_animations.at(i))
                    frame = _animations[i]->current_frame();
                else {
                    // Rebuild a new roster's pre-combat idle pose without
                    // altering resolved combat, gameplay or the shared RNG.
                    if (unit.form > 1 || unit.facing > 3)
                        throw FormatError("Invalid support sprite pose");
                    const auto type = _session->world().units.at(sprite.unit_index).type;
                    const auto animation = _animationTable.at(type).at(unit.form * 4 + unit.facing);
                    if (animation >= sprite.descriptor.animations().size())
                        throw FormatError("Missing pre-combat support sprite animation");
                    frame = sprite.descriptor.animations()[animation].front().frame_index;
                }
                snapshots.push_back({_spriteMeshes.at(i).at(frame),
                                     spriteTexture(sprite.resource.image_id),
                                     unit_model(_session->world(), unit),
                                     {{1, 1, 1, 1}},
                                     true,
                                     {},
                                     false,
                                     false});
            }
        }
        _supportSavedCursor = _returnCursor;
        _supportSavedTitle = _title;
        if (!_supportSavedCursor) {
            const auto selected_index = _session->selected().value_or(pending.attacker);
            const auto &selected = pending.units_before.at(selected_index);
            _supportSavedCursor = WorldCoordinate{selected.tile_x, selected.tile_y};
        }
        _supportUnitDraws = std::move(snapshots);
        _supportPresentation = std::move(support);
        _supportDisplay.invalidate();
        _range.reset();
        _cursor.reset();
        _attackRange.reset();
        _healRange.reset();
        _productionRange.reset();
        _moveMode = false;
        _attackMode = false;
        _healMode = false;
        _pendingDestination.reset();
        _productionMode.reset();
        _productionType.reset();
        _productionPlacement.reset();
        if (!_supportPresentation->status().complete) {
            ++_supportSequences;
            showSupportHelper();
            _clockStarted = false;
            _simulationClock.reset();
            ++_contentGeneration;
            requestSupportTutorial();
        }
    }
    if (!_supportPresentation->status().complete)
        return;
    if (_battlePresentation && _battlePresentation->status().token == pending.token)
        return;
    M3GBattleRequest request;
    request.token = pending.token;
    request.terrain_style = _session->world().style;
    request.attacker_before = pending.attacker_before;
    request.defender_before = pending.defender_before;
    request.exchange = pending.exchange;
    auto presentation = std::make_unique<M3GBattlePresentation>(*_archive, std::move(request));
    std::vector<std::shared_ptr<GLESModel>> models;
    models.reserve(presentation->assets().size());
    _battleEnhancedTextures = 0;
    for (std::size_t i = 0; i < presentation->assets().size(); ++i) {
        const auto &asset = presentation->assets()[i];
        const auto kind = i == 0  ? MaterialEnhancement::environment
                          : i < 3 ? MaterialEnhancement::character
                                  : MaterialEnhancement::effect;
        auto gpu = _graphicsModern ? _m3gRenderer->upload_modern(asset.render, kind)
                                   : _m3gRenderer->upload(asset.render);
        _battleEnhancedTextures += gpu->enhanced_texture_count();
        models.push_back(std::move(gpu));
    }
    // Publish only a complete asset/GPU set. Allocation failure leaves the
    // pending core battle unacknowledged for the host's visible error path.
    _battleModels = std::move(models);
    _battlePresentation = std::move(presentation);
    _supportCursor.reset();
    _supportUnitDraws.clear();
    _returnCursor = _supportSavedCursor;
    _title = _supportSavedTitle.empty() ? originalText(715) : _supportSavedTitle;
    _battlePresentedToken = 0;
    _uiState.battle.reset();
    for (const auto audio : _battlePresentation->take_audio_requests())
        dispatchAudio(audio);
    _clockStarted = false;
    _simulationClock.reset();
    ++_contentGeneration;
}

void AndroidGame::Impl::showSupportHelper() {
    if (!_supportPresentation || _supportPresentation->status().complete || !_session ||
        !_session->battle())
        return;
    const auto status = _supportPresentation->status();
    const auto index = status.helper_unit.value();
    const auto &unit = _session->battle()->units_before.at(index);
    const auto type = _session->world().units.at(index).type;
    const auto &definition = _session->world().unit_definitions.at(type);
    const auto primary = script_text_id(unit_text_index(definition.raw_text_indices.at(0)));
    _title = _text->at(primary) + " " + _text->at(support_text_id);
    // Retain the iOS host's immediate inspection-camera focus policy.
    _camera.center_x = (unit.tile_x + 0.5F) * 0.32F;
    _camera.center_z = (unit.tile_y + 0.5F) * 0.32F;
    TerrainMesh cursor;
    cursor.position_scale = _scene->terrain.position_scale;
    for (const auto &quad : _controlQuads)
        if (quad.tile_x == unit.tile_x && quad.tile_y == unit.tile_y)
            cursor.quads.push_back(quad);
    auto vertices = terrain_vertices(cursor);
    for (auto &vertex : vertices) {
        vertex.position[1] += 0.006F;
        vertex.uv = {{0, 0}};
    }
    _supportCursor = _renderer->mesh(vertices);
    _supportDisplay.invalidate();
    ++_contentGeneration;
}

void AndroidGame::Impl::requestSupportTutorial() {
    if (!_campaign || !_session || !_session->battle() || !_supportPresentation ||
        _supportPresentation->status().complete)
        return;
    const auto &pending = *_session->battle();
    SupportTutorialSlots owners{};
    if (pending.exchange.support_units.size() > owners.size())
        throw FormatError("Support tutorial helper list exceeds original six slots");
    for (std::size_t i = 0; i < pending.exchange.support_units.size(); ++i)
        owners[i] = _session->world().units.at(pending.exchange.support_units[i]).owner;
    const auto player = _session->current_player();
    const auto decision =
        support_tutorial_entry(_campaign->profile().support_tutorial_pending ? 1U : 0U, player,
                               _session->turn().controllers.at(player), owners);
    if (!decision.request_message)
        return;
    // The source host persists consumption atomically before showing the popup.
    const auto written = _campaign->consume_support_tutorial();
    if (!written.succeeded) {
        _supportTutorialRetryRequired = true;
        showPanel(NativePanel::tutorial_save_error, "Tutorial progress could not be saved",
                  written.error, ++_localPanelToken);
        addButton("Retry save", retry_tutorial);
        return;
    }
    _supportTutorialRetryRequired = false;
    _pendingMessageIds.push_back(support_tutorial_text_resource);
    ++_supportTutorialRequests;
    showNextMessage();
}

void AndroidGame::Impl::finishBattleFrame(const NativePresentedFrame::Battle &frame) {
    if (!_loaded || _failed || !_session || !_session->battle() || !_battlePresentation ||
        !frame.complete)
        return;
    const auto status = _battlePresentation->status();
    if (frame.token != _session->battle()->token || frame.token != status.token ||
        frame.revision != status.revision || !status.complete || !_pendingFrame ||
        _pendingFrame->ui.battle_token != frame.token ||
        _pendingFrame->ui.battle_revision != frame.revision)
        return;
    // Root calls this only after successful EGL swap/GPU completion and epoch
    // validation; the matching final battle HUD must have encoded in that frame.
    const auto battle = *_session->battle();
    _session->report_battle_finished(battle.token);
    _completedBattles.push_back(battle);
    if (_completedBattles.size() > 64)
        _completedBattles.erase(_completedBattles.begin());
    ++_battleAcknowledgements;
    _lastBattleFinalToken = frame.token;
    _lastBattleFinalRevision = frame.revision;
    dispatchAudio(MissionAudioRequest{2, 0});
    _battlePresentation.reset();
    _battleModels.clear();
    _battlePresentedToken = 0;
    _supportPresentation.reset();
    _supportUnitDraws.clear();
    _supportCursor.reset();
    _supportSavedCursor.reset();
    _supportDisplay.invalidate();
    _supportSavedTitle.clear();
    _checkpointRequested = true;
    _rangeDirty = true;
    _attackMode = false;
    _notice.clear();
    _clockStarted = false;
    _simulationClock.reset();
    ++_contentGeneration;
    refreshControls();
}

// Re-upload presentation-only support snapshots after EGL context replacement.
// The core battle, helper index, elapsed effect clock and tutorial/profile state
// remain unchanged. The replacement frame must be visibly presented again.
void AndroidGame::Impl::restoreSupportGraphics() {
    if (!_session || !_session->battle() || !_scene || !_supportPresentation ||
        _supportPresentation->status().complete)
        return;
    const auto &battle = *_session->battle();
    if (_supportPresentation->status().token != battle.token)
        throw FormatError("Restored support presentation belongs to another battle");
    std::vector<GLESDraw> snapshots;
    for (std::size_t i = 0; i < _scene->sprites.size(); ++i) {
        const auto &sprite = _scene->sprites[i];
        const auto &unit = battle.units_before.at(sprite.unit_index);
        if (!unit.active)
            continue;
        std::size_t frame;
        if (_animations.at(i))
            frame = _animations[i]->current_frame();
        else {
            if (unit.form > 1 || unit.facing > 3)
                throw FormatError("Invalid restored support sprite pose");
            const auto type = _session->world().units.at(sprite.unit_index).type;
            const auto animation = _animationTable.at(type).at(unit.form * 4 + unit.facing);
            if (animation >= sprite.descriptor.animations().size())
                throw FormatError("Missing restored support sprite animation");
            frame = sprite.descriptor.animations()[animation].front().frame_index;
        }
        snapshots.push_back({_spriteMeshes.at(i).at(frame),
                             spriteTexture(sprite.resource.image_id),
                             unit_model(_session->world(), unit),
                             {{1, 1, 1, 1}},
                             true,
                             {},
                             false,
                             false});
    }
    _supportUnitDraws = std::move(snapshots);
    showSupportHelper(); // Rebuild cursor and require this cue's new draw receipt.
}
