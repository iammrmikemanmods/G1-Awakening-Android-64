// Android host of the pinned iOS Build 017 portable engine. Gameplay, RNG,
// mission parsing and save codecs are compiled verbatim from upstream/ios.
#include "AndroidGame.hpp"
#include "AndroidUI.hpp"
#include "AndroidAudio.hpp"
#include "AssetStore.hpp"
#include "GLESModelRenderer.hpp"
#include "g1/CampaignController.hpp"
#include "g1/ContactShadow.hpp"
#include "g1/DamageAnchor.hpp"
#include "g1/GameSession.hpp"
#include "g1/SessionSave.hpp"
#include "g1/PresentationEpoch.hpp"
#include "g1/PresentationQuality.hpp"
#include "g1/TacticalModel.hpp"
#include "g1/TechSpecsModel.hpp"
#include "g1/InitialPresentation.hpp"
#include "g1/OriginalUI.hpp"
#include "g1/SpritePlacement.hpp"
#include "g1/SpriteAnimation.hpp"
#include "g1/SupportPresentation.hpp"
#include "g1/SupportDisplayGate.hpp"
#include "g1/SupportOverlayAssets.hpp"
#include "g1/SupportGeometry.hpp"
#include "g1/SupportTutorial.hpp"
#include "g1/AudioMaintenance.hpp"
#include "g1/MenuMusic.hpp"
#include "g1/TerrainDecorations.hpp"
#include "g1/TerrainSpriteDecorations.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <ctime>
#include <unistd.h>

namespace g1::android {
namespace {
struct Size {
    double width{}, height{};
};
struct Point {
    double x{}, y{};
};
struct Bounds {
    double x{}, y{}, width{}, height{};
    bool valid{};
    void add(double px, double py) {
        if (!valid) {
            x = px;
            y = py;
            width = height = 0.1;
            valid = true;
            return;
        }
        const auto right = std::max(x + width, px + 0.1), bottom = std::max(y + height, py + 0.1);
        x = std::min(x, px);
        y = std::min(y, py);
        width = right - x;
        height = bottom - y;
    }
    bool contains(Point p, double pad = 0) const {
        return valid && p.x >= x - pad && p.y >= y - pad && p.x <= x + width + pad &&
               p.y <= y + height + pad;
    }
};
double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
std::string jsonString(const std::string &value) {
    std::ostringstream s;
    s << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            s << '\\' << char(c);
        else if (c == '\n')
            s << "\\n";
        else if (c == '\r')
            s << "\\r";
        else if (c == '\t')
            s << "\\t";
        else if (c < 32)
            s << "?";
        else
            s << char(c);
    }
    s << '"';
    return s.str();
}
std::array<float, 4> transform(const Matrix4 &matrix, std::array<float, 4> point) {
    std::array<float, 4> r{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned c = 0; c < 4; ++c)
            r[row] += matrix[c * 4 + row] * point[c];
    return r;
}
struct InspectionCamera {
    float center_x{}, center_z{}, span{5}, base_span{5}, aspect{2};
    float up_y = .7071067811865476F, up_z = -.7071067811865476F;
    void set_modern(bool m) {
        up_y = m ? .81915204429F : .7071067811865476F;
        up_z = m ? -.57357643635F : -.7071067811865476F;
    }
    void fit(const MissionScene &scene, Size size) {
        if (size.width <= 0 || size.height <= 0)
            return;
        aspect = float(size.width / size.height);
        center_x = scene.world.grid.width * .16F;
        center_z = scene.world.grid.height * .16F;
        base_span = 1.15F * std::max(scene.world.grid.height * .32F * -up_z + .6F,
                                     (scene.world.grid.width * .32F + .8F) / aspect);
        span = base_span;
    }
    Matrix4 matrix() const {
        Matrix4 r{};
        r[0] = 2 / (span * aspect);
        r[5] = 2 * up_y / span;
        r[9] = 2 * up_z / span;
        r[6] = up_z / 100;
        r[10] = -up_y / 100;
        r[12] = -center_x * r[0];
        r[13] = -center_z * r[9];
        r[14] = .5F - center_z * r[10];
        r[15] = 1;
        return r;
    }
};
Matrix4 unit_model(const WorldBootstrap &world, const MovementUnitState &unit) {
    bool site = false, group = false;
    auto matches = [&](WorldCoordinate p) {
        return p.x < 128 && p.y < 128 && p.x == unit.tile_x && p.y == unit.tile_y;
    };
    for (const auto &s : world.sites) {
        site = site || matches(s.position);
        for (const auto &g : s.coordinate_groups)
            group = group || std::any_of(g.begin(), g.end(), matches);
    }
    return make_unit_sprite_placement(
               {unit.tile_x, unit.tile_y, unit.form, unit.offset_x, unit.offset_y, site, group})
        .model;
}
bool inside_triangle(Point p, Point a, Point b, Point c) {
    auto side = [](Point x, Point y, Point z) {
        return (x.x - z.x) * (y.y - z.y) - (y.x - z.x) * (x.y - z.y);
    };
    auto d1 = side(p, a, b), d2 = side(p, b, c), d3 = side(p, c, a);
    return !((d1 < -.01 || d2 < -.01 || d3 < -.01) && (d1 > .01 || d2 > .01 || d3 > .01));
}
struct NativeCameraPan {
    std::uint64_t token{};
    bool session_focus{};
    float from_x{}, from_z{}, to_x{}, to_z{};
    std::uint32_t elapsed{}, duration{360};
    bool reached{};
};
struct NativeTransformCompletion {
    std::size_t unit_index{};
    std::uint64_t transform_generation{}, spawn_generation{};
};
struct NativePresentedFrame {
    struct Battle {
        std::uint64_t token{}, revision{};
        bool complete{};
        std::size_t queued_draws{};
    };
    std::uint64_t scene_revision{}, content_generation{}, presentation_epoch{};
    std::optional<std::size_t> mission_camera;
    std::optional<std::uint64_t> session_focus, world;
    std::vector<NativeTransformCompletion> transforms;
    std::optional<Battle> battle;
    std::optional<SupportFrameReceipt> support;
    UiReceipt ui;
};
struct NativeWorldOverlay {
    std::shared_ptr<GLESMesh> mesh;
    std::array<float, 4> tint;
};
enum class NativePanel {
    none,
    startup,
    pause,
    results,
    ending,
    menu,
    mode_select,
    mission_select,
    settings,
    help,
    help_topic,
    credits,
    save_error,
    load_error,
    message,
    arena,
    arena_opponent,
    tutorial_save_error,
    tech_specs,
    production
};
enum MenuAction {
    profile = 100,
    modes,
    story,
    showdown,
    arena,
    mission,
    previous_page,
    next_page,
    arena_autobots,
    arena_decepticons,
    arena_computer,
    arena_human,
    continue_game,
    resume_game,
    settings,
    settings_back,
    graphics,
    music,
    sound,
    toggle_diagnostics,
    camera_speed,
    help,
    help_topic,
    credits,
    information_back,
    dismiss_message,
    results_continue,
    retry_campaign,
    retry_checkpoint,
    retry_profile,
    retry_tutorial,
    production_type,
    production_cancel,
    reset_view,
    save_and_menu,
    settings_refresh
};
} // namespace

struct AndroidGame::Impl {
    std::function<RgbaImage(const std::string &, float)> _rasterizer;
    std::string _assets, _saveRoot, _profilePath, _sessionSavePath, _notice, _title,
        _supportSavedTitle, _checkpointError, _campaignError;
    std::string _accessibilitySnapshot{"{\"nodes\":[]}"}, _accessibilityDelivered;
    std::shared_ptr<const Archive> _archive;
    Bytes _terrainTableBytes;
    TerrainTables _terrainTables;
    std::unique_ptr<TextTable> _text;
    UnitAnimationTable _animationTable;
    std::unique_ptr<GameSession> _session;
    std::unique_ptr<CampaignController> _campaign;
    std::unique_ptr<MissionScene> _scene;
    std::unique_ptr<AndroidAudio> _audio;
    std::unique_ptr<GLESRenderer> _renderer;
    std::unique_ptr<GLESModelRenderer> _m3gRenderer;
    std::unique_ptr<AndroidUI> _ui;
    std::map<std::uint32_t, GLTexturePtr> _textures, _modernTextures;
    GLTexturePtr _white, _supportTexture, _contactShadowTexture;
    std::shared_ptr<GLESMesh> _terrain, _animatedTerrain, _decorationTerrain, _range, _cursor,
        _attackRange, _healRange, _productionRange, _supportCursor, _contactShadowMesh;
    std::vector<NativeWorldOverlay> _worldOverlays;
    std::vector<TerrainQuad> _controlQuads;
    std::vector<std::int32_t> _worldOverlaySignature;
    std::map<std::uint32_t, std::vector<std::shared_ptr<GLESMesh>>> _terrainSpriteMeshes;
    std::vector<TerrainSpriteDecorationDraw> _terrainSpriteDraws;
    std::vector<std::vector<std::vector<SceneVertex>>> _spriteVertices;
    std::vector<std::vector<std::shared_ptr<GLESMesh>>> _spriteMeshes;
    std::vector<std::optional<SpriteAnimation>> _animations;
    std::vector<std::int32_t> _animationIds;
    std::vector<bool> _renderedActive;
    std::vector<std::uint64_t> _playbackSpawnGenerations, _playbackTransformGenerations;
    std::vector<std::optional<NativeTransformCompletion>> _pendingTransformCompletions;
    std::vector<MovementUnitState> _previousUnits;
    std::vector<Matrix4> _unitModels;
    std::vector<std::pair<std::size_t, CompletionPresentation>> _pendingCompletionRequests;
    std::map<std::uint8_t, std::unique_ptr<TacticalModel>> _tacticalModels;
    std::map<std::uint8_t, std::shared_ptr<GLESModel>> _tacticalGpu;
    std::map<std::uint8_t, std::string> _tacticalErrors;
    std::map<std::size_t, Bounds> _tacticalHitBounds;
    std::unique_ptr<M3GBattlePresentation> _battlePresentation;
    std::vector<std::shared_ptr<GLESModel>> _battleModels;
    std::unique_ptr<SupportOverlayAssets> _supportAssets;
    std::unique_ptr<SupportPresentation> _supportPresentation;
    std::vector<GLESDraw> _supportUnitDraws;
    SupportDisplayGate _supportDisplay;
    std::optional<WorldCoordinate> _supportSavedCursor, _returnCursor, _pendingDestination;
    std::optional<ProductionMode> _productionMode;
    std::optional<std::uint8_t> _productionType;
    std::optional<ProductionPlacement> _productionPlacement;
    std::optional<MovementRange> _previewRange;
    std::vector<std::size_t> _attackTargets, _healTargets;
    std::optional<std::size_t> _lastSelection;
    std::optional<std::uint64_t> _preparedWorldToken;
    std::optional<MissionAudioRequest> _lastMapMusic;
    std::optional<NativeCameraPan> _cameraPan;
    std::vector<std::uint32_t> _pendingMessageIds, _presentedMessageIds;
    Bytes _lastCheckpointBytes;
    std::vector<SessionBattle> _completedBattles;
    SimulationClock _simulationClock;
    InspectionCamera _camera;
    PresentationEpoch _presentationEpoch;
    NativePanel _panelPurpose{NativePanel::none}, _informationReturnPanel{NativePanel::startup};
    UiState _uiState;
    std::optional<NativePresentedFrame> _pendingFrame;
    std::optional<DialogueLayout> _dialogueLayout;
    std::int32_t _dialoguePresentedFirstLine{};
    std::uint64_t _drawnPanelToken{}, _dialoguePresentedIdentity{}, _panelToken{},
        _localPanelToken{}, _sceneRevision{1}, _sessionInstalls{}, _contentGeneration{};
    std::uint64_t _renderedTerrainGeneration{}, _renderedTerrainWorld{}, _battlePresentedToken{},
        _battleAcknowledgements{}, _lastBattleFinalToken{}, _lastBattleFinalRevision{};
    std::uint64_t _spawnPlaybackResets{}, _sceneRebuilds{}, _simulationTicks{}, _supportTicks{},
        _supportSequences{}, _supportTutorialRequests{}, _battlePresentationTicks{};
    std::uint64_t _checkpointWrites{}, _checkpointLoads{}, _presentedFrames{}, _moveActions{},
        _captureActions{}, _transformActions{}, _attackActions{}, _healActions{}, _upgradeActions{},
        _endTurnActions{}, _undoActions{}, _productionActions{}, _elapsedNanosRemainder{};
    std::size_t _checkpointCommand{std::numeric_limits<std::size_t>::max()},
        _dialogueCommand{std::numeric_limits<std::size_t>::max()},
        _inputCommand{std::numeric_limits<std::size_t>::max()};
    std::size_t _tacticalDrawn{}, _tacticalFallback{}, _battleEnhancedTextures{};
    std::uint32_t _inputEdges{}, _resultsInputEdges{}, _randomSeed{};
    std::int32_t _missionPickerPage{}, _pendingArenaIndex{};
    std::uint8_t _missionPickerGroup{}, _pendingArenaFaction{}, _lastMissionMode{255};
    bool _loaded{}, _failed{}, _graphicsModern{}, _showDiagnostics{}, _settingsFromPause{},
        _renderMemoryPressure{}, _renderLowPower{}, _renderThermalSerious{}, _foreground{true},
        _audioFocus{true}, _clockStarted{};
    bool _moveMode{}, _attackMode{}, _healMode{}, _rangeDirty{true}, _typeListOpen{},
        _checkpointRequested{}, _checkpointRetryRequired{}, _checkpointRetired{},
        _restoredCheckpoint{}, _campaignRetryRequired{}, _supportTutorialRetryRequired{};
    float _touchCameraSpeed{1};
    double _noticeUntil{};
    std::string _failure;
    int _width{1}, _height{1};
    adaptive::Insets _insets{};
    std::vector<std::size_t> _techSpecsRoster;
    std::size_t _techSpecsPosition{};
    std::uint8_t _techSpecsPreviewForm{};
    bool _techSpecsFromPause{};
    float _specsAngle{};
    std::unique_ptr<TechSpecsModel> _specsModel;
    std::shared_ptr<GLESModel> _specsGpu;
    GLuint _fbo{}, _color{}, _depth{};
    int _renderWidth{}, _renderHeight{};
    quality::Plan _renderQuality{};
    std::uint64_t _renderAllocationFallbacks{};

    Impl(std::string assets, std::string saves, bool modern);
    void attach();
    void detach(bool lost);
    void resize(int, int, adaptive::Insets);
    void draw(std::uint64_t);
    void presented(bool);
    void action(UiAction);
    void touch(int, float, float, float, float, float);
    void back();
    void pause();
    void resume();
    void showPanel(NativePanel, const std::string &, const std::string &, std::uint64_t = 0,
                   bool info = false);
    void addButton(const std::string &, int, int = 0, bool = true);
    void hidePanel();
    std::string originalText(std::uint16_t id) const {
        return _text->at(id);
    }
    std::string messageText(std::uint32_t) const;
    void showProfileMenu();
    void showModesMenu();
    void showMissionPicker();
    void chooseCampaign(std::uint8_t);
    void arenaFaction(int);
    void arenaOpponent(int, std::uint8_t);
    void startCampaign(std::uint8_t, int, std::uint8_t = 0, std::int8_t = 1);
    void installSession(std::unique_ptr<GameSession>);
    void continueCheckpoint();
    bool retireCheckpoint();
    void saveCheckpoint();
    void showPause();
    void showSettings();
    void showHelp();
    void showCredits();
    void saveSettings();
    void showNextMessage();
    void dismissMessage();
    void showResults();
    void updateCampaign(std::uint32_t);
    void retryCampaignSave();
    void rebuildScene(bool);
    void synchronizeScene();
    void refreshWorldOverlays();
    void refreshTerrainDecorations();
    void refreshRange();
    void prepareWorldPresentation();
    void advanceCamera(std::uint32_t);
    void advanceSprites(std::uint32_t);
    void simulate(std::uint32_t);
    void prepareBattle();
    void showSupportHelper();
    void requestSupportTutorial();
    void finishBattleFrame(const NativePresentedFrame::Battle &);
    void restoreSupportGraphics();
    void refreshControls();
    void dispatchAudio(MissionAudioRequest r) {
        if (_audio)
            _audio->request(r);
    }
    void maintainMusicAfterStep(MusicMaintenanceSite);
    void maintainMenuMusic();
    GLTexturePtr spriteTexture(std::uint32_t id) {
        if (_graphicsModern && _modernTextures.count(id))
            return _modernTextures.at(id);
        return _textures.at(id);
    }
    void primeModernTextures();
    Bounds unitBounds(std::size_t) const;
    std::optional<std::size_t> unitAtPoint(Point, double) const;
    void tap(Point);
    bool productionTileAllowed(WorldCoordinate);
    void showProduction(ProductionMode);
    void openTechSpecs();
    void renderCurrentTechSpecs();
    void drawTechSpecs(adaptive::Rect);
    void renderScene(NativePresentedFrame &, Size);
    void renderTactical(NativePresentedFrame &, Size, bool);
    void beginTarget();
    void resolveTarget();
    void releaseTarget();
    void notice(const std::string &text) {
        _notice = text;
        _noticeUntil = nowSeconds() + 4;
    }
    std::string report() const;
    void captureAccessibility();
};

AndroidGame::Impl::Impl(std::string assets, std::string saves, bool modern)
    : _assets(std::move(assets)), _saveRoot(std::move(saves)), _graphicsModern(modern) {
    std::filesystem::create_directories(_saveRoot);
    _profilePath = _saveRoot + "/campaign.g1native";
    _sessionSavePath = _saveRoot + "/session.g1native";
    auto bytes = read_file(_assets + "/GameData/iphone.big");
    verifyAsset(bytes, "663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4");
    _archive = std::make_shared<const Archive>(std::move(bytes));
    _terrainTableBytes = read_file(_assets + "/GameData/terrain-tables.bin");
    verifyAsset(_terrainTableBytes,
                "b870e3d74244741003c8068178e7f58bb5fbe35599599c918e024227463725a8");
    _terrainTables = decode_terrain_tables(_terrainTableBytes);
    _text = std::make_unique<TextTable>(load_text_table(*_archive));
    const auto found = _archive->find(309, 0xf4e02223U);
    if (found.size() != 1)
        throw FormatError("Unit animation table missing");
    _animationTable = decode_unit_animation_table(_archive->read(found.front()));
    bool musicOn = true, soundOn = true;
    std::ifstream prefs(_saveRoot + "/settings.txt");
    int version = 0;
    if (prefs >> version && version == 1) {
        int m, s, g, d;
        float speed;
        if (prefs >> m >> s >> g >> d >> speed) {
            musicOn = m != 0;
            soundOn = s != 0;
            _graphicsModern = g != 0;
            _showDiagnostics = d != 0;
            if (std::isfinite(speed))
                _touchCameraSpeed = std::clamp(speed, .75F, 1.5F);
        }
    }
    _audio = std::make_unique<AndroidAudio>(*_archive);
    _audio->configure(musicOn, soundOn);
    for (int i = 0; i < 3; ++i)
        dispatchAudio({0, i});
    try {
        _campaign = std::make_unique<CampaignController>(_profilePath);
        showProfileMenu();
    } catch (const std::exception &e) {
        showPanel(NativePanel::load_error, "Campaign profile could not be loaded", e.what());
        addButton("Retry", retry_profile);
    }
}

void AndroidGame::Impl::showPanel(NativePanel purpose, const std::string &title,
                                  const std::string &body, std::uint64_t token, bool info) {
    _accessibilitySnapshot = "{\"nodes\":[]}";
    _supportDisplay.invalidate();
    _presentationEpoch.invalidate();
    _panelPurpose = purpose;
    _panelToken = token ? token : ++_localPanelToken;
    _drawnPanelToken = 0;
    _inputEdges = 0;
    _resultsInputEdges = 0;
    _simulationClock.reset();
    if (_session)
        _previousUnits = _session->units();
    const bool sourceMenu = purpose == NativePanel::startup || purpose == NativePanel::menu ||
                            purpose == NativePanel::mode_select ||
                            purpose == NativePanel::settings || purpose == NativePanel::help ||
                            purpose == NativePanel::help_topic || purpose == NativePanel::credits ||
                            purpose == NativePanel::pause ||
                            purpose == NativePanel::mission_select ||
                            purpose == NativePanel::arena || purpose == NativePanel::arena_opponent;
    _uiState = {};
    _uiState.tactical.visible = false;
    _uiState.show_menu = false;
    _uiState.panel =
        UiPanel{title, body, _panelToken, sourceMenu, info, purpose == NativePanel::pause, 0, {}};
    _uiState.panel->action_list = purpose == NativePanel::production;
}
void AndroidGame::Impl::addButton(const std::string &label, int id, int value, bool enabled) {
    if (!_uiState.panel)
        throw FormatError("Menu button requires an actual panel");
    _uiState.panel->buttons.push_back({label, {id, value}, enabled});
}
void AndroidGame::Impl::hidePanel() {
    _accessibilitySnapshot = "{\"nodes\":[]}";
    _supportDisplay.invalidate();
    _presentationEpoch.invalidate();
    _panelPurpose = NativePanel::none;
    _panelToken = 0;
    _uiState = {};
    _drawnPanelToken = 0;
    _inputEdges = 0;
    _resultsInputEdges = 0;
    _clockStarted = false;
    _elapsedNanosRemainder = 0;
    _simulationClock.reset();
    _dialoguePresentedIdentity = 0;
    if (_session)
        _previousUnits = _session->units();
}
void AndroidGame::Impl::showProfileMenu() {
    if (!_campaign)
        return;
    const auto &p = _campaign->profile();
    const bool requested = _campaign->state().gate == CampaignControllerGate::campaign_menu;
    showPanel(requested ? NativePanel::menu : NativePanel::startup, originalText(1169),
              originalText(617) + " " + std::to_string(p.highest_unlocked[0] + 1) + " / " +
                  std::to_string(campaign_mission_count(0)),
              requested ? _campaign->state().token : 0);
    if (std::filesystem::exists(_sessionSavePath))
        addButton(originalText(771), continue_game);
    addButton(originalText(471), modes);
    addButton(originalText(468), settings);
    addButton(originalText(754), help);
    addButton("CREDITS", credits);
    if (_session && _campaign->state().gate == CampaignControllerGate::idle)
        addButton(originalText(1205), resume_game);
}
void AndroidGame::Impl::showModesMenu() {
    if (!_campaign)
        return;
    auto &p = _campaign->profile();
    showPanel(NativePanel::mode_select, originalText(983), "");
    addButton(originalText(617), story);
    addButton(originalText(1151), showdown, 0, p.completed[0]);
    addButton(originalText(1190), arena, 0, p.completed[1]);
    addButton("Back", profile);
}
void AndroidGame::Impl::chooseCampaign(std::uint8_t group) {
    _missionPickerGroup = group;
    _missionPickerPage = 0;
    showMissionPicker();
}
void AndroidGame::Impl::showMissionPicker() {
    if (!_campaign || _missionPickerGroup > 2)
        return;
    const int group = _missionPickerGroup, count = int(campaign_mission_count(group));
    const int maximum = group == 2
                            ? count - 1
                            : std::min(count - 1, _campaign->profile().highest_unlocked.at(group));
    const int start = _missionPickerPage * original_ui::mission_page_size;
    if (start < 0 || start > maximum)
        return;
    showPanel(NativePanel::mission_select,
              originalText(group == 0   ? 617
                           : group == 1 ? 1151
                                        : 1190),
              "");
    for (int i = start; i <= maximum && i < start + original_ui::mission_page_size; ++i)
        addButton(originalText(group == 2 ? 536 : 1242) + " " + std::to_string(i + 1), mission, i);
    if (_missionPickerPage > 0)
        addButton("Previous", previous_page);
    if (start + original_ui::mission_page_size <= maximum)
        addButton("Next", next_page);
    addButton("Back", modes);
}
void AndroidGame::Impl::arenaFaction(int i) {
    _pendingArenaIndex = i;
    showPanel(NativePanel::arena, originalText(1022), "");
    addButton(originalText(501), arena_autobots);
    addButton(originalText(1315), arena_decepticons);
    addButton("Back", mission, -1);
}
void AndroidGame::Impl::arenaOpponent(int i, std::uint8_t faction) {
    _pendingArenaIndex = i;
    _pendingArenaFaction = faction;
    showPanel(NativePanel::arena_opponent, originalText(1313), "");
    addButton(originalText(580), arena_computer);
    addButton(originalText(690), arena_human);
    addButton("Back", arena, i);
}
void AndroidGame::Impl::startCampaign(std::uint8_t group, int index, std::uint8_t faction,
                                      std::int8_t opponent) {
    try {
        if (!_campaign || group > 2 || index < 0 ||
            std::size_t(index) >= campaign_mission_count(group) ||
            (group < 2 && index > _campaign->profile().highest_unlocked[group]) ||
            (group == 1 && !_campaign->profile().completed[0]) ||
            (group == 2 && !_campaign->profile().completed[1]) || faction > 1 || opponent < 0 ||
            opponent > 1 || (group < 2 && (faction != 0 || opponent != 1)))
            throw FormatError("Mission is not unlocked in the saved profile");
        CampaignContext context;
        context.group = group;
        context.mission_index = index;
        context.player_factions = {{faction, std::uint8_t(1 - faction)}};
        context.player_controllers = {{0, opponent}};
        _randomSeed = std::uint32_t(std::time(nullptr));
        auto bootstrap =
            bootstrap_campaign(*_archive, context, GameRandom(_randomSeed), false, &_terrainTables);
        auto session = std::make_unique<GameSession>(*_archive, std::move(bootstrap), context);
        auto campaign = std::make_unique<CampaignController>(_profilePath);
        installSession(std::move(session));
        if (!retireCheckpoint())
            throw std::runtime_error(_checkpointError);
        _campaign = std::move(campaign);
        _checkpointRequested = true;
        hidePanel();
        refreshControls();
    } catch (const std::exception &e) {
        showPanel(NativePanel::load_error, "Mission could not be started", e.what());
        addButton(originalText(883), profile);
    }
}
void AndroidGame::Impl::saveCheckpoint() {
    if (!_checkpointRequested || _checkpointRetryRequired || !_session ||
        !_session->checkpoint_available())
        return;
    try {
        auto bytes = encode_session_checkpoint(_session->make_checkpoint());
        if (bytes != _lastCheckpointBytes || !std::filesystem::exists(_sessionSavePath)) {
            auto written = write_native_save_atomic(_sessionSavePath, bytes);
            if (!written.succeeded)
                throw std::runtime_error(written.error);
            _lastCheckpointBytes = std::move(bytes);
            ++_checkpointWrites;
        }
        _checkpointCommand = _session->mission().command_index;
        _checkpointRequested = false;
        _checkpointRetired = false;
        _checkpointError.clear();
    } catch (const std::exception &e) {
        _checkpointError = e.what();
        _checkpointRetryRequired = true;
        notice("Checkpoint save failed. Open Menu to retry.");
    }
}
bool AndroidGame::Impl::retireCheckpoint() {
    if (!std::filesystem::exists(_sessionSavePath)) {
        _lastCheckpointBytes.clear();
        _checkpointRetired = true;
        return true;
    }
    std::error_code ec;
    std::filesystem::rename(
        _sessionSavePath,
        _sessionSavePath + "." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            ".retired",
        ec);
    if (ec) {
        _checkpointError = ec.message();
        return false;
    }
    _lastCheckpointBytes.clear();
    _checkpointRetired = true;
    return true;
}
void AndroidGame::Impl::continueCheckpoint() {
    try {
        auto bytes = read_file(_sessionSavePath, max_session_save_size);
        auto saved = decode_session_checkpoint(*_archive, bytes, &_terrainTables);
        auto session = GameSession::restore_checkpoint(*_archive, saved);
        auto campaign = std::make_unique<CampaignController>(_profilePath);
        installSession(std::move(session));
        _campaign = std::move(campaign);
        _lastCheckpointBytes = std::move(bytes);
        _checkpointRetryRequired = false;
        _restoredCheckpoint = true;
        ++_checkpointLoads;
        hidePanel();
        refreshControls();
    } catch (const std::exception &e) {
        showPanel(NativePanel::load_error, "Checkpoint could not be loaded",
                  std::string(e.what()) + "\n\nThe saved file has been kept.");
        addButton("Retry", continue_game);
        addButton(originalText(883), profile);
    }
}

void AndroidGame::Impl::installSession(std::unique_ptr<GameSession> session) {
    if (!session)
        throw FormatError("Cannot install an empty game session");
    _loaded = false;
    _presentationEpoch.invalidate();
    ++_sceneRevision;
    ++_sessionInstalls;
    _session = std::move(session);
    _scene.reset();
    _cameraPan.reset();
    _preparedWorldToken.reset();
    _worldOverlays.clear();
    _worldOverlaySignature.clear();
    _animatedTerrain.reset();
    _decorationTerrain.reset();
    _terrainSpriteDraws.clear();
    _renderedTerrainGeneration = _renderedTerrainWorld = 0;
    _animations.clear();
    _animationIds.clear();
    _renderedActive.clear();
    _playbackSpawnGenerations.clear();
    _playbackTransformGenerations.clear();
    _pendingTransformCompletions.clear();
    _spriteVertices.clear();
    _spriteMeshes.clear();
    _unitModels.clear();
    _tacticalModels.clear();
    _tacticalGpu.clear();
    _tacticalErrors.clear();
    _tacticalHitBounds.clear();
    _inputEdges = 0;
    _inputCommand = _dialogueCommand = std::numeric_limits<std::size_t>::max();
    _dialogueLayout.reset();
    _dialoguePresentedIdentity = 0;
    _lastMissionMode = 255;
    _lastSelection.reset();
    _pendingCompletionRequests.clear();
    _completedBattles.clear();
    _battlePresentation.reset();
    _battleModels.clear();
    _battlePresentedToken = 0;
    _supportPresentation.reset();
    _supportUnitDraws.clear();
    _supportCursor.reset();
    _supportSavedCursor.reset();
    _supportDisplay.invalidate();
    _supportSavedTitle.clear();
    _supportTutorialRetryRequired = false;
    _presentedMessageIds.clear();
    _notice.clear();
    _checkpointCommand = std::numeric_limits<std::size_t>::max();
    _checkpointRequested = true;
    _checkpointRetryRequired = false;
    _pendingMessageIds.clear();
    _campaignRetryRequired = false;
    _healMode = false;
    _healTargets.clear();
    _healRange.reset();
    _productionMode.reset();
    _productionType.reset();
    _productionPlacement.reset();
    _returnCursor.reset();
    _productionRange.reset();
    _typeListOpen = false;
    _controlQuads.clear();
    _techSpecsRoster.clear();
    _specsModel.reset();
    _specsGpu.reset();
    _simulationClock.reset();
    rebuildScene(true);
    if (_session->world().camera) {
        _camera.center_x = (_session->world().camera->x + .5F) * .32F;
        _camera.center_z = (_session->world().camera->y + .5F) * .32F;
    }
    _loaded = true;
    _failed = false;
    _failure.clear();
}
void AndroidGame::Impl::primeModernTextures() {
    if (!_renderer || !_scene)
        return;
    for (const auto &item : _scene->textures) {
        if (_modernTextures.count(item.first))
            continue;
        if (item.first == _scene->terrain.atlas_resource_id)
            _modernTextures[item.first] = _renderer->texture(cleanTerrain(item.second, _assets));
        else if (item.second.width == 512 && item.second.height == 512)
            _modernTextures[item.first] = _renderer->texture(cleanSprite(item.second, _assets));
    }
    _renderedTerrainGeneration = 0;
    refreshTerrainDecorations();
}
void AndroidGame::Impl::attach() {
    _renderer = std::make_unique<GLESRenderer>();
    _m3gRenderer = std::make_unique<GLESModelRenderer>();
    _m3gRenderer->set_clean_texture_loader(
        [this](const RgbaImage &image, const char *category, unsigned factor) {
            return cleanTexture(image, category, factor, _assets);
        });
    _ui = std::make_unique<AndroidUI>(*_renderer);
    _ui->initialize(*_archive, _assets);
    _ui->setSystemTextRasterizer(_rasterizer);
    _ui->resize(_width, _height, _insets);
    _white = _renderer->texture({1, 1, {255, 255, 255, 255}});
    _contactShadowTexture = _renderer->texture(soft_contact_shadow());
    _contactShadowMesh = _renderer->mesh(contact_shadow_vertices());
    if (_session) {
        auto animations = _animations;
        auto ids = _animationIds;
        auto active = _renderedActive;
        auto spawns = _playbackSpawnGenerations;
        auto transforms = _playbackTransformGenerations;
        auto pending = _pendingTransformCompletions;
        const auto camera = _camera;
        _scene.reset();
        rebuildScene(false);
        if (animations.size() == _animations.size()) {
            _animations = std::move(animations);
            _animationIds = std::move(ids);
            _renderedActive = std::move(active);
            _playbackSpawnGenerations = std::move(spawns);
            _playbackTransformGenerations = std::move(transforms);
            _pendingTransformCompletions = std::move(pending);
        }
        _camera = camera;
        _loaded = true;
        if (_supportAssets)
            _supportTexture = _renderer->texture(_supportAssets->image);
        restoreSupportGraphics();
        if (_battlePresentation) {
            for (std::size_t i = 0; i < _battlePresentation->assets().size(); ++i) {
                const auto &a = _battlePresentation->assets()[i];
                auto k = i == 0  ? MaterialEnhancement::environment
                         : i < 3 ? MaterialEnhancement::character
                                 : MaterialEnhancement::effect;
                _battleModels.push_back(_graphicsModern ? _m3gRenderer->upload_modern(a.render, k)
                                                        : _m3gRenderer->upload(a.render));
            }
        }
        if (_panelPurpose == NativePanel::tech_specs)
            renderCurrentTechSpecs();
    }
    _presentationEpoch.invalidate();
    _simulationClock.reset();
    _clockStarted = false;
    _elapsedNanosRemainder = 0;
}
void AndroidGame::Impl::detach(bool lost) {
    _accessibilitySnapshot = "{\"nodes\":[]}";
    _presentationEpoch.invalidate();
    _pendingFrame.reset();
    if (lost) {
        if (_renderer)
            _renderer->abandon_context();
        if (_m3gRenderer)
            _m3gRenderer->abandon_context();
        _fbo = _color = _depth = 0;
    } else
        releaseTarget();
    _ui.reset();
    _textures.clear();
    _modernTextures.clear();
    _white.reset();
    _supportTexture.reset();
    _contactShadowTexture.reset();
    _contactShadowMesh.reset();
    _terrain.reset();
    _animatedTerrain.reset();
    _decorationTerrain.reset();
    _range.reset();
    _cursor.reset();
    _attackRange.reset();
    _healRange.reset();
    _productionRange.reset();
    _supportCursor.reset();
    _worldOverlays.clear();
    _worldOverlaySignature.clear();
    _terrainSpriteMeshes.clear();
    _spriteMeshes.clear();
    _tacticalGpu.clear();
    _tacticalModels.clear();
    _battleModels.clear();
    _specsGpu.reset();
    _supportUnitDraws.clear();
    _m3gRenderer.reset();
    _renderer.reset();
    _renderedTerrainGeneration = 0;
}
void AndroidGame::Impl::resize(int w, int h, adaptive::Insets insets) {
    if (w <= 0 || h <= 0)
        return;
    _accessibilitySnapshot = "{\"nodes\":[]}";
    _width = w;
    _height = h;
    _insets = insets;
    _camera.aspect = float(w) / h;
    if (_ui)
        _ui->resize(w, h, insets);
    _presentationEpoch.invalidate();
    _dialoguePresentedIdentity = 0;
    _simulationClock.reset();
}
void AndroidGame::Impl::saveSettings() {
    std::ostringstream s;
    s << "1 " << _audio->musicEnabled() << ' ' << _audio->soundEnabled() << ' ' << _graphicsModern
      << ' ' << _showDiagnostics << ' ' << _touchCameraSpeed << '\n';
    auto str = s.str();
    auto result =
        write_native_save_atomic(_saveRoot + "/settings.txt", Bytes(str.begin(), str.end()));
    if (!result.succeeded)
        notice("Settings could not be saved: " + result.error);
}
void AndroidGame::Impl::showPause() {
    const auto detail = !_checkpointError.empty() ? "Checkpoint: " + _checkpointError
                        : _lastCheckpointBytes.empty()
                            ? "A checkpoint will be saved at the next completed objective boundary."
                            : "Continue restores the last completed action at an active objective.";
    showPanel(NativePanel::pause, originalText(1162), detail);
    addButton(originalText(1205), resume_game);
    if (_checkpointRetryRequired)
        addButton("Retry save", retry_checkpoint);
    addButton(originalText(1169), save_and_menu);
    addButton("Settings", settings);
    addButton("Help", help);
    if (_session && !_session->world().units.empty())
        addButton("TECH SPECS", ui_tech_specs);
}
void AndroidGame::Impl::showSettings() {
    showPanel(
        NativePanel::settings, originalText(468),
        std::string(
            "Enhanced original UI is shared by both modes. Modern adds supersampling and soft "
            "contact shadows; power and thermal limits reduce render resolution safely.\n\n") +
            (_graphicsModern ? "MODERN" : "CLASSIC"));
    addButton(_graphicsModern ? "Graphics: Modern" : "Graphics: Classic", graphics);
    addButton(originalText(_audio->musicEnabled() ? 495 : 1222), music);
    addButton(originalText(_audio->soundEnabled() ? 855 : 923), sound);
    std::ostringstream label;
    label << "Camera: " << std::fixed << std::setprecision(2) << _touchCameraSpeed << 'x';
    addButton(label.str(), camera_speed);
    addButton("Back", settings_back);
}
void AndroidGame::Impl::showHelp() {
    showPanel(NativePanel::help, originalText(754), "");
    const std::array<std::uint16_t, 4> titles{{630, 1181, 983, 1200}};
    for (std::size_t i = 0; i < titles.size(); ++i)
        addButton(originalText(titles[i]), help_topic, int(i));
    addButton("Back", information_back);
}
void AndroidGame::Impl::showCredits() {
    showPanel(NativePanel::credits, "CREDITS",
              "TRANSFORMERS G1: AWAKENING\n\nNative ARM64 Android port by iammrmikeman, "
              "based on the working iOS port. "
              "Original game models, art, sound and campaign data are retained. Unofficial fan "
              "project. Transformers and the original game remain the property of their respective "
              "rights holders. The original iOS opening cinematic plays before the G1 menu.",
              0, true);
    addButton("Back", information_back);
}

void AndroidGame::Impl::refreshControls() {
    if (!_session || _failed || _panelPurpose != NativePanel::none)
        return;
    if (_lastMissionMode != _session->mission().interaction_mode ||
        _lastSelection != _session->selected()) {
        _lastMissionMode = _session->mission().interaction_mode;
        _lastSelection = _session->selected();
        _moveMode = _attackMode = _healMode = false;
        _productionMode.reset();
        _productionType.reset();
        _productionPlacement.reset();
        _pendingDestination.reset();
        _rangeDirty = true;
    }
    _uiState = {};
    _uiState.tactical.visible = false;
    if (_session->battle()) {
        prepareBattle();
        if (_panelPurpose != NativePanel::none)
            return;
        if (_supportPresentation && !_supportPresentation->status().complete) {
            _uiState.support_banner = _title;
            _uiState.show_menu = false;
            return;
        }
        const auto status = _battlePresentation->status();
        auto name = [&](std::size_t side) {
            auto &d = _session->world().unit_definitions.at(status.types[side]);
            return _text->at(script_text_id(unit_text_index(d.raw_text_indices[0])));
        };
        _uiState.battle = UiBattle{status,
                                   name(0),
                                   name(1),
                                   _session->world().unit_definitions.at(status.types[0]).faction(),
                                   _session->world().unit_definitions.at(status.types[1]).faction(),
                                   {}};
        _uiState.show_menu = false;
        return;
    }
    refreshRange();
    const auto &state = _session->mission();
    if (state.gate == MissionGate::dialogue && state.dialogue) {
        if (_dialogueCommand != state.command_index) {
            _dialogueCommand = state.command_index;
            _inputEdges = 0;
            _dialoguePresentedIdentity = 0;
        }
        _uiState.dialogue =
            UiDialogue{(_sessionInstalls << 32) | std::uint64_t(state.command_index + 1),
                       _text->at(state.dialogue->heading_text_id),
                       _text->at(state.dialogue->text_id), state.dialogue->progress};
        return;
    }
    auto &hud = _uiState.tactical;
    hud.visible = true;
    hud.commands = state.interaction_mode == 0 && !_session->moving() &&
                   !_session->focus_request() && !_session->world_presentation() &&
                   _session->turn().controllers[_session->current_player()] != 1 &&
                   state.stop == MissionStop::none;
    hud.tech_specs = hud.commands && _session->selected().has_value();
    hud.funds = _session->turn().funds[_session->current_player()];
    hud.move = _previewRange && _previewRange->has_unoccupied_destination && !_session->moving();
    hud.capture = _session->selected_can_capture();
    hud.transform = _session->selected_transform_eligibility() == TransformEligibility::allowed;
    hud.attack = !_attackTargets.empty();
    hud.heal = !_healTargets.empty();
    hud.upgrade = _session->selected_upgrade_eligibility() == UpgradeEligibility::allowed;
    hud.end_turn = _session->end_turn_available();
    hud.build = _session->selected_production_eligibility(ProductionMode::ordinary) ==
                ProductionEligibility::allowed;
    hud.deploy = _session->selected_production_eligibility(ProductionMode::deploy) ==
                 ProductionEligibility::allowed;
    hud.undo = _session->selected_can_undo();
    if (_session->selected()) {
        const auto index = *_session->selected();
        auto &u = _session->world().units.at(index);
        auto &d = _session->world().unit_definitions.at(u.type);
        auto &combat = _session->combat_units().at(index);
        hud.shows_unit = true;
        hud.upgrade_cost = unit_spawn_cost(d);
        hud.name = _text->at(script_text_id(unit_text_index(d.raw_text_indices[0])));
        hud.health = combat.health;
        hud.attack_low = combat.stats.attack_low;
        hud.attack_high = combat.stats.attack_high;
        hud.defense = combat.stats.defense;
        hud.speed = _session->world().movement_profile(index).base_budget;
        hud.vehicle_form = u.form != 0;
    } else {
        hud.name = _session->world().title_text_index
                       ? _text->at(script_text_id(*_session->world().title_text_index))
                       : "G1 AWAKENING";
        hud.objective = state.objective ? _text->at(state.objective->text_id) : "";
    }
    if (!_notice.empty() && nowSeconds() < _noticeUntil)
        _uiState.notice = _notice;
    if (state.stop != MissionStop::none && state.stop != MissionStop::campaign_handoff_required)
        _uiState.notice = "The iOS engine stopped this mission. See diagnostics.";
    if (_showDiagnostics)
        _uiState.diagnostics = std::to_string(_presentedFrames) + " frames / " +
                               std::to_string(_simulationTicks) + " simulation ticks";
}

void AndroidGame::Impl::action(UiAction a) {
    if (!a || _failed)
        return;
    if (a.id >= 100 && _uiState.panel) {
        bool allowed = false;
        for (const auto &b : _uiState.panel->buttons)
            if (b.enabled && b.action.id == a.id && b.action.value == a.value)
                allowed = true;
        if (!allowed)
            return;
    }
    switch (a.id) {
    case profile:
        showProfileMenu();
        return;
    case modes:
        showModesMenu();
        return;
    case story:
        chooseCampaign(0);
        return;
    case showdown:
        if (_campaign && _campaign->profile().completed[0])
            chooseCampaign(1);
        return;
    case arena:
        if (_panelPurpose == NativePanel::arena_opponent)
            arenaFaction(_pendingArenaIndex);
        else if (_campaign && _campaign->profile().completed[1])
            chooseCampaign(2);
        return;
    case mission:
        if (a.value < 0) {
            showMissionPicker();
            return;
        }
        if (_panelPurpose != NativePanel::mission_select ||
            a.value / original_ui::mission_page_size != _missionPickerPage)
            return;
        if (_missionPickerGroup == 2)
            arenaFaction(a.value);
        else
            startCampaign(_missionPickerGroup, a.value);
        return;
    case previous_page:
        if (_missionPickerPage > 0) {
            --_missionPickerPage;
            showMissionPicker();
        }
        return;
    case next_page:
        ++_missionPickerPage;
        showMissionPicker();
        return;
    case arena_autobots:
        arenaOpponent(_pendingArenaIndex, 0);
        return;
    case arena_decepticons:
        arenaOpponent(_pendingArenaIndex, 1);
        return;
    case arena_computer:
        startCampaign(2, _pendingArenaIndex, _pendingArenaFaction, 1);
        return;
    case arena_human:
        startCampaign(2, _pendingArenaIndex, _pendingArenaFaction, 0);
        return;
    case continue_game:
        continueCheckpoint();
        return;
    case resume_game:
        if (_session) {
            hidePanel();
            refreshControls();
        }
        return;
    case settings:
        _settingsFromPause = _panelPurpose == NativePanel::pause;
        showSettings();
        return;
    case settings_back:
        if (_settingsFromPause)
            showPause();
        else
            showProfileMenu();
        return;
    case graphics: {
        const bool next = !_graphicsModern;
        if (next)
            try {
                primeModernTextures();
            } catch (const std::exception &e) {
                showPanel(NativePanel::settings, "Graphics could not be changed", e.what());
                _uiState.panel->details = true;
                addButton("Back", settings_refresh);
                return;
            }
        _graphicsModern = next;
        _camera.set_modern(next);
        _tacticalModels.clear();
        _tacticalGpu.clear();
        _tacticalHitBounds.clear();
        _tacticalErrors.clear();
        ++_contentGeneration;
        saveSettings();
        showSettings();
        return;
    }
    case settings_refresh:
        showSettings();
        return;
    case music:
        _audio->configure(!_audio->musicEnabled(), _audio->soundEnabled());
        if (_audio->musicEnabled() && _settingsFromPause && _lastMapMusic)
            dispatchAudio(*_lastMapMusic);
        saveSettings();
        showSettings();
        return;
    case sound:
        _audio->configure(_audio->musicEnabled(), !_audio->soundEnabled());
        saveSettings();
        showSettings();
        return;
    case toggle_diagnostics:
        _showDiagnostics = !_showDiagnostics;
        saveSettings();
        showSettings();
        return;
    case camera_speed:
        _touchCameraSpeed = _touchCameraSpeed < 1       ? 1
                            : _touchCameraSpeed < 1.25F ? 1.25F
                            : _touchCameraSpeed < 1.5F  ? 1.5F
                                                        : .75F;
        saveSettings();
        showSettings();
        return;
    case help:
        if (_panelPurpose != NativePanel::help_topic)
            _informationReturnPanel = _panelPurpose;
        showHelp();
        return;
    case help_topic: {
        if (a.value < 0 || a.value >= 4)
            return;
        const std::array<std::uint16_t, 4> titles{{630, 1181, 983, 1200}},
            bodies{{861, 458, 788, 393}};
        showPanel(NativePanel::help_topic, originalText(titles[a.value]),
                  originalText(bodies[a.value]), 0, true);
        addButton("Back", help);
        return;
    }
    case credits:
        _informationReturnPanel = _panelPurpose;
        showCredits();
        return;
    case information_back: {
        const auto destination = _informationReturnPanel;
        _informationReturnPanel = NativePanel::none;
        switch (destination) {
        case NativePanel::settings:
            showSettings();
            break;
        case NativePanel::pause:
            showPause();
            break;
        case NativePanel::mode_select:
            showModesMenu();
            break;
        case NativePanel::mission_select:
            showMissionPicker();
            break;
        case NativePanel::arena:
        case NativePanel::arena_opponent:
            arenaFaction(_pendingArenaIndex);
            break;
        default:
            showProfileMenu();
        }
        return;
    }
    case dismiss_message:
        dismissMessage();
        return;
    case results_continue:
        if (_campaign && _panelPurpose == NativePanel::results && _drawnPanelToken == _panelToken &&
            _campaign->state().gate == CampaignControllerGate::results &&
            _campaign->state().results->drawn)
            _resultsInputEdges |= 16;
        return;
    case retry_campaign:
        retryCampaignSave();
        return;
    case retry_profile:
        try {
            _campaign = std::make_unique<CampaignController>(_profilePath);
            showProfileMenu();
        } catch (const std::exception &e) {
            notice(e.what());
        }
        return;
    case retry_checkpoint:
        _checkpointRetryRequired = false;
        _checkpointRequested = true;
        saveCheckpoint();
        showPause();
        return;
    case retry_tutorial:
        _supportTutorialRetryRequired = false;
        hidePanel();
        requestSupportTutorial();
        refreshControls();
        return;
    case reset_view:
        if (_scene)
            _camera.fit(*_scene, {double(_width), double(_height)});
        hidePanel();
        refreshControls();
        return;
    case save_and_menu:
        _checkpointRequested = true;
        saveCheckpoint();
        showProfileMenu();
        return;
    case production_cancel:
        _productionMode.reset();
        _productionType.reset();
        _productionPlacement.reset();
        _typeListOpen = false;
        _pendingDestination.reset();
        hidePanel();
        _rangeDirty = true;
        refreshControls();
        return;
    case production_type: {
        if (!_session || !_productionMode || !_session->selected() || a.value < 0 || a.value > 255)
            return;
        const auto mode = *_productionMode;
        const auto choices = _session->selected_production_candidates(mode);
        if (std::find(choices.begin(), choices.end(), std::uint8_t(a.value)) == choices.end())
            return;
        hidePanel();
        _typeListOpen = false;
        _productionPlacement = _session->selected_production_placement(mode);
        if (!_productionPlacement)
            return;
        _productionMode = mode;
        _productionType = std::uint8_t(a.value);
        _pendingDestination = _productionPlacement->initial_cursor;
        _moveMode = _attackMode = _healMode = false;
        _returnCursor.reset();
        _rangeDirty = true;
        refreshControls();
        return;
    }
    case ui_menu:
        if (_uiState.dialogue) {
            action({ui_dialogue_skip, 0});
            return;
        }
        if (_session && _panelPurpose == NativePanel::none) {
            _checkpointRequested = true;
            saveCheckpoint();
            showPause();
        } else if (!_session)
            showProfileMenu();
        return;
    case ui_dialogue_next:
    case ui_dialogue_skip:
        if (_session && _panelPurpose == NativePanel::none &&
            _session->mission().gate == MissionGate::dialogue) {
            _inputCommand = _session->mission().command_index;
            _inputEdges |= a.id == ui_dialogue_skip ? 128U : 16U;
        }
        return;
    case ui_tech_specs:
        openTechSpecs();
        return;
    case ui_specs_back:
        if (_panelPurpose != NativePanel::tech_specs)
            return;
        _specsGpu.reset();
        _specsModel.reset();
        if (_techSpecsFromPause)
            showPause();
        else {
            hidePanel();
            refreshControls();
        }
        return;
    case ui_specs_previous:
        if (_panelPurpose != NativePanel::tech_specs)
            return;
        if (_techSpecsRoster.size() > 1) {
            _techSpecsPosition =
                (_techSpecsPosition + _techSpecsRoster.size() - 1) % _techSpecsRoster.size();
            renderCurrentTechSpecs();
        }
        return;
    case ui_specs_next:
        if (_panelPurpose != NativePanel::tech_specs)
            return;
        if (_techSpecsRoster.size() > 1) {
            _techSpecsPosition = (_techSpecsPosition + 1) % _techSpecsRoster.size();
            renderCurrentTechSpecs();
        }
        return;
    case ui_specs_robot:
    case ui_specs_vehicle:
        if (_panelPurpose != NativePanel::tech_specs)
            return;
        if (_specsModel) {
            _techSpecsPreviewForm = a.id == ui_specs_vehicle ? 1 : 0;
            if (_uiState.tech_specs && _techSpecsPreviewForm >= _uiState.tech_specs->data.forms)
                return;
            _specsModel->set_form(_techSpecsPreviewForm);
        }
        return;
    default:
        break;
    }
    if (!_session || !_loaded || _failed)
        return;
    if (a.id == ui_build || a.id == ui_deploy) {
        if (_panelPurpose == NativePanel::production && a.value == 1)
            hidePanel();
        if (_panelPurpose != NativePanel::none)
            return;
        const bool both = _session->selected_production_eligibility(ProductionMode::ordinary) ==
                              ProductionEligibility::allowed &&
                          _session->selected_production_eligibility(ProductionMode::deploy) ==
                              ProductionEligibility::allowed;
        if (a.id == ui_build && both && a.value == 0) {
            showPanel(NativePanel::production, "Activate", "Select available unit activation");
            addButton("Activate unit", ui_build, 1);
            addButton("Activate tape", ui_deploy, 1);
            addButton("Cancel", production_cancel);
            return;
        }
        showProduction(a.id == ui_deploy ? ProductionMode::deploy : ProductionMode::ordinary);
        return;
    }
    if (_panelPurpose != NativePanel::none || _session->battle() || _session->world_presentation())
        return;
    switch (a.id) {
    case ui_move:
    case ui_attack:
    case ui_heal:
        _moveMode = a.id == ui_move;
        _attackMode = a.id == ui_attack && !_session->selected_attack_targets().empty();
        _healMode = a.id == ui_heal && !_session->selected_heal_targets().empty();
        _productionMode.reset();
        _productionType.reset();
        _productionPlacement.reset();
        _returnCursor.reset();
        _pendingDestination.reset();
        _rangeDirty = true;
        _notice.clear();
        break;
    case ui_capture: {
        auto r = _session->capture_selected();
        if (r.status == CaptureStatus::captured) {
            ++_captureActions;
            _checkpointRequested = true;
            if (r.audio_5_12)
                dispatchAudio({5, 12});
            notice("Captured.");
            _rangeDirty = true;
            _pendingDestination.reset();
            _moveMode = _attackMode = _healMode = false;
        } else if (r.message_id)
            _pendingMessageIds.push_back(*r.message_id);
        break;
    }
    case ui_transform: {
        auto r = _session->transform_selected();
        if (r) {
            ++_transformActions;
            _checkpointRequested = true;
            if (r->audio_event_in_group5)
                dispatchAudio({5, *r->audio_event_in_group5});
            _rangeDirty = true;
            _pendingDestination.reset();
            _moveMode = _attackMode = _healMode = false;
            _notice.clear();
        }
        break;
    }
    case ui_upgrade: {
        auto r = _session->upgrade_selected();
        if (r) {
            ++_upgradeActions;
            _checkpointRequested = true;
            _rangeDirty = true;
            dispatchAudio({5, 13});
            _moveMode = _attackMode = _healMode = false;
            _pendingDestination.reset();
            notice("Level " + std::to_string(r->combat.level) + " / Energon " +
                   std::to_string(r->funds_after));
        }
        break;
    }
    case ui_undo: {
        auto r = _session->undo_selected();
        if (r) {
            ++_undoActions;
            _checkpointRequested = true;
            _rangeDirty = true;
            _moveMode = _attackMode = _healMode = false;
            _productionMode.reset();
            _productionType.reset();
            _productionPlacement.reset();
            _pendingDestination.reset();
            _returnCursor = r->cursor;
            _previousUnits = _session->units();
            dispatchAudio({r->audio_operation, r->audio_argument});
            _notice.clear();
        }
        break;
    }
    case ui_end_turn:
        if (_session->end_turn()) {
            ++_endTurnActions;
            _checkpointRequested = true;
            _rangeDirty = true;
            _moveMode = _attackMode = _healMode = false;
            _pendingDestination.reset();
            _notice.clear();
        }
        break;
    default:
        break;
    }
    refreshControls();
    saveCheckpoint();
}
bool AndroidGame::Impl::productionTileAllowed(WorldCoordinate tile) {
    if (!_productionMode || !_productionPlacement || !_session->selected())
        return false;
    auto &u = _session->units().at(*_session->selected());
    int dx = int(tile.x) - u.tile_x, dy = int(tile.y) - u.tile_y;
    const std::array<std::array<int, 2>, 4> directions{{{{0, -1}}, {{0, 1}}, {{-1, 0}}, {{1, 0}}}};
    for (std::size_t i = 0; i < 4; ++i)
        if (dx == directions[i][0] && dy == directions[i][1])
            return _productionPlacement->reachable[i] && _productionPlacement->vacant[i];
    return false;
}
void AndroidGame::Impl::showProduction(ProductionMode mode) {
    if (!_session || !_session->selected() ||
        _session->selected_production_eligibility(mode) != ProductionEligibility::allowed)
        return;
    auto candidates = _session->selected_production_candidates(mode);
    if (candidates.empty())
        return;
    _productionMode = mode;
    showPanel(NativePanel::production, originalText(mode == ProductionMode::deploy ? 1055 : 785),
              "Energy: " + std::to_string(_session->turn().funds[_session->current_player()]));
    for (auto type : candidates) {
        auto &d = _session->world().unit_definitions.at(type);
        addButton(_text->at(script_text_id(unit_text_index(d.raw_text_indices[0]))) + " · " +
                      std::to_string(unit_spawn_cost(d)),
                  production_type, type);
    }
    addButton("Cancel", production_cancel);
}

Bounds AndroidGame::Impl::unitBounds(std::size_t i) const {
    Bounds bounds;
    if (!_scene || i >= _animations.size() || !_animations[i])
        return bounds;
    const auto index = _scene->sprites[i].unit_index;
    if (!_session->units().at(index).active)
        return bounds;
    if (_graphicsModern && _tacticalHitBounds.count(i))
        return _tacticalHitBounds.at(i);
    auto matrix = multiply_matrix(_camera.matrix(), _unitModels[i]);
    for (const auto &v : _spriteVertices[i][_animations[i]->current_frame()]) {
        auto p = transform(matrix, v.position);
        if (p[3] != 0)
            bounds.add((p[0] / p[3] * .5 + .5) * _width, (.5 - p[1] / p[3] * .5) * _height);
    }
    return bounds;
}
std::optional<std::size_t> AndroidGame::Impl::unitAtPoint(Point point, double padding) const {
    std::optional<std::size_t> selected;
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < _animations.size(); ++i) {
        const auto bounds = unitBounds(i);
        if (bounds.contains(point, padding)) {
            auto d = std::hypot(point.x - bounds.x - bounds.width / 2,
                                point.y - bounds.y - bounds.height / 2);
            if (d < best) {
                best = d;
                selected = i;
            }
        }
    }
    return selected;
}
void AndroidGame::Impl::tap(Point point) {
    if (!_loaded || _failed || !_session || _panelPurpose != NativePanel::none ||
        _session->world_presentation() || _session->mission().interaction_mode != 0 ||
        _session->moving() || _session->focus_request() || _session->battle())
        return;
    const auto hit = unitAtPoint(point, _moveMode ? 0 : 14 * _ui->layout().scale);
    std::optional<WorldCoordinate> tile;
    float closest = std::numeric_limits<float>::max();
    auto camera = _camera.matrix();
    for (const auto &q : _controlQuads) {
        if (q.face != TerrainFace::top)
            continue;
        std::array<Point, 4> screen{};
        float depth = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            auto &p = q.positions[i];
            auto c = transform(camera, {{p[0] * _scene->terrain.position_scale,
                                         p[1] * _scene->terrain.position_scale,
                                         p[2] * _scene->terrain.position_scale, 1}});
            screen[i] = {(c[0] / c[3] * .5 + .5) * _width, (.5 - c[1] / c[3] * .5) * _height};
            depth += c[2] / c[3];
        }
        if ((inside_triangle(point, screen[0], screen[1], screen[2]) ||
             inside_triangle(point, screen[2], screen[1], screen[3])) &&
            depth < closest) {
            closest = depth;
            tile = WorldCoordinate{std::uint8_t(q.tile_x), std::uint8_t(q.tile_y)};
        }
    }
    if (_moveMode && hit) {
        auto &u = _session->units().at(_scene->sprites[*hit].unit_index);
        tile = WorldCoordinate{u.tile_x, u.tile_y};
    }
    auto confirmed = [&] {
        return tile && _pendingDestination && tile->x == _pendingDestination->x &&
               tile->y == _pendingDestination->y;
    };
    if (_productionMode && _productionType && tile && productionTileAllowed(*tile)) {
        if (confirmed()) {
            auto r = _session->produce_selected(*_productionMode, *_productionType, *tile,
                                                ProductionConfirmation::direct_type_list);
            if (r) {
                ++_productionActions;
                _checkpointRequested = true;
                dispatchAudio({r->audio_operation, r->audio_argument});
                synchronizeScene();
                _returnCursor = r->return_cursor;
                notice("Unit activated.");
            } else
                notice("That activation is not available.");
            _productionMode.reset();
            _productionType.reset();
            _productionPlacement.reset();
            _pendingDestination.reset();
        } else {
            _pendingDestination = tile;
            notice("Tap the same tile again to activate.");
        }
        _rangeDirty = true;
        refreshControls();
        return;
    }
    if (_moveMode && tile && _previewRange && _previewRange->at(tile->x, tile->y) >= 0 &&
        _previewRange->at(tile->x, tile->y) != movement_reserved_cell) {
        if (confirmed()) {
            if (_session->move_selected_to(*tile) == MoveRequestResult::started) {
                ++_moveActions;
                _checkpointRequested = true;
                _moveMode = false;
                _pendingDestination.reset();
                notice("Moving...");
            } else
                notice("That move is not available.");
        } else {
            _pendingDestination = tile;
            notice("Tap the same tile again to move.");
        }
        _rangeDirty = true;
        refreshControls();
        return;
    }
    if (hit) {
        auto index = _scene->sprites[*hit].unit_index;
        if (_healMode) {
            if (std::find(_healTargets.begin(), _healTargets.end(), index) != _healTargets.end()) {
                auto r = _session->heal_selected(index);
                if (r) {
                    if (r->outcome == HealOutcome::healed) {
                        ++_healActions;
                        _checkpointRequested = true;
                        notice("HP " + std::to_string(r->target.health) + " / Energon " +
                               std::to_string(r->funds_after));
                    }
                    if (r->audio_event_in_group5)
                        dispatchAudio({5, *r->audio_event_in_group5});
                    if (r->message_id)
                        _pendingMessageIds.push_back(*r->message_id);
                }
                _healMode = false;
                _rangeDirty = true;
            } else
                notice("Select a highlighted friendly unit.");
            refreshControls();
            return;
        }
        if (_attackMode) {
            if (std::find(_attackTargets.begin(), _attackTargets.end(), index) !=
                    _attackTargets.end() &&
                _session->attack_selected(index)) {
                ++_attackActions;
                _checkpointRequested = true;
                _attackMode = false;
                _rangeDirty = true;
                _notice.clear();
            } else
                notice("Select a highlighted enemy.");
            refreshControls();
            return;
        }
        if (_session->select(index)) {
            _moveMode = _attackMode = _healMode = false;
            _productionMode.reset();
            _productionType.reset();
            _productionPlacement.reset();
            _returnCursor.reset();
            _pendingDestination.reset();
            _notice.clear();
            _rangeDirty = true;
        } else
            notice("Select one of your available units.");
    } else if (!_moveMode && !_attackMode && !_healMode && !_productionMode) {
        _session->clear_selection();
        _rangeDirty = true;
    }
    refreshControls();
}
void AndroidGame::Impl::touch(int event, float x, float y, float dx, float dy, float scale) {
    if (!_ui)
        return;
    if (event == 0) {
        auto a = _ui->hit(x, y);
        if (a)
            action(a);
        else if (_uiState.dialogue)
            action({ui_dialogue_next, 0});
        else
            tap({x, y});
        return;
    }
    if (_panelPurpose == NativePanel::tech_specs) {
        const auto viewport = _ui->specsModelViewport();
        if (event == 1 && viewport.width > 0)
            _specsAngle += dx / viewport.width * 1.6F;
        return;
    }
    if (_panelPurpose != NativePanel::none) {
        if (event == 1 || event == 3)
            _ui->scroll(dy);
        return;
    }
    if (!_session || _session->world_presentation() || _session->mission().interaction_mode != 0 ||
        _session->focus_request() || _session->battle())
        return;
    if (event == 1) {
        float factor = _camera.span / _height;
        _camera.center_x -= dx * factor * _touchCameraSpeed;
        _camera.center_z += dy * factor * _touchCameraSpeed / _camera.up_z;
    } else if (event == 2 && std::isfinite(scale) && scale > 0)
        _camera.span = std::clamp(_camera.span / std::pow(scale, _touchCameraSpeed),
                                  _camera.base_span * .2F, _camera.base_span * 3);
    ++_contentGeneration;
}
void AndroidGame::Impl::back() {
    if (_failed)
        return;
    if (_panelPurpose == NativePanel::none) {
        if (_uiState.dialogue)
            action({ui_dialogue_skip, 0});
        else if (_session) {
            _checkpointRequested = true;
            saveCheckpoint();
            showPause();
        } else
            showProfileMenu();
    } else if (_panelPurpose == NativePanel::pause)
        action({resume_game, 0});
    else if (_panelPurpose == NativePanel::tech_specs)
        action({ui_specs_back, 0});
    else if (_panelPurpose == NativePanel::production)
        action({production_cancel, 0});
    else if (_panelPurpose == NativePanel::settings)
        action({settings_back, 0});
    else if (_panelPurpose == NativePanel::help_topic)
        showHelp();
    else if (_panelPurpose == NativePanel::help || _panelPurpose == NativePanel::credits)
        action({information_back, 0});
    else if (_panelPurpose == NativePanel::mission_select)
        showModesMenu();
    else if (_panelPurpose == NativePanel::mode_select)
        showProfileMenu();
    else if (_panelPurpose == NativePanel::message)
        dismissMessage();
    else if (_panelPurpose == NativePanel::arena)
        showMissionPicker();
    else if (_panelPurpose == NativePanel::arena_opponent)
        arenaFaction(_pendingArenaIndex);
}

std::string AndroidGame::Impl::messageText(std::uint32_t r) const {
    if ((r & 0xffff0000U) == 0x21ff0000U)
        return originalText(std::uint16_t(r));
    if (r <= 65535 && _text->contains(std::uint16_t(r)))
        return originalText(std::uint16_t(r));
    throw FormatError("Requested localized message has no source text");
}
void AndroidGame::Impl::showNextMessage() {
    if (_panelPurpose != NativePanel::none || _pendingMessageIds.empty())
        return;
    showPanel(NativePanel::message, originalText(715), messageText(_pendingMessageIds.front()), 0,
              true);
    addButton(originalText(587), dismiss_message, 0, false);
}
void AndroidGame::Impl::dismissMessage() {
    if (_panelPurpose != NativePanel::message || _pendingMessageIds.empty() ||
        _drawnPanelToken != _panelToken)
        return;
    _presentedMessageIds.push_back(_pendingMessageIds.front());
    if (_presentedMessageIds.size() > 256)
        _presentedMessageIds.erase(_presentedMessageIds.begin());
    _pendingMessageIds.erase(_pendingMessageIds.begin());
    hidePanel();
    showNextMessage();
    refreshControls();
}
void AndroidGame::Impl::showResults() {
    auto &state = _campaign->state();
    if (!state.results)
        throw FormatError("Results record missing");
    auto &r = *state.results;
    std::string name = _session && _session->world().title_text_index
                           ? _text->at(script_text_id(*_session->world().title_text_index))
                           : "";
    std::string body = originalText(784) + std::to_string(r.campaign.mission_index + 1) + ": " +
                       name + "\n\n" + originalText(1217) + " " +
                       std::to_string(r.current_record.value_278) + "\n" + originalText(941) + " " +
                       std::to_string(r.current_record.value_27c) + "\n" + originalText(667) + " " +
                       std::to_string(r.current_record.value_274);
    if (!r.finished_campaign)
        body += "\n\n" + originalText(1321);
    if (state.profile_persisted)
        body += "\n" + originalText(462);
    showPanel(NativePanel::results, originalText(1323), body, state.token, true);
    addButton(originalText(587), results_continue, 0, false);
}
void AndroidGame::Impl::retryCampaignSave() {
    if (!_campaign)
        return;
    try {
        if (_campaign->state().gate == CampaignControllerGate::save_failed) {
            auto r = _campaign->retry_save(_campaign->state().token);
            if (!r.succeeded)
                _panelPurpose = NativePanel::none;
        }
        _campaignRetryRequired = false;
        updateCampaign(0);
    } catch (const std::exception &e) {
        _campaignRetryRequired = true;
        showPanel(NativePanel::save_error, "Campaign could not continue", e.what(), 0, true);
        addButton("Retry", retry_campaign);
    }
}
void AndroidGame::Impl::updateCampaign(std::uint32_t delta) {
    if (!_campaign || !_session || _campaignRetryRequired)
        return;
    if (_panelPurpose == NativePanel::message) {
        if (_uiState.panel && !_uiState.panel->buttons.empty())
            _uiState.panel->buttons.front().enabled = _drawnPanelToken == _panelToken;
        return;
    }
    using Gate = CampaignControllerGate;
    if (_campaign->state().gate == Gate::idle) {
        if (_panelPurpose != NativePanel::none || !_pendingMessageIds.empty() ||
            _session->mission().stop != MissionStop::campaign_handoff_required)
            return;
        (void)_campaign->begin(*_session);
    }
    const auto token = _campaign->state().token;
    if (_campaign->state().gate == Gate::save_failed) {
        if (_panelPurpose != NativePanel::save_error || _panelToken != token) {
            showPanel(NativePanel::save_error, "Campaign could not be saved",
                      _campaign->state().save_error, token, true);
            addButton("Retry save", retry_campaign);
        }
        return;
    }
    if (_campaign->state().gate == Gate::results) {
        if (_panelPurpose != NativePanel::results || _panelToken != token)
            showResults();
        if (_drawnPanelToken == token && !_campaign->state().results->drawn)
            _campaign->report_results_presented(token,
                                                std::int32_t(std::ceil(_ui->layout().safe.height)));
        if (_campaign->state().results->drawn) {
            if (_uiState.panel && !_uiState.panel->buttons.empty())
                _uiState.panel->buttons.front().enabled = true;
            auto update = _campaign->update_results(token, delta ? _resultsInputEdges : 0U, delta);
            if (delta)
                _resultsInputEdges = update.remaining_input_edges;
            if (_uiState.panel)
                _uiState.panel->background_offset = update.background_offset_q16;
        }
        if (_campaign->state().gate == Gate::results)
            return;
    }
    try {
        if (_campaign->state().gate == Gate::next_mission) {
            auto prepared = _campaign->prepare_next_campaign(token, *_archive);
            auto session = std::make_unique<GameSession>(*_archive, std::move(prepared.bootstrap),
                                                         prepared.context);
            auto next = std::make_unique<CampaignController>(_profilePath);
            installSession(std::move(session));
            if (!retireCheckpoint())
                throw std::runtime_error(_checkpointError);
            _campaign->report_next_session_installed(token, *_session);
            _campaign = std::move(next);
            _checkpointRequested = true;
            _restoredCheckpoint = false;
            hidePanel();
            refreshControls();
            return;
        }
        if (_campaign->state().gate == Gate::campaign_menu) {
            if (!retireCheckpoint())
                throw std::runtime_error(_checkpointError);
            if (_panelPurpose != NativePanel::menu || _panelToken != token)
                showProfileMenu();
            if (_drawnPanelToken == token)
                _campaign->report_menu_presented(token);
            return;
        }
        if (_campaign->state().gate == Gate::campaign_ending) {
            if (!retireCheckpoint())
                throw std::runtime_error(_checkpointError);
            if (!_campaign->state().handoff || !_campaign->state().handoff->ending_text_resource)
                throw FormatError("Campaign ending lacks its source text");
            if (_panelPurpose != NativePanel::ending || _panelToken != token) {
                showPanel(NativePanel::ending, originalText(715),
                          messageText(*_campaign->state().handoff->ending_text_resource), token,
                          true);
                addButton(originalText(1169), profile, 0, false);
            }
            if (_drawnPanelToken == token) {
                _campaign->report_ending_presented(token);
                if (_uiState.panel && !_uiState.panel->buttons.empty())
                    _uiState.panel->buttons.front().enabled = true;
            }
        }
    } catch (const std::exception &e) {
        _campaignError = e.what();
        _campaignRetryRequired = true;
        showPanel(NativePanel::save_error, "Campaign could not continue", _campaignError, 0, true);
        addButton("Retry", retry_campaign);
    }
}
void AndroidGame::Impl::prepareWorldPresentation() {
    if (!_session->world_presentation())
        return;
    const auto &p = *_session->world_presentation();
    if (_preparedWorldToken && *_preparedWorldToken == p.token)
        return;
    synchronizeScene();
    refreshWorldOverlays();
    for (const auto &plan : p.operations) {
        if (auto map = std::get_if<WorldMapInstall>(&plan.data)) {
            if (map->music_request) {
                _lastMapMusic = map->music_request;
                dispatchAudio(*map->music_request);
            }
            if (map->stop_music_if_config_a4_disabled && !_audio->musicEnabled())
                dispatchAudio({3, 0});
        } else if (auto phase = std::get_if<WorldPhaseCommand>(&plan.data)) {
            if (phase->title_text_id)
                _title = originalText(*phase->title_text_id);
        }
    }
    _preparedWorldToken = p.token;
    ++_sceneRevision;
    ++_contentGeneration;
}
void AndroidGame::Impl::maintainMenuMusic() {
    if (_failed || !_foreground || !_audio)
        return;
    static const MenuMusicNode main{}, list{&main}, faction{&list}, enable{}, pauseNode{},
        actions{}, opponent{&faction};
    const MenuMusicNode *current = nullptr;
    switch (_panelPurpose) {
    case NativePanel::startup:
    case NativePanel::menu:
    case NativePanel::mode_select:
    case NativePanel::settings:
    case NativePanel::help:
    case NativePanel::help_topic:
    case NativePanel::credits:
        current = &main;
        break;
    case NativePanel::mission_select:
        current = &list;
        break;
    case NativePanel::arena:
        current = &faction;
        break;
    case NativePanel::arena_opponent:
        current = &opponent;
        break;
    case NativePanel::pause:
        current = &pauseNode;
        break;
    default:
        return;
    }
    const MenuMusicContext ctx{
        _audio->musicEnabled(), 10, current, &enable, &pauseNode, &opponent, &actions};
    if (menu_music_queries_playing(ctx) && !_audio->request({6, 0}))
        if (auto r = idle_menu_music(ctx))
            dispatchAudio(*r);
}
void AndroidGame::Impl::maintainMusicAfterStep(MusicMaintenanceSite site) {
    if (!_loaded || _failed || !_session || _panelPurpose != NativePanel::none ||
        _session->mission().stop != MissionStop::none)
        return;
    const bool battle = _session->battle() && _battlePresentation &&
                        _battlePresentation->status().token == _session->battle()->token;
    if ((site == MusicMaintenanceSite::map_update && battle) ||
        (site == MusicMaintenanceSite::ordinary_battle && !battle))
        return;
    auto plan = plan_music_maintenance(
        {site, _audio->musicEnabled(), _session->mission().interaction_mode, 100},
        _session->world().style);
    if (plan.query_playing && !_audio->request({6, 0}) && plan.when_idle)
        dispatchAudio(*plan.when_idle);
}

void AndroidGame::Impl::simulate(std::uint32_t delta) {
    if (!_session || _typeListOpen)
        return;
    auto installed = _sessionInstalls;
    auto panel = _panelPurpose;
    updateCampaign(0);
    if (installed != _sessionInstalls ||
        (panel != NativePanel::none && _panelPurpose == NativePanel::none))
        delta = 0;
    if (_panelPurpose != NativePanel::none) {
        auto steps = _simulationClock.add_elapsed(delta);
        for (std::uint32_t i = 0; i < steps && _panelPurpose != NativePanel::none; ++i)
            updateCampaign(SimulationClock::step_ms);
        return;
    }
    showNextMessage();
    if (_panelPurpose != NativePanel::none)
        return;
    synchronizeScene();
    prepareWorldPresentation();
    refreshWorldOverlays();
    refreshTerrainDecorations();
    auto steps = _simulationClock.add_elapsed(delta);
    for (std::uint32_t n = 0; n < steps; ++n) {
        _previousUnits = _session->units();
        refreshControls();
        if (_panelPurpose != NativePanel::none)
            break;
        if (_session->battle()) {
            if (_supportPresentation && !_supportPresentation->status().complete) {
                auto status = _supportPresentation->status();
                if (_supportDisplay.can_advance(status)) {
                    auto result = _supportPresentation->advance(SimulationClock::step_ms);
                    ++_supportTicks;
                    ++_contentGeneration;
                    if (result == SupportPresentationAction::start_main_battle) {
                        prepareBattle();
                        break;
                    }
                    maintainMusicAfterStep(MusicMaintenanceSite::map_update);
                    if (result == SupportPresentationAction::show_next_helper) {
                        showSupportHelper();
                        break;
                    }
                }
            } else if (_battlePresentation &&
                       _battlePresentedToken == _battlePresentation->status().token &&
                       !_battlePresentation->status().complete) {
                _battlePresentation->advance(SimulationClock::step_ms);
                for (auto r : _battlePresentation->take_audio_requests())
                    dispatchAudio(r);
                maintainMusicAfterStep(MusicMaintenanceSite::ordinary_battle);
                ++_battlePresentationTicks;
                ++_contentGeneration;
            }
            continue;
        }
        advanceCamera(SimulationClock::step_ms);
        MissionTickInput input;
        if (_session->mission().gate == MissionGate::dialogue) {
            if (!_uiState.dialogue)
                continue;
            input.dialogue_layout = _ui->dialogueLayout(*_uiState.dialogue);
            if (_dialogueLayout && _dialoguePresentedIdentity == _uiState.dialogue->identity &&
                _dialoguePresentedFirstLine == _uiState.dialogue->progress.first_line)
                input.dialogue_layout->visible_text_fully_revealed =
                    _dialogueLayout->visible_text_fully_revealed;
            if (input.dialogue_layout->box_height <= 0 || input.dialogue_layout->line_height <= 0)
                continue;
            if (_inputCommand == _session->mission().command_index)
                input.input_edges = _inputEdges;
        } else
            _inputEdges = 0;
        auto result = _session->tick(SimulationClock::step_ms, input);
        synchronizeScene();
        prepareWorldPresentation();
        refreshWorldOverlays();
        refreshTerrainDecorations();
        _inputEdges = result.mission.remaining_input_edges;
        for (auto r : result.audio)
            dispatchAudio(r);
        _pendingMessageIds.insert(_pendingMessageIds.end(), result.message_ids.begin(),
                                  result.message_ids.end());
        for (auto effect : result.mission.effects)
            if (effect == MissionEffect::skip_dialogue_audio_event)
                dispatchAudio({5, 18});
        if (result.completion) {
            for (auto r : result.completion->audio_requests)
                dispatchAudio(r);
            _pendingCompletionRequests.emplace_back(_session->mission().command_index,
                                                    *result.completion);
            if (result.completion->text_resource_id)
                _pendingMessageIds.push_back(*result.completion->text_resource_id);
        }
        ++_simulationTicks;
        if (result.movement_finished || result.world_changed || result.turn_transition) {
            _rangeDirty = true;
            _checkpointRequested = true;
        }
        if (_session->battle()) {
            prepareBattle();
            break;
        }
        advanceSprites(SimulationClock::step_ms);
        showNextMessage();
        if (_panelPurpose != NativePanel::none)
            break;
        maintainMusicAfterStep(MusicMaintenanceSite::map_update);
    }
    if (_panelPurpose != NativePanel::none || _typeListOpen)
        return;
    if (!_session->battle())
        advanceCamera(0);
    refreshControls();
    if (_session->checkpoint_available() && _checkpointCommand != _session->mission().command_index)
        _checkpointRequested = true;
    saveCheckpoint();
    const float blend = _simulationClock.interpolation();
    for (std::size_t i = 0; i < _unitModels.size(); ++i) {
        const auto index = _scene->sprites[i].unit_index;
        const auto &unit = _session->units().at(index);
        auto model = unit_model(_session->world(), unit);
        if (index < _previousUnits.size() && _previousUnits[index].form == unit.form) {
            auto previous = unit_model(_session->world(), _previousUnits[index]);
            for (std::size_t j = 12; j < 15; ++j)
                model[j] = previous[j] + (model[j] - previous[j]) * blend;
        }
        if (model != _unitModels[i]) {
            _unitModels[i] = model;
            ++_contentGeneration;
        }
    }
}

void AndroidGame::Impl::openTechSpecs() {
    if (!_session || !_loaded || _failed ||
        (_panelPurpose != NativePanel::none && _panelPurpose != NativePanel::pause) ||
        _session->battle() || _session->moving() || _session->focus_request() ||
        _session->mission().gate == MissionGate::dialogue)
        return;
    _techSpecsRoster.clear();
    auto &units = _session->world().units;
    for (std::size_t i = 0; i < units.size(); ++i)
        if (units[i].active && units[i].type < _session->world().unit_definitions.size())
            _techSpecsRoster.push_back(i);
    if (_techSpecsRoster.empty())
        return;
    _techSpecsFromPause = _panelPurpose == NativePanel::pause;
    _techSpecsPosition = 0;
    if (_session->selected()) {
        auto it =
            std::find(_techSpecsRoster.begin(), _techSpecsRoster.end(), *_session->selected());
        if (it != _techSpecsRoster.end())
            _techSpecsPosition = std::distance(_techSpecsRoster.begin(), it);
    }
    _panelPurpose = NativePanel::tech_specs;
    _presentationEpoch.invalidate();
    _inputEdges = 0;
    _simulationClock.reset();
    _uiState = {};
    _uiState.tactical.visible = false;
    _uiState.show_menu = false;
    renderCurrentTechSpecs();
}
void AndroidGame::Impl::renderCurrentTechSpecs() {
    if (!_session || _techSpecsPosition >= _techSpecsRoster.size())
        return;
    auto index = _techSpecsRoster[_techSpecsPosition];
    auto &w = _session->world();
    auto &u = w.units.at(index);
    auto &d = w.unit_definitions.at(u.type);
    const auto stats = read_tech_specs(d, _session->combat_units().at(index).level);
    auto asText = [&](std::uint16_t raw) {
        return _text->at(script_text_id(unit_text_index(raw)));
    };
    UiTechSpecs out;
    out.data = stats;
    out.name = asText(d.raw_text_indices[0]);
    out.quote = asText(d.raw_text_indices[1]);
    out.vehicle = stats.forms > 1;
    const auto &form = stats.form[0];
    out.details.push_back(
        "ATTACK RANGE: " + std::to_string(form.min_range) +
        (form.min_range == form.max_range ? "" : "-" + std::to_string(form.max_range)));
    if (d.type == 11) {
        for (std::uint16_t id : {807, 714, 929})
            out.details.push_back(originalText(id));
    } else if (d.type == 19) {
        for (std::uint16_t id : {807, 929, 1174})
            out.details.push_back(originalText(id));
    } else
        for (auto raw : d.raw_detail_text_indices) {
            auto detail = asText(raw);
            if (detail == "ATTACK RANGE: " || detail == "NEXT LEVEL UNLOCKED!" ||
                (detail == "CANNOT ATTACK" && form.combat.attack_low > 0))
                continue;
            out.details.push_back(detail);
        }
    out.portrait_resource = d.type == 11   ? 731
                            : d.type == 19 ? 646
                            : d.type == 0  ? 878
                            : d.type == 2  ? 670
                                           : 0;
    _uiState.tech_specs = std::move(out);
    _techSpecsPreviewForm = std::min<std::uint8_t>(u.form, stats.forms - 1);
    _specsAngle = 0;
    try {
        _specsModel = std::make_unique<TechSpecsModel>(*_archive, u.type);
        _specsModel->set_form(_techSpecsPreviewForm);
        _specsGpu = _m3gRenderer->upload(_specsModel->asset());
    } catch (const std::exception &e) {
        _specsGpu.reset();
        _specsModel.reset();
        notice(std::string("Original model preview unavailable: ") + e.what());
    }
}
void AndroidGame::Impl::drawTechSpecs(adaptive::Rect rect) {
    if (!_specsModel || !_specsGpu || rect.width <= 0 || rect.height <= 0)
        return;
    const int x = int(std::round(rect.x)), y = _height - int(std::round(rect.y + rect.height)),
              w = int(std::round(rect.width)), h = int(std::round(rect.height));
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glDepthMask(GL_TRUE);
    glClearDepthf(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    auto frame = _specsModel->prepare(w, h, _specsAngle);
    if (frame.assets.size() != 1)
        throw FormatError("Tech Specs frame must contain its single source model");
    frame.assets[0] = &_specsGpu->asset();
    _m3gRenderer->encode_frame({_specsGpu}, frame);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, _width, _height);
}

void AndroidGame::Impl::releaseTarget() {
    if (_depth)
        glDeleteRenderbuffers(1, &_depth);
    if (_color)
        glDeleteTextures(1, &_color);
    if (_fbo)
        glDeleteFramebuffers(1, &_fbo);
    _depth = _color = _fbo = 0;
    _renderWidth = _renderHeight = 0;
}
void AndroidGame::Impl::beginTarget() {
    quality::Capability cap;
    const auto pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
        cap.memory_bytes = std::uint64_t(pages) * std::uint64_t(pageSize);
    GLint maximumTexture = 0, maximumDepth = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTexture);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximumDepth);
    cap.maximum_dimension = std::uint32_t(std::max(64, std::min(maximumTexture, maximumDepth)));
    cap.memory_pressure = _renderMemoryPressure;
    cap.low_power = _renderLowPower;
    cap.thermal_serious = _renderThermalSerious;
    _renderQuality = quality::plan(
        _width, _height, _graphicsModern ? quality::Mode::modern : quality::Mode::classic, cap);
    auto nativeTarget = [this] {
        releaseTarget();
        _renderWidth = _width;
        _renderHeight = _height;
        _renderQuality.width = _width;
        _renderQuality.height = _height;
        _renderQuality.scale = 1;
        _renderQuality.supersampling = false;
        _renderQuality.estimated_surface_bytes = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, _width, _height);
    };
    // Match MetalSupersampling::begin: Classic and constrained Modern render
    // directly to the native target, retaining native-resolution UI afterward.
    if (!_renderQuality.supersampling) {
        nativeTarget();
        return;
    }
    const int w = int(_renderQuality.width), h = int(_renderQuality.height);
    if (!_fbo || w != _renderWidth || h != _renderHeight) {
        const auto precedingError = glGetError();
        if (precedingError == 0x0507)
            throw GLContextLost("Context lost before scene target allocation");
        if (precedingError != GL_NO_ERROR)
            throw FormatError("GLES error precedes scene target allocation");
        releaseTarget();
        glGenFramebuffers(1, &_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glGenTextures(1, &_color);
        glBindTexture(GL_TEXTURE_2D, _color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _color, 0);
        glGenRenderbuffers(1, &_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, _depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depth);
        const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        bool allocationFailed = !_fbo || !_color || !_depth || status != GL_FRAMEBUFFER_COMPLETE;
        // Drain only errors produced by this allocation, so the native fallback
        // does not inherit GL_OUT_OF_MEMORY or incomplete-attachment errors.
        for (GLenum error; (error = glGetError()) != GL_NO_ERROR;) {
            if (error == 0x0507)
                throw GLContextLost("Context lost during scene target allocation");
            allocationFailed = true;
        }
        if (allocationFailed) {
            ++_renderAllocationFallbacks;
            nativeTarget();
            return;
        }
        _renderWidth = w;
        _renderHeight = h;
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClearDepthf(1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glViewport(0, 0, w, h);
}
void AndroidGame::Impl::resolveTarget() {
    if (_fbo)
        _renderer->resolve_supersampling(_color, _width, _height);
}

void AndroidGame::Impl::draw(std::uint64_t nanos) {
    if (!_renderer || !_ui || !_foreground)
        return;
    _pendingFrame.reset();
    // Preserve sub-millisecond time across 60/90/120 Hz display callbacks. The
    // authoritative engine still advances exclusively in its 30 ms steps.
    std::uint32_t delta = 0;
    if (!_clockStarted) {
        _clockStarted = true;
        _elapsedNanosRemainder = 0;
    } else {
        const auto elapsed = std::min<std::uint64_t>(nanos, 1000000000ULL) + _elapsedNanosRemainder;
        delta = std::uint32_t(elapsed / 1000000ULL);
        _elapsedNanosRemainder = elapsed % 1000000ULL;
    }
    _audio->maintain();
    maintainMenuMusic();
    if (_session)
        simulate(delta);
    if (_panelPurpose == NativePanel::none)
        refreshControls();
    NativePresentedFrame frame;
    frame.scene_revision = _sceneRevision;
    frame.content_generation = _contentGeneration;
    frame.presentation_epoch = _presentationEpoch.current();
    if (_session && _loaded && _panelPurpose == NativePanel::none) {
        beginTarget();
        renderScene(frame, {double(_renderWidth), double(_renderHeight)});
        resolveTarget();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, _width, _height);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(.025F, .04F, .065F, 1);
        glClearDepthf(1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    if (!_notice.empty() && nowSeconds() < _noticeUntil)
        _uiState.notice = _notice;
    frame.ui = _ui->render(_uiState, [this](adaptive::Rect r) { drawTechSpecs(r); });
    if (!frame.ui.tactical_drawn)
        frame.world.reset();
    if (_panelPurpose == NativePanel::none && _session && _session->battle() &&
        _supportPresentation && !_supportPresentation->status().complete)
        frame.support =
            _supportDisplay.receipt(_supportPresentation->status(), frame.ui.support_banner_drawn);
    _pendingFrame = std::move(frame);
}
void AndroidGame::Impl::presented(bool succeeded) {
    if (!_pendingFrame)
        return;
    const auto &frame = *_pendingFrame;
    if (!succeeded || !_foreground || frame.scene_revision != _sceneRevision ||
        frame.content_generation != _contentGeneration ||
        !_presentationEpoch.matches(frame.presentation_epoch)) {
        _pendingFrame.reset();
        return;
    }
    ++_presentedFrames;
    captureAccessibility();
    if (frame.ui.panel_token && _uiState.panel && frame.ui.panel_token == _uiState.panel->token)
        _drawnPanelToken = frame.ui.panel_token;
    if (frame.ui.dialogue_layout && _uiState.dialogue &&
        frame.ui.dialogue_identity == _uiState.dialogue->identity) {
        _dialogueLayout = frame.ui.dialogue_layout;
        _dialoguePresentedIdentity = frame.ui.dialogue_identity;
        _dialoguePresentedFirstLine = _uiState.dialogue->progress.first_line;
    }
    if (_panelPurpose != NativePanel::none || !_session) {
        _pendingFrame.reset();
        return;
    }
    if (frame.support) {
        if (_session->battle() && _supportPresentation &&
            _session->battle()->token == frame.support->token)
            _supportDisplay.observe(_supportPresentation->status(), *frame.support);
        _pendingFrame.reset();
        return;
    }
    if (frame.battle) {
        if (_session->battle() && _battlePresentation &&
            _session->battle()->token == frame.battle->token &&
            _battlePresentation->status().token == frame.battle->token) {
            _battlePresentedToken = frame.battle->token;
            finishBattleFrame(*frame.battle);
        }
    } else if (_session->battle()) {
        _pendingFrame.reset();
        return;
    }
    if (frame.world && _session->world_presentation() &&
        _session->world_presentation()->token == *frame.world && _preparedWorldToken &&
        *_preparedWorldToken == *frame.world) {
        _session->report_world_presented(*frame.world);
        _preparedWorldToken.reset();
        _rangeDirty = true;
        _checkpointRequested = true;
    }
    if (frame.mission_camera && _session->mission().gate == MissionGate::camera &&
        _session->mission().command_index == *frame.mission_camera)
        _session->report_camera(*frame.mission_camera, true);
    if (frame.session_focus && _session->focus_request() &&
        _session->focus_request()->token == *frame.session_focus) {
        _session->report_focus_presented(*frame.session_focus);
        _rangeDirty = true;
        _checkpointRequested = true;
    }
    for (auto &completion : frame.transforms) {
        auto i = completion.unit_index;
        if (i >= _session->units().size() || !_session->units()[i].active ||
            !_session->unit_transform_waiting(i) ||
            _session->unit_transform_generation(i) != completion.transform_generation ||
            _session->unit_spawn_generation(i) != completion.spawn_generation)
            continue;
        _session->report_transform_finished(i, completion.transform_generation);
        _rangeDirty = true;
        _checkpointRequested = true;
        for (auto &p : _pendingTransformCompletions)
            if (p && p->unit_index == i &&
                p->transform_generation == completion.transform_generation &&
                p->spawn_generation == completion.spawn_generation)
                p.reset();
    }
    saveCheckpoint();
    _pendingFrame.reset();
}
void AndroidGame::Impl::pause() {
    _accessibilitySnapshot = "{\"nodes\":[]}";
    _foreground = false;
    _presentationEpoch.invalidate();
    _pendingFrame.reset();
    _simulationClock.reset();
    _clockStarted = false;
    _elapsedNanosRemainder = 0;
    _inputEdges = 0;
    _dialoguePresentedIdentity = 0;
    _checkpointRequested = true;
    saveCheckpoint();
    _audio->foreground(false);
    std::ofstream diagnostic(_saveRoot + "/android-diagnostics.json");
    diagnostic << report() << '\n';
}
void AndroidGame::Impl::resume() {
    _foreground = true;
    _presentationEpoch.invalidate();
    _simulationClock.reset();
    _clockStarted = false;
    _elapsedNanosRemainder = 0;
    _inputEdges = 0;
    _dialoguePresentedIdentity = 0;
    if (_session)
        _previousUnits = _session->units();
    _audio->foreground(_audioFocus);
}
void AndroidGame::Impl::captureAccessibility() {
    std::ostringstream out;
    out << "{\"nodes\":[";
    bool first = true;
    if (_ui)
        for (const auto &n : _ui->accessibleNodes()) {
            if (!first)
                out << ',';
            first = false;
            out << "{\"id\":" << n.id << ",\"action\":" << n.action.id
                << ",\"label\":" << jsonString(n.label) << ",\"hint\":" << jsonString(n.hint)
                << ",\"value\":" << jsonString(n.value) << ",\"x\":" << n.bounds.x
                << ",\"y\":" << n.bounds.y << ",\"width\":" << n.bounds.width
                << ",\"height\":" << n.bounds.height
                << ",\"enabled\":" << (n.enabled ? "true" : "false")
                << ",\"scroll_forward\":" << (n.scroll_forward ? "true" : "false")
                << ",\"scroll_backward\":" << (n.scroll_backward ? "true" : "false") << '}';
        }
    out << "]}";
    _accessibilitySnapshot = out.str();
}
std::string AndroidGame::Impl::report() const {
    auto quote = [](const std::string &value) {
        std::ostringstream s;
        s << '"';
        for (unsigned char c : value) {
            if (c == '"' || c == '\\')
                s << '\\' << char(c);
            else if (c == '\n')
                s << "\\n";
            else if (c == '\r')
                s << "\\r";
            else if (c == '\t')
                s << "\\t";
            else if (c < 32)
                s << "?";
            else
                s << char(c);
        }
        s << '"';
        return s.str();
    };
    std::ostringstream s;
    s << "{\"ios_source_commit\":\"be203a911bc26313615ea450758413e5c9cf6462\",\"resource_"
         "revision\":\"017R1\",\"panel\":"
      << int(_panelPurpose) << ",\"loaded\":" << (_loaded ? "true" : "false")
      << ",\"failed\":" << (_failed ? "true" : "false")
      << ",\"session_installs\":" << _sessionInstalls
      << ",\"presented_frames\":" << _presentedFrames
      << ",\"simulation_ticks\":" << _simulationTicks
      << ",\"modern\":" << (_graphicsModern ? "true" : "false")
      << ",\"checkpoint_writes\":" << _checkpointWrites
      << ",\"checkpoint_loads\":" << _checkpointLoads << ",\"tactical_models\":" << _tacticalDrawn
      << ",\"tactical_fallbacks\":" << _tacticalFallback << ",\"error\":" << quote(_failure)
      << ",\"audio_error\":" << quote(_audio ? _audio->error() : "");
    if (_session) {
        auto &m = _session->mission();
        s << ",\"mission_gate\":" << int(m.gate) << ",\"mission_stop\":" << int(m.stop)
          << ",\"interaction_mode\":" << int(m.interaction_mode)
          << ",\"command_index\":" << m.command_index
          << ",\"mission_index\":" << _session->campaign_context().mission_index
          << ",\"world_pending\":" << (_session->world_presentation() ? "true" : "false")
          << ",\"selected\":"
          << (_session->selected() ? std::to_string(*_session->selected()) : "null")
          << ",\"moving\":" << (_session->moving() ? "true" : "false")
          << ",\"battle_token\":" << (_session->battle() ? _session->battle()->token : 0)
          << ",\"battle_completed\":" << _battleAcknowledgements
          << ",\"turn\":" << int(_session->current_player());
        if (m.dialogue)
            s << ",\"dialogue_text\":" << quote(_text->at(m.dialogue->text_id));
    }
    if (_battlePresentation) {
        const auto status = _battlePresentation->status();
        s << ",\"battle_visual_complete\":" << (status.complete ? "true" : "false")
          << ",\"battle_revision\":" << status.revision;
    }
    s << ",\"render_width\":" << _renderWidth << ",\"render_height\":" << _renderHeight
      << ",\"render_allocation_fallbacks\":" << _renderAllocationFallbacks
      << ",\"render_low_power\":" << (_renderLowPower ? "true" : "false")
      << ",\"render_thermal_serious\":" << (_renderThermalSerious ? "true" : "false")
      << ",\"render_memory_pressure\":" << (_renderMemoryPressure ? "true" : "false");
    s << ",\"move_actions\":" << _moveActions << ",\"capture_actions\":" << _captureActions
      << ",\"transform_actions\":" << _transformActions << ",\"attack_actions\":" << _attackActions
      << ",\"heal_actions\":" << _healActions << ",\"upgrade_actions\":" << _upgradeActions
      << ",\"end_turn_actions\":" << _endTurnActions << ",\"undo_actions\":" << _undoActions
      << ",\"production_actions\":" << _productionActions;
    s << ",\"unit_targets\":[";
    bool first = true;
    if (_scene && _session)
        for (std::size_t i = 0; i < _animations.size(); ++i) {
            const auto bounds = unitBounds(i);
            if (!bounds.valid)
                continue;
            Point center{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
            const auto hit = unitAtPoint(center, 0);
            if (!hit || *hit != i || center.x < 0 || center.x >= _width || center.y < 0 ||
                center.y >= _height)
                continue;
            const auto index = _scene->sprites[i].unit_index;
            const auto &unit = _session->world().units.at(index);
            const auto &movement = _session->units().at(index);
            if (!first)
                s << ',';
            first = false;
            s << "{\"index\":" << index << ",\"type\":" << int(unit.type)
              << ",\"faction\":" << int(unit.faction)
              << ",\"active\":" << (movement.active ? "true" : "false")
              << ",\"form\":" << int(movement.form) << ",\"tile_x\":" << int(movement.tile_x)
              << ",\"tile_y\":" << int(movement.tile_y) << ",\"x\":" << center.x
              << ",\"y\":" << center.y << '}';
        }
    s << "],\"tile_targets\":[";
    first = true;
    if (_moveMode && _previewRange && _scene) {
        const auto camera = _camera.matrix();
        for (const auto &q : _controlQuads) {
            if (q.face != TerrainFace::top || _previewRange->at(q.tile_x, q.tile_y) < 0 ||
                _previewRange->at(q.tile_x, q.tile_y) == movement_reserved_cell)
                continue;
            std::array<Point, 4> projected{};
            for (std::size_t i = 0; i < 4; ++i) {
                const auto &p = q.positions[i];
                auto c = transform(camera, {{p[0] * _scene->terrain.position_scale,
                                             p[1] * _scene->terrain.position_scale,
                                             p[2] * _scene->terrain.position_scale, 1}});
                projected[i] = {(c[0] / c[3] * .5 + .5) * _width,
                                (.5 - c[1] / c[3] * .5) * _height};
            }
            std::optional<Point> target;
            for (double v : {.5, .2, .8, .05, .95}) {
                for (double u : {.5, .2, .8, .05, .95}) {
                    const std::array<double, 4> weights{
                        {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v}};
                    Point point{};
                    for (std::size_t i = 0; i < 4; ++i) {
                        point.x += weights[i] * projected[i].x;
                        point.y += weights[i] * projected[i].y;
                    }
                    if (point.x < 0 || point.y < 0 || point.x >= _width || point.y >= _height ||
                        (_ui && _ui->hit(point.x, point.y)))
                        continue;
                    const auto hit = unitAtPoint(point, 0);
                    if (hit) {
                        const auto &unit = _session->units().at(_scene->sprites[*hit].unit_index);
                        if (unit.tile_x != q.tile_x || unit.tile_y != q.tile_y)
                            continue;
                    }
                    target = point;
                    break;
                }
                if (target)
                    break;
            }
            if (!target)
                continue;
            if (!first)
                s << ',';
            first = false;
            s << "{\"tile_x\":" << int(q.tile_x) << ",\"tile_y\":" << int(q.tile_y)
              << ",\"x\":" << target->x << ",\"y\":" << target->y << '}';
        }
    }
    s << "],\"menu_labels\":[";
    first = true;
    if (_uiState.panel)
        for (auto &b : _uiState.panel->buttons) {
            if (!first)
                s << ',';
            first = false;
            s << quote(b.label);
        }
    s << "],\"hit_targets\":[";
    first = true;
    if (_ui) {
        std::map<std::pair<int, int>, std::pair<int, int>> targets;
        for (int y = 4; y < _height; y += 8)
            for (int x = 4; x < _width; x += 8) {
                auto a = _ui->hit(float(x), float(y));
                if (a && !targets.count({a.id, a.value}))
                    targets[{a.id, a.value}] = {x, y};
            }
        for (auto &item : targets) {
            if (!first)
                s << ',';
            first = false;
            s << "{\"id\":" << item.first.first << ",\"value\":" << item.first.second
              << ",\"x\":" << item.second.first << ",\"y\":" << item.second.second << '}';
        }
    }
    s << "]}";
    return s.str();
}

#include "IOSScenePort.inc"
#include "IOSBattlePort.inc"
#include "IOSRenderPort.inc"

AndroidGame::AndroidGame(std::string assets, std::string saves, bool modern)
    : impl_(std::make_unique<Impl>(std::move(assets), std::move(saves), modern)) {}
AndroidGame::~AndroidGame() = default;
void AndroidGame::setTextRasterizer(std::function<RgbaImage(const std::string &, float)> raster) {
    impl_->_rasterizer = std::move(raster);
    if (impl_->_ui)
        impl_->_ui->setSystemTextRasterizer(impl_->_rasterizer);
}
void AndroidGame::attachGraphics() {
    impl_->attach();
}
void AndroidGame::detachGraphics(bool lost) {
    impl_->detach(lost);
}
void AndroidGame::resize(int w, int h, int left, int top, int right, int bottom) {
    impl_->resize(w, h, {double(top), double(left), double(bottom), double(right)});
}
void AndroidGame::draw(std::uint64_t delta) {
    impl_->draw(delta);
}
void AndroidGame::presented(bool ok) {
    impl_->presented(ok);
}
void AndroidGame::touch(int a, float x, float y, float dx, float dy, float scale) {
    impl_->touch(a, x, y, dx, dy, scale);
}
bool AndroidGame::back() {
    if (impl_->_failed || impl_->_panelPurpose == NativePanel::startup ||
        impl_->_panelPurpose == NativePanel::menu)
        return false;
    impl_->back();
    return true;
}
void AndroidGame::pause() {
    impl_->pause();
}
void AndroidGame::resume() {
    impl_->resume();
}
void AndroidGame::audioFocus(bool focused) {
    impl_->_audioFocus = focused;
    impl_->_audio->foreground(impl_->_foreground && focused);
}
void AndroidGame::resourcePressure(bool lowPower, bool thermalSerious, bool memoryPressure) {
    impl_->_renderLowPower = lowPower;
    impl_->_renderThermalSerious = thermalSerious;
    impl_->_renderMemoryPressure = memoryPressure;
}
std::optional<std::string> AndroidGame::accessibilitySnapshot(bool force) {
    if (!force && impl_->_accessibilitySnapshot == impl_->_accessibilityDelivered)
        return std::nullopt;
    impl_->_accessibilityDelivered = impl_->_accessibilitySnapshot;
    return impl_->_accessibilitySnapshot;
}
std::string AndroidGame::diagnostics() const {
    return impl_->report();
}
} // namespace g1::android
